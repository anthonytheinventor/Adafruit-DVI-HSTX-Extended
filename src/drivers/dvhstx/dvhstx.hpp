#pragma once

#include <string.h>

#include "dvi.hpp"
#include "pico/stdlib.h"
#include "hardware/gpio.h"

// HSTX only exists on the RP2350. Note ARDUINO_ARCH_RP2040 is defined for
// both chips, so check PICO_RP2350 instead.
#if !PICO_RP2350
#error "This library requires an RP2350 board -- the HSTX peripheral does not exist on the RP2040."
#endif

// DVI HSTX driver for use with Pimoroni PicoGraphics

namespace pimoroni {

  struct DVHSTXPinout {
    uint8_t clk_p, rgb_p[3];
  };

  typedef uint32_t RGB888;

  // Digital Video using HSTX
  // Screen modes:
  //   Pixel doubled: 640x480, 720x480, 720x400, 720x576, 800x600, 800x480,
  //                  800x450, 960x540, 1024x768 (all 60Hz except noted)
  //   Doubled or quadrupled: 1280x720 (50Hz)
  //
  // Resolutions:
  //   320x180, 640x360 (square pixels, 16:9)
  //   480x270, 400x225 (square pixels, 16:9, less common support)
  //   320x240, 360x240, 360x200, 360x288, 400x300, 512x384 (pixels not square)
  //   400x240 (pixels not square, less common support)
  //   720x480 native, 4:3, no doubling (MODE_PALETTE4 only -- different
  //   from the doubled 360x240->720x480 above)
  //
  // MODE_PALETTE4: 4bpp, 16 colors, 2 pixels per byte. Half the RAM of
  // MODE_PALETTE at the same resolution.
  //
  // Double buffering uses RAM, so e.g. 640x360 uses almost all of it.
  class DVHSTX {
  public:
    static constexpr int PALETTE_SIZE = 256;


    enum Mode {
      MODE_PALETTE = 2,
      MODE_RGB565 = 1,
      MODE_RGB888 = 3,
      MODE_TEXT_MONO = 4,
      MODE_TEXT_RGB111 = 5,
      MODE_PALETTE4 = 6, ///< 4bpp, 2 pixels/byte (high nibble = left,
                         ///< low nibble = right). Same palette as
                         ///< MODE_PALETTE, indices 0-15 only.
    };

    enum TextColour {
      TEXT_BLACK = 0,
      TEXT_RED,
      TEXT_GREEN,
      TEXT_BLUE,
      TEXT_YELLOW,
      TEXT_MAGENTA,
      TEXT_CYAN,
      TEXT_WHITE,

      BG_BLACK = 0,
      BG_RED = TEXT_RED << 3,
      BG_GREEN = TEXT_GREEN << 3,
      BG_BLUE = TEXT_BLUE << 3,
      BG_YELLOW = TEXT_YELLOW << 3,
      BG_MAGENTA = TEXT_MAGENTA << 3,
      BG_CYAN = TEXT_CYAN << 3,
      BG_WHITE = TEXT_WHITE << 3,

      ATTR_NORMAL_INTEN = 0,
      ATTR_LOW_INTEN = 1 << 6,
      ATTR_V_LOW_INTEN = 1 << 7 | ATTR_LOW_INTEN,
    };    

    //--------------------------------------------------
    // Variables
    //--------------------------------------------------
  protected:
    friend void vsync_callback();

    uint16_t display_width = 320;
    uint16_t display_height = 180;
    uint16_t frame_width = 320;
    uint16_t frame_height = 180;
    uint8_t frame_bytes_per_pixel = 2;
    uint8_t h_repeat = 4;
    uint8_t v_repeat = 4;
    Mode mode = MODE_RGB565;

  public:
    DVHSTX();

    //--------------------------------------------------
    // Methods
    //--------------------------------------------------
    public:
      bool get_single_buffered() { return frame_buffer_display && frame_buffer_display == frame_buffer_back; }
      bool get_double_buffered() { return frame_buffer_display && frame_buffer_display != frame_buffer_back; }

      template<class T>
      T *get_back_buffer() { return (T*)(frame_buffer_back); }
      template<class T>
      T *get_front_buffer() { return (T*)(frame_buffer_display); }

      uint16_t get_width() const { return frame_width; }
      uint16_t get_height() const { return frame_height; }

      RGB888* get_palette();

      bool init(uint16_t width, uint16_t height, Mode mode, bool double_buffered, const DVHSTXPinout &pinout);
      void reset();

      // Rebuilds the MODE_PALETTE4 lookup tables from the palette. Runs
      // automatically in init() and setColor(); call it yourself only if
      // you write to get_palette() directly.
      void rebuild_palette4_cache();

      // Wait for vsync and then flip the buffers
      void flip_blocking();

      // Flip immediately without waiting for vsync
      void flip_now();

      void wait_for_vsync();

      // flip_async queues a flip to happen next vsync but returns without blocking.
      // You should call wait_for_flip before doing any more reads or writes, defining sprites, etc.
      void flip_async();
      void wait_for_flip();

      // DMA handlers, should not be called externally
      void gfx_dma_handler();
      void text_dma_handler();

      void set_cursor(int x, int y) { cursor_x = x; cursor_y = y; }
      void cursor_off(void) { cursor_y = -1; }

    private:
      // palette and palette4 lookup tables are file-scope globals in
      // dvhstx.cpp (palette_table, palette4_hi/lo).
      // volatile: swap() writes these pointers on the main thread, the
      // DMA ISR reads them every scanline.
      uint8_t* volatile frame_buffer_display;
      uint8_t* volatile frame_buffer_back;
      uint32_t* font_cache = nullptr;

      void display_setup_clock();

      // DMA scanline filling
      uint ch_num = 0;
      int line_num = -1;

      // dynamically claimed DMA channel ids; ch_num above is the logical
      // ring index, dma_chan_ids[ch_num] is the hardware channel
      uint8_t dma_chan_ids[16];
      bool channels_claimed = false;

    public:
      // diagnostics: read from the sketch, no API stability promised.
      // late_isr_count increments when the ISR finds more than one of our
      // channel IRQs already pending (the reload fell behind the ring).
      // max_pending is the worst simultaneous pending count seen.
      volatile uint32_t late_isr_count = 0;
      volatile uint32_t max_pending = 0;
      uint32_t diag_chan_mask = 0;
    private:

      volatile int v_scanline = 2;
      volatile bool flip_next;

      bool inited = false;

      uint32_t* line_buffers;
      const struct dvi_timing* timing_mode;
      int v_inactive_total;
      int v_total_active_lines;

      uint h_repeat_shift;
      uint v_repeat_shift;
      int line_bytes_per_pixel;

      uint32_t* display_palette = nullptr;

      int cursor_x, cursor_y;
  };
}
