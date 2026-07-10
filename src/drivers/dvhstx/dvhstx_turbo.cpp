#include <stdlib.h>
#include <string.h>

#include <pico/stdlib.h>

#include "hardware/sync.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pll.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"

#include "dvhstx.hpp" // DVHSTXPinout; also pulls in the clock preinit
#include "dvhstx_turbo.hpp"

using namespace pimoroni;

// Per-line command word counts. Vblank lines are 3 RAW_REPEAT pairs;
// active lines add the TMDS header word before the pixel data.
#define VBLANK_LINE_WORDS 6
#define ACTIVE_HEADER_WORDS 7

void DVHSTXTurbo::setup_hstx_clock() {
  // Same scheme as DVHSTX::display_setup_clock(): clk_sys already runs at
  // 240MHz off the USB PLL (set by the preinit hook), so pll_sys is free
  // to generate the HSTX clock at bit_clk/2.
  const uint32_t dvi_clock_khz = timing->bit_clk_khz >> 1;
  uint vco_freq, post_div1, post_div2;
  if (!check_sys_clock_khz(dvi_clock_khz, &vco_freq, &post_div1, &post_div2))
    panic("HSTX clock of %u kHz cannot be exactly achieved", dvi_clock_khz);
  const uint32_t freq = vco_freq / (post_div1 * post_div2);

  pll_init(pll_sys, PLL_COMMON_REFDIV, vco_freq, post_div1, post_div2);

  clock_configure(clk_hstx, 0, CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                  freq, freq);
}

void DVHSTXTurbo::build_frame_commands() {
  uint32_t *p = frame_cmd_buffer;

  auto put_vblank_line = [&](bool vsync_on) {
    const uint32_t s_h1 = vsync_on ? SYNC_V0_H1 : SYNC_V1_H1;
    const uint32_t s_h0 = vsync_on ? SYNC_V0_H0 : SYNC_V1_H0;
    *p++ = HSTX_CMD_RAW_REPEAT | timing->h_front_porch;
    *p++ = s_h1;
    *p++ = HSTX_CMD_RAW_REPEAT | timing->h_sync_width;
    *p++ = s_h0;
    *p++ = HSTX_CMD_RAW_REPEAT | (timing->h_back_porch + timing->h_active_pixels);
    *p++ = s_h1;
  };

  // Frame starts at the vertical front porch, same convention as DVHSTX.
  for (int i = 0; i < timing->v_front_porch; i++)
    put_vblank_line(false);
  for (int i = 0; i < timing->v_sync_width; i++)
    put_vblank_line(true);
  for (int i = 0; i < timing->v_back_porch; i++)
    put_vblank_line(false);

  active_start_offset = (uint32_t)((uint8_t *)p - (uint8_t *)frame_cmd_buffer);

  for (int y = 0; y < timing->v_active_lines; y++) {
    *p++ = HSTX_CMD_RAW_REPEAT | timing->h_front_porch;
    *p++ = SYNC_V1_H1;
    *p++ = HSTX_CMD_RAW_REPEAT | timing->h_sync_width;
    *p++ = SYNC_V1_H0;
    *p++ = HSTX_CMD_RAW_REPEAT | timing->h_back_porch;
    *p++ = SYNC_V1_H1;
    *p++ = HSTX_CMD_TMDS | timing->h_active_pixels;
    if (y == 0)
      frame_pixels_base = (uint8_t *)p;
    p += pixel_words; // pixel bytes, left blank here (calloc'd to 0)
  }
}

