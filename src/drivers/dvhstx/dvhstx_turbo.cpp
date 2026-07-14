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


// ---------------------------------------------------------------------------
// HDMI data island support (opt-in via init(..., hdmi=true))
//
// A DVI signal becomes minimally-HDMI by transmitting an AVI InfoFrame in a
// data island once per frame, plus video preambles/guard bands before each
// active line. These symbols are generated during initialization and stored
// in the static command stream.
//
// TERC4 and BCH tables follow HDMI 1.3 and PicoDVI
// (BSD-3-Clause, Copyright (c) 2021 Luke Wren and contributors).
// ---------------------------------------------------------------------------

// Control-period symbols for the CTL[3:0] preamble patterns (10-bit TMDS
// control codes, same convention as TMDS_CTRL_xx in dvi.hpp).
// Data island preamble: CTL = 0101 -> ch1 = CTRL_01, ch2 = CTRL_01
// Video preamble:       CTL = 0001 -> ch1 = CTRL_01, ch2 = CTRL_00
#define HDMI_DI_PREAMBLE(ch0_sync) \
  ((ch0_sync) | (TMDS_CTRL_01 << 10) | ((uint32_t)TMDS_CTRL_01 << 20))
#define HDMI_VIDEO_PREAMBLE(ch0_sync) \
  ((ch0_sync) | (TMDS_CTRL_01 << 10) | ((uint32_t)TMDS_CTRL_00 << 20))

// Video guard band (2 clocks): fixed symbols per lane.
#define HDMI_VIDEO_GUARD \
  (0x2CCu | (0x133u << 10) | (0x2CCu << 20))

// TERC4: 4-bit data -> 10-bit symbols, used on all lanes during islands.
static const uint16_t terc4_syms[16] = {
    0b1010011100, 0b1001100011, 0b1011100100, 0b1011100010,
    0b0101110001, 0b0100011110, 0b0110001110, 0b0100111100,
    0b1011001100, 0b0100111001, 0b0110011100, 0b1011000110,
    0b1010001110, 0b1001110001, 0b0101100011, 0b1011000011,
};

// BCH ECC byte table (HDMI packet error correction), from PicoDVI.
static const uint8_t bch_table[256] = {
    0x00, 0xd9, 0xb5, 0x6c, 0x6d, 0xb4, 0xd8, 0x01, 0xda, 0x03, 0x6f, 0xb6,
    0xb7, 0x6e, 0x02, 0xdb, 0xb3, 0x6a, 0x06, 0xdf, 0xde, 0x07, 0x6b, 0xb2,
    0x69, 0xb0, 0xdc, 0x05, 0x04, 0xdd, 0xb1, 0x68, 0x61, 0xb8, 0xd4, 0x0d,
    0x0c, 0xd5, 0xb9, 0x60, 0xbb, 0x62, 0x0e, 0xd7, 0xd6, 0x0f, 0x63, 0xba,
    0xd2, 0x0b, 0x67, 0xbe, 0xbf, 0x66, 0x0a, 0xd3, 0x08, 0xd1, 0xbd, 0x64,
    0x65, 0xbc, 0xd0, 0x09, 0xc2, 0x1b, 0x77, 0xae, 0xaf, 0x76, 0x1a, 0xc3,
    0x18, 0xc1, 0xad, 0x74, 0x75, 0xac, 0xc0, 0x19, 0x71, 0xa8, 0xc4, 0x1d,
    0x1c, 0xc5, 0xa9, 0x70, 0xab, 0x72, 0x1e, 0xc7, 0xc6, 0x1f, 0x73, 0xaa,
    0xa3, 0x7a, 0x16, 0xcf, 0xce, 0x17, 0x7b, 0xa2, 0x79, 0xa0, 0xcc, 0x15,
    0x14, 0xcd, 0xa1, 0x78, 0x10, 0xc9, 0xa5, 0x7c, 0x7d, 0xa4, 0xc8, 0x11,
    0xca, 0x13, 0x7f, 0xa6, 0xa7, 0x7e, 0x12, 0xcb, 0x83, 0x5a, 0x36, 0xef,
    0xee, 0x37, 0x5b, 0x82, 0x59, 0x80, 0xec, 0x35, 0x34, 0xed, 0x81, 0x58,
    0x30, 0xe9, 0x85, 0x5c, 0x5d, 0x84, 0xe8, 0x31, 0xea, 0x33, 0x5f, 0x86,
    0x87, 0x5e, 0x32, 0xeb, 0xe2, 0x3b, 0x57, 0x8e, 0x8f, 0x56, 0x3a, 0xe3,
    0x38, 0xe1, 0x8d, 0x54, 0x55, 0x8c, 0xe0, 0x39, 0x51, 0x88, 0xe4, 0x3d,
    0x3c, 0xe5, 0x89, 0x50, 0x8b, 0x52, 0x3e, 0xe7, 0xe6, 0x3f, 0x53, 0x8a,
    0x41, 0x98, 0xf4, 0x2d, 0x2c, 0xf5, 0x99, 0x40, 0x9b, 0x42, 0x2e, 0xf7,
    0xf6, 0x2f, 0x43, 0x9a, 0xf2, 0x2b, 0x47, 0x9e, 0x9f, 0x46, 0x2a, 0xf3,
    0x28, 0xf1, 0x9d, 0x44, 0x45, 0x9c, 0xf0, 0x29, 0x20, 0xf9, 0x95, 0x4c,
    0x4d, 0x94, 0xf8, 0x21, 0xfa, 0x23, 0x4f, 0x96, 0x97, 0x4e, 0x22, 0xfb,
    0x93, 0x4a, 0x26, 0xff, 0xfe, 0x27, 0x4b, 0x92, 0x49, 0x90, 0xfc, 0x25,
    0x24, 0xfd, 0x91, 0x48,
};

