#pragma once

#include "dvi.hpp"
#include "pico/stdlib.h"

#if !PICO_RP2350
#error "This library requires an RP2350 board -- the HSTX peripheral does not exist on the RP2040."
#endif

namespace pimoroni {

  struct DVHSTXPinout; // dvhstx.hpp

  // DMA-driven 8-bit RGB332 display driver.
  //
  // Unlike DVHSTX, there is no per-scanline ISR and no line buffers. The
  // HSTX command words (sync sequences and TMDS headers) are unrolled into
  // one whole-frame buffer with the pixel bytes interleaved between them.
  // A pair of DMA channels repeatedly streams that buffer to the HSTX FIFO.
  // The HSTX TMDS encoder expands RGB332 pixels in hardware, so scanout does
  // not depend on per-line CPU service.
  //
  // Costs: single-buffered only (~352KB at 720x480, ~314KB at 640x480;
  // two copies don't fit in SRAM), fixed RGB332 color (the expander's
  // bit->lane mapping replaces the palette), and rows are strided --
  // command words sit between pixel rows (28 bytes in DVI mode, 44 bytes
  // in HDMI mode). Use row() /
  // row_stride_bytes() to draw.
  class DVHSTXTurbo {
  public:
    ~DVHSTXTurbo() { reset(); }

    // Supported: 720x480 and 640x480 (both CEA/VESA 60Hz timing).
    //
    // hdmi=false (default): pure DVI output, maximum monitor
    // compatibility.
    // hdmi=true (EXPERIMENTAL): adds HDMI data islands -- an AVI
    // InfoFrame is transmitted once per frame declaring the video format
    // (VIC 3, 16:9 anamorphic for 720x480; VIC 1, 4:3 for 640x480),
    // full-range RGB quantization, underscan, and IT/graphics content.
    // May help 720x480 display better on TVs that don't scale it
    // properly on their own; TV behavior varies and some ignore the
    // InfoFrame entirely. Costs ~8KB extra RAM (wider command stream).
    // The islands and per-line video guard bands are generated during
    // initialization and stored in the static command stream. Some pure DVI
    // monitors may reject the data-island symbols, so this option is disabled
    // by default. It does not alter the DVI path when false.
    bool init(uint16_t width, uint16_t height, const DVHSTXPinout &pinout,
              bool hdmi = false);

    bool is_hdmi() const { return hdmi_mode; }

    int width() const { return disp_width; }
    int height() const { return disp_height; }
    void reset();

    bool is_inited() const { return inited; }

    // Pointer to the first pixel byte of row y (width() bytes per row).
    uint8_t *row(int y) {
      return frame_pixels_base + (size_t)y * row_stride;
    }
    const uint8_t *row(int y) const {
      return frame_pixels_base + (size_t)y * row_stride;
    }

    // Distance in bytes from row y to row y+1 (includes the interleaved
    // command words -- do not write into the gap).
    size_t row_stride_bytes() const { return row_stride; }

    // Blocks until scanout enters the vertical blanking block. Waits for
    // the active region first, so on return a full vblank (~1.4ms) is
    // ahead. Polls the DMA read pointer -- there is no vsync IRQ.
    void wait_for_vsync() const;

    // RGB888 -> RGB332
    static constexpr uint8_t color(uint8_t r, uint8_t g, uint8_t b) {
      return (r & 0xE0) | ((g & 0xE0) >> 3) | (b >> 6);
    }

    // Copy n_rows of width() pixels from a contiguous source buffer into
    // the (strided) frame, starting at row y. Uses a scatter-gather DMA
    // chain -- one control block per row -- so the interleaved command
    // words between rows are never touched. src must be 4-byte aligned;
    // if it isn't (or no DMA channels are free) a memcpy fallback is used.
    //
    // block=true waits for completion. block=false returns immediately;
    // do not modify src or start another blit until blit_busy() is false.
    // Rows outside the display are clipped.
    bool blit_rows(int y, const uint8_t *src, int n_rows, bool block = true);
    bool blit_busy() const;
    void blit_wait() const {
      while (blit_busy())
        tight_loop_contents();
    }

  private:
    void setup_hstx_clock();
    void build_frame_commands();

    uint32_t *frame_cmd_buffer = nullptr; // whole-frame command+pixel stream
    uint8_t *frame_pixels_base = nullptr; // first pixel byte of row 0
    size_t row_stride = 0;                // bytes, row y -> row y+1
    uint32_t frame_words = 0;
    uint32_t emitted_words = 0;          // build-time self-check
    uint32_t pixel_words = 0;             // pixel words per active line
    uint32_t active_start_offset = 0;     // byte offset of first active line
    uint16_t disp_width = 0;
    uint16_t disp_height = 0;
    bool hdmi_mode = false;

    // read by the restart DMA channel, holds &frame_cmd_buffer[0]
    uintptr_t restart_read_word = 0;

    int8_t dma_ch_data = -1;
    int8_t dma_ch_restart = -1;

    // blit engine (lazily claimed on first blit_rows call)
    int8_t dma_ch_blit = -1;
    int8_t dma_ch_blit_ctrl = -1;
    uint32_t *blit_blocks = nullptr;    // {read, write_trig} pairs + null
    const uint32_t *blit_blocks_end = nullptr;

    const struct dvi_timing *timing = nullptr;
    bool inited = false;
  };

} // namespace pimoroni