bool DVHSTXTurbo::init(uint16_t width, uint16_t height,
                       const DVHSTXPinout &pinout) {
  if (inited)
    reset();

  if (width == 720 && height == 480)
    timing = &dvi_timing_720x480p_60hz;
  else if (width == 640 && height == 480)
    timing = &dvi_timing_640x480p_60hz;
  else
    return false; // unsupported resolution

  disp_width = width;
  disp_height = height;
  pixel_words = width / 4; // 8bpp, 4 pixels per word

  const int v_blank_lines =
      timing->v_front_porch + timing->v_sync_width + timing->v_back_porch;
  frame_words = (uint32_t)v_blank_lines * VBLANK_LINE_WORDS +
                (uint32_t)timing->v_active_lines *
                    (ACTIVE_HEADER_WORDS + pixel_words);
  row_stride = (ACTIVE_HEADER_WORDS + pixel_words) * sizeof(uint32_t);

  // calloc so the pixel regions start black
  frame_cmd_buffer = (uint32_t *)calloc(frame_words, sizeof(uint32_t));
  if (!frame_cmd_buffer)
    return false;

  build_frame_commands();

  setup_hstx_clock();

  // Ensure HSTX FIFO is clear
  reset_block_num(RESET_HSTX);
  sleep_us(10);
  unreset_block_num_wait_blocking(RESET_HSTX);
  sleep_us(10);

  // TMDS encoder input is the low byte of the shift register, RGB332:
  // red [7:5], green [4:2], blue [1:0]. Rotations put each field's MSB at
  // bit 7 of the lane input (same scheme as MODE_TEXT_RGB111's 2-bit
  // fields, widened).
  hstx_ctrl_hw->expand_tmds =
      2 << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB |
      0 << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB |
      2 << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
      29 << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB |
      1 << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
      26 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;

  // Pixels (TMDS) come as 4 8-bit chunks per word, first pixel in the low
  // byte. Control symbols (RAW) are an entire 32-bit word.
  hstx_ctrl_hw->expand_shift =
      4 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
      8 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
      1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
      0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;

  // Serial output: clock period 5 cycles, pop every 5 cycles, shift by 2
  // per cycle. Identical to DVHSTX.
  hstx_ctrl_hw->csr = 0;
  hstx_ctrl_hw->csr = HSTX_CTRL_CSR_EXPAND_EN_BITS |
                      5u << HSTX_CTRL_CSR_CLKDIV_LSB |
                      5u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
                      2u << HSTX_CTRL_CSR_SHIFT_LSB | HSTX_CTRL_CSR_EN_BITS;

  // Pin mapping, identical to DVHSTX
  constexpr int HSTX_FIRST_PIN = 12;
  hstx_ctrl_hw->bit[(pinout.clk_p) - HSTX_FIRST_PIN] = HSTX_CTRL_BIT0_CLK_BITS;
  hstx_ctrl_hw->bit[(pinout.clk_p ^ 1) - HSTX_FIRST_PIN] =
      HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
  for (uint lane = 0; lane < 3; ++lane) {
    int bit = pinout.rgb_p[lane];
    uint32_t lane_data_sel_bits =
        (lane * 10) << HSTX_CTRL_BIT0_SEL_P_LSB |
        (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;
    hstx_ctrl_hw->bit[(bit)-HSTX_FIRST_PIN] = lane_data_sel_bits;
    hstx_ctrl_hw->bit[(bit ^ 1) - HSTX_FIRST_PIN] =
        lane_data_sel_bits | HSTX_CTRL_BIT0_INV_BITS;
  }

  for (int i = 12; i <= 19; ++i) {
    gpio_set_function(i, GPIO_FUNC_HSTX);
    gpio_set_drive_strength(i, GPIO_DRIVE_STRENGTH_4MA);
  }

  // Two-channel free-running loop:
  //   data:    streams the whole frame to the HSTX FIFO, paced by
  //            DREQ_HSTX, then chains to restart.
  //   restart: one unpaced word -- writes the buffer address into data's
  //            READ_ADDR trigger alias, which re-arms data with its
  //            reloaded transfer count. No IRQ anywhere.
  int d = dma_claim_unused_channel(false);
  int r = dma_claim_unused_channel(false);
  if (d < 0 || r < 0) {
    if (d >= 0)
      dma_channel_unclaim(d);
    if (r >= 0)
      dma_channel_unclaim(r);
    hstx_ctrl_hw->csr = 0;
    free(frame_cmd_buffer);
    frame_cmd_buffer = nullptr;
    return false;
  }
  dma_ch_data = d;
  dma_ch_restart = r;

  restart_read_word = (uintptr_t)frame_cmd_buffer;

  dma_channel_config c = dma_channel_get_default_config(dma_ch_data);
  channel_config_set_dreq(&c, DREQ_HSTX);
  channel_config_set_chain_to(&c, dma_ch_restart);
  // Win round-robin inside the DMA block too, so a mem-to-mem blit
  // running flat out can't add latency to the scanout feed.
  channel_config_set_high_priority(&c, true);
  dma_channel_configure(dma_ch_data, &c, &hstx_fifo_hw->fifo,
                        frame_cmd_buffer, frame_words, false);

  c = dma_channel_get_default_config(dma_ch_restart);
  channel_config_set_read_increment(&c, false);
  channel_config_set_write_increment(&c, false);
  dma_channel_configure(dma_ch_restart, &c,
                        &dma_hw->ch[dma_ch_data].al3_read_addr_trig,
                        &restart_read_word, 1, false);

  // Give DMA priority on the SRAM fabric. The scanout feed has a hard
  // deadline the cores don't -- if a core wins a bank collision and the
  // data read arrives after the HSTX FIFO drains, that scanline glitches.
  // There is no ISR and no line-buffer depth to absorb the jitter in this
  // mode, so the feed is naked; this closes the bus-contention underrun
  // path (distinct from the interrupt-latency one the ring driver guards).
  bus_ctrl_hw->priority =
      BUSCTRL_BUS_PRIORITY_DMA_R_BITS | BUSCTRL_BUS_PRIORITY_DMA_W_BITS;

  dma_channel_start(dma_ch_data);

  inited = true;
  return true;
}

void DVHSTXTurbo::reset() {
  if (!inited)
    return;
  inited = false;

  hstx_ctrl_hw->csr = 0;

  if (dma_ch_data >= 0) {
    dma_channel_abort(dma_ch_data);
    dma_channel_abort(dma_ch_restart);
    dma_channel_unclaim(dma_ch_data);
    dma_channel_unclaim(dma_ch_restart);
    dma_ch_data = dma_ch_restart = -1;
  }

  if (dma_ch_blit >= 0) {
    dma_channel_abort(dma_ch_blit);
    dma_channel_abort(dma_ch_blit_ctrl);
    dma_channel_unclaim(dma_ch_blit);
    dma_channel_unclaim(dma_ch_blit_ctrl);
    dma_ch_blit = dma_ch_blit_ctrl = -1;
  }
  free(blit_blocks);
  blit_blocks = nullptr;
  blit_blocks_end = nullptr;

  // restore fair arbitration -- don't leave the fabric altered after end()
  bus_ctrl_hw->priority = 0;

  free(frame_cmd_buffer);
  frame_cmd_buffer = nullptr;
  frame_pixels_base = nullptr;
}

void DVHSTXTurbo::wait_for_vsync() const {
  if (!inited)
    return;
  const uintptr_t base = (uintptr_t)frame_cmd_buffer;
  const uintptr_t active = base + active_start_offset;

  // wait for the active region first so a full vblank is ahead on return
  while (dma_hw->ch[dma_ch_data].read_addr < active)
    tight_loop_contents();
  while (dma_hw->ch[dma_ch_data].read_addr >= active)
    tight_loop_contents();
}

bool DVHSTXTurbo::blit_busy() const {
  if (dma_ch_blit < 0)
    return false;
  // The chain is done when the control channel has consumed the final
  // null block AND neither channel is mid-transfer. Checking busy flags
  // alone races the chain-trigger handoff between rows.
  if (dma_channel_is_busy(dma_ch_blit) || dma_channel_is_busy(dma_ch_blit_ctrl))
    return true;
  return dma_hw->ch[dma_ch_blit_ctrl].read_addr < (uintptr_t)blit_blocks_end;
}

bool DVHSTXTurbo::blit_rows(int y, const uint8_t *src, int n_rows,
                            bool block) {
  if (!inited || !src)
    return false;

  // clip
  if (y < 0) {
    src += (size_t)(-y) * disp_width;
    n_rows += y;
    y = 0;
  }
  if (y + n_rows > disp_height)
    n_rows = disp_height - y;
  if (n_rows <= 0)
    return true;

  // one blit at a time
  blit_wait();

  // Fallback path: misaligned source, or DMA resources unavailable.
  bool dma_ok = ((uintptr_t)src & 3) == 0;

  if (dma_ok && dma_ch_blit < 0) {
    // lazy one-time setup
    int d = dma_claim_unused_channel(false);
    int c = (d >= 0) ? dma_claim_unused_channel(false) : -1;
    uint32_t *blocks = (uint32_t *)malloc((disp_height + 1) * 2 * sizeof(uint32_t));
    if (d < 0 || c < 0 || !blocks) {
      if (d >= 0)
        dma_channel_unclaim(d);
      if (c >= 0)
        dma_channel_unclaim(c);
      free(blocks);
      dma_ok = false;
    } else {
      dma_ch_blit = d;
      dma_ch_blit_ctrl = c;
      blit_blocks = blocks;

      // data: one row per trigger; count reloads automatically
      dma_channel_config cfg = dma_channel_get_default_config(dma_ch_blit);
      channel_config_set_read_increment(&cfg, true);
      channel_config_set_write_increment(&cfg, true);
      channel_config_set_chain_to(&cfg, dma_ch_blit_ctrl);
      dma_channel_configure(dma_ch_blit, &cfg, nullptr, nullptr, pixel_words,
                            false);

      // control: feeds {READ_ADDR, WRITE_ADDR_TRIG} pairs into the data
      // channel via the AL2 alias window (8-byte write ring)
      cfg = dma_channel_get_default_config(dma_ch_blit_ctrl);
      channel_config_set_read_increment(&cfg, true);
      channel_config_set_write_increment(&cfg, true);
      channel_config_set_ring(&cfg, true /*write*/, 3 /*2^3 = 8 bytes*/);
      dma_channel_configure(dma_ch_blit_ctrl, &cfg,
                            &dma_hw->ch[dma_ch_blit].al2_read_addr, nullptr,
                            2, false);
    }
  }

  if (!dma_ok) {
    for (int r = 0; r < n_rows; r++)
      memcpy(row(y + r), src + (size_t)r * disp_width, disp_width);
    return true;
  }

  // build the control-block list: one {src, dst} pair per row, then a
  // null pair -- writing 0 to a trigger register is a null trigger, which
  // ends the chain
  uint32_t *b = blit_blocks;
  for (int r = 0; r < n_rows; r++) {
    *b++ = (uintptr_t)(src + (size_t)r * disp_width);
    *b++ = (uintptr_t)row(y + r);
  }
  *b++ = 0;
  *b++ = 0;
  blit_blocks_end = b;

  // ensure block writes land before DMA reads them
  __compiler_memory_barrier();
  __dmb();

  dma_channel_set_read_addr(dma_ch_blit_ctrl, blit_blocks, true);

  if (block)
    blit_wait();
  return true;
}