static uint8_t bch_ecc(const uint8_t *p, int n) {
  uint8_t v = 0;
  for (int i = 0; i < n; i++)
    v = bch_table[p[i] ^ v];
  return v;
}

// Build the 32 island-body words (ch0|ch1<<10|ch2<<20 per pixel clock) for
// one HDMI packet. hv = live (VSYNC<<1)|HSYNC levels during the island.
static void hdmi_encode_packet_words(const uint8_t header[4],
                                     const uint8_t sub[4][8], uint32_t hv,
                                     uint32_t *out) {
  for (int t = 0; t < 32; t++) {
    uint32_t hdr_bit = (header[t >> 3] >> (t & 7)) & 1;
    // ch0: D3 = 0 only on the island's first clock, D2 = header bit,
    // D1 = VSYNC level, D0 = HSYNC level
    uint32_t ch0 = terc4_syms[(t ? 8 : 0) | (hdr_bit << 2) | hv];
    // ch1/ch2: bit k = subpacket k's even/odd bit for this clock
    uint32_t n1 = 0, n2 = 0;
    int byte = t >> 2, sh = (t & 3) << 1;
    for (int k = 0; k < 4; k++) {
      n1 |= ((sub[k][byte] >> sh) & 1) << k;
      n2 |= ((sub[k][byte] >> (sh + 1)) & 1) << k;
    }
    out[t] = ch0 | ((uint32_t)terc4_syms[n1] << 10) |
             ((uint32_t)terc4_syms[n2] << 20);
  }
}

// AVI InfoFrame (CEA-861): declares VIC + picture aspect (the anamorphic
// flag), full-range RGB, underscan, and IT/graphics content signaling.
static void hdmi_build_avi_packet(uint8_t vic, uint8_t aspect_m,
                                  uint8_t header[4], uint8_t sub[4][8]) {
  header[0] = 0x82; // AVI InfoFrame
  header[1] = 0x02; // version 2
  header[2] = 13;   // length
  header[3] = bch_ecc(header, 3);

  uint8_t pb[14] = {0};
  pb[1] = 0x12;                     // A=1, RGB, no bars, underscan
  pb[2] = (aspect_m << 4) | 0x08;   // picture aspect; AFAR = same as picture
  pb[3] = 0x88;                     // ITC=1, Q = full-range RGB
  pb[4] = vic;                      // video identification code
  pb[5] = 0x00;                     // CN = graphics content
  // pb[6..13] = 0: no pixel repetition, no bar info
  uint32_t sum = header[0] + header[1] + header[2];
  for (int i = 1; i < 14; i++)
    sum += pb[i];
  pb[0] = (uint8_t)(0x100 - (sum & 0xFF)); // checksum

  memset(sub, 0, 4 * 8);
  memcpy(sub[0], &pb[0], 7);
  memcpy(sub[1], &pb[7], 7);
  sub[0][7] = bch_ecc(sub[0], 7);
  sub[1][7] = bch_ecc(sub[1], 7);
  sub[2][7] = bch_ecc(sub[2], 7); // zero data still gets parity
  sub[3][7] = bch_ecc(sub[3], 7);
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

  // In HDMI mode, the first vblank line carries the AVI InfoFrame in a
  // data island. Put the whole island inside the hsync pulse, after a
  // short control period, so HSYNC is stable before the data-island
  // preamble and remains stable through both guard bands and the packet.
  if (hdmi_mode) {
    uint8_t header[4], sub[4][8];
    hdmi_build_avi_packet(disp_width == 720 ? 3 : 1, // VIC 3 / VIC 1
                          disp_width == 720 ? 2 : 1, // 16:9 / 4:3
                          header, sub);
    const uint32_t hv = 0b10; // V inactive high, H active low
    const uint32_t di_guard =
        terc4_syms[0b1100 | hv] | (0x133u << 10) | (0x133u << 20);

    constexpr uint32_t sync_settle_clocks = 4;
    constexpr uint32_t island_sync_clocks =
        sync_settle_clocks + 8 + 2 + 32 + 2;
    if (timing->h_sync_width < island_sync_clocks)
      panic("HSYNC width %u is too short for HDMI data island",
            timing->h_sync_width);

    *p++ = HSTX_CMD_RAW_REPEAT | timing->h_front_porch;
    *p++ = SYNC_V1_H1;
    *p++ = HSTX_CMD_RAW_REPEAT | sync_settle_clocks;
    *p++ = SYNC_V1_H0;
    *p++ = HSTX_CMD_RAW_REPEAT | 8;
    *p++ = HDMI_DI_PREAMBLE(TMDS_CTRL_10);
    *p++ = HSTX_CMD_RAW_REPEAT | 2;
    *p++ = di_guard;
    *p++ = HSTX_CMD_RAW | 32;
    hdmi_encode_packet_words(header, sub, hv, p);
    p += 32;
    *p++ = HSTX_CMD_RAW_REPEAT | 2;
    *p++ = di_guard;
    *p++ = HSTX_CMD_RAW_REPEAT |
           (timing->h_sync_width - island_sync_clocks);
    *p++ = SYNC_V1_H0;
    *p++ = HSTX_CMD_RAW_REPEAT |
           (timing->h_back_porch + timing->h_active_pixels);
    *p++ = SYNC_V1_H1;
  }

  // Frame starts at the vertical front porch, same convention as DVHSTX.
  for (int i = hdmi_mode ? 1 : 0; i < timing->v_front_porch; i++)
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
    if (hdmi_mode) {
      // HDMI requires an 8-clock video preamble + 2-clock guard band
      // immediately before every active video period; carve them out of
      // the back porch.
      *p++ = HSTX_CMD_RAW_REPEAT | (timing->h_back_porch - 10);
      *p++ = SYNC_V1_H1;
      *p++ = HSTX_CMD_RAW_REPEAT | 8;
      *p++ = HDMI_VIDEO_PREAMBLE(TMDS_CTRL_11);
      *p++ = HSTX_CMD_RAW_REPEAT | 2;
      *p++ = HDMI_VIDEO_GUARD;
    } else {
      *p++ = HSTX_CMD_RAW_REPEAT | timing->h_back_porch;
      *p++ = SYNC_V1_H1;
    }
    *p++ = HSTX_CMD_TMDS | timing->h_active_pixels;
    if (y == 0)
      frame_pixels_base = (uint8_t *)p;
    p += pixel_words; // pixel bytes, left blank here (calloc'd to 0)
  }

  // Record the generated size. init() compares it with the DMA transfer
  // length before scanout starts.
  emitted_words = (uint32_t)(p - frame_cmd_buffer);
}

bool DVHSTXTurbo::init(uint16_t width, uint16_t height,
                       const DVHSTXPinout &pinout, bool hdmi) {
  if (inited)
    reset();

  hdmi_mode = hdmi;

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
  // HDMI: island line is 47 words instead of 6, active headers gain the
  // video preamble + guard band (4 words).
  const int island_extra = hdmi_mode ? (47 - VBLANK_LINE_WORDS) : 0;
  const int header_words = ACTIVE_HEADER_WORDS + (hdmi_mode ? 4 : 0);
  frame_words = (uint32_t)v_blank_lines * VBLANK_LINE_WORDS + island_extra +
                (uint32_t)timing->v_active_lines *
                    (header_words + pixel_words);
  row_stride = (header_words + pixel_words) * sizeof(uint32_t);

  // calloc so the pixel regions start black
  frame_cmd_buffer = (uint32_t *)calloc(frame_words, sizeof(uint32_t));
  if (!frame_cmd_buffer)
    return false;

  build_frame_commands();
  if (emitted_words != frame_words) {
    // layout math and emission disagree -- refuse to stream garbage
    free(frame_cmd_buffer);
    frame_cmd_buffer = nullptr;
    return false;
  }

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
