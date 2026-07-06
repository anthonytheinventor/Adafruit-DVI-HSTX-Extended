#pragma once

#include <string.h> // memset, used by DVHSTX4::fillScreen

#include "Adafruit_GFX.h"

#include "drivers/dvhstx/dvhstx.hpp"

#if DOXYGEN
/// Enumerated types in the library. Due to a technical limitations in the
/// documentation generator, these are displayed as members of a class called
/// "enum", but in reality they are defined as top-level enumerations.
struct enum {
  public :
#endif

      /// Available video resolutions
      enum DVHSTXResolution {
        DVHSTX_RESOLUTION_320x180, ///< well supported, aspect ratio 16:9,
                                   ///< actual resolution 1280x720@50Hz
        DVHSTX_RESOLUTION_640x360, ///< well supported, aspect ratio 16:9,
                                   ///< actual resolution 1280x720@50Hz

        DVHSTX_RESOLUTION_480x270, ///< sometimes supported, aspect ratio 16:9,
                                   ///< actual resolution 960x540@60Hz

        DVHSTX_RESOLUTION_400x225, ///< sometimes supported, aspect ratio 16:9,
                                   ///< actual resolution 800x450@60Hz

        DVHSTX_RESOLUTION_320x240, ///< well supported, aspect ratio 4:3, actual
                                   ///< resolution 640x480@60Hz
        DVHSTX_RESOLUTION_640x480, ///< well supported, aspect ratio 4:3, actual
                                   ///< resolution 640x480@60Hz
        DVHSTX_RESOLUTION_360x240, ///< well supported, aspect ratio 3:2, actual
                                   ///< resolution 720x480@60Hz
        DVHSTX_RESOLUTION_720x480, ///< well supported (CEA-861 NTSC
                                   ///< timing), aspect ratio 4:3, native
                                   ///< resolution -- not doubled, unlike
                                   ///< the 720x480 you get from
                                   ///< DVHSTX_RESOLUTION_360x240 above.
                                   ///< Many TVs show this as 4:3 by
                                   ///< default; use the TV's own stretch
                                   ///< setting for 16:9.
        DVHSTX_RESOLUTION_360x200, ///< well supported, aspect ratio 9:5, actual
                                   ///< resolution 720x400@70Hz (very close to
                                   ///< 16:9)
        DVHSTX_RESOLUTION_720x400, ///< well supported, aspect ratio 9:5, actual
                                   ///< resolution 720x400@70Hz (very close to
                                   ///< 16:9)
        DVHSTX_RESOLUTION_360x288, ///< well supported, aspect ratio 5:4, actual
                                   ///< resolution 720x576@60Hz
        DVHSTX_RESOLUTION_400x300, ///< well supported, aspect ratio 4:3, actual
                                   ///< resolution 800x600@60Hz
        DVHSTX_RESOLUTION_512x384, ///< well supported, aspect ratio 4:3, actual
                                   ///< resolution 1024x768@60Hz

        /* sometimes supported, but pixels aren't square on a 16:9 display */
        DVHSTX_RESOLUTION_400x240, ///< well supported, aspect ratio 5:3, actual
                                   ///< resolution 800x480@60Hz
      };

#if DOXYGEN
  /// Foreground, background & attribute definitions for DVHSTXText Cells
  enum TextColor{
      TEXT_BLACK,   ///< Black foreground color
      TEXT_RED,     ///< Red foreground color
      TEXT_GREEN,   ///< Green foreground color
      TEXT_YELLOW,  ///< Yellow foreground color
      TEXT_BLUE,    ///< Blue foreground color
      TEXT_MAGENTA, ///< Magenta foreground color
      TEXT_WHITE,   ///< White foreground color

      BG_BLACK,   ///< Black background color
      BG_RED,     ///< Red background color
      BG_GREEN,   ///< Green background color
      BG_YELLOW,  ///< Yellow background color
      BG_BLUE,    ///< Blue background color
      BG_MAGENTA, ///< Magenta background color
      BG_CYAN,    ///< Cyan background color
      BG_WHITE,   ///< White background color

      ATTR_NORMAL_INTEN, ///< Normal intensity foreground
      ATTR_LOW_INTEN,    ///< Lower intensity foreground
      ATTR_V_LOW_INTEN,  ///< Lowest intensity foreground
  };
};
#endif

using pimoroni::DVHSTXPinout;

// If the board definition provides pre-defined pins for the HSTX connection,
// use them to define a default pinout object.
// This object can be used as the first argument of the DVHSTX constructor.
// Otherwise you must provide the pin nubmers directly as a list of 4 numbers
// in curly brackets such as {12, 14, 16, 18}. These give the location of the
// positive ("P") pins in the order: Clock, Data 0, Data 1, Data 2; check your
// board's schematic for details.
#if defined(PIN_CKP)
#define DVHSTX_PINOUT_DEFAULT                                                  \
  { PIN_CKP, PIN_D0P, PIN_D1P, PIN_D2P }
#else
#if !defined(DVHSTX_NO_DEFAULT_PINOUT_WARNING)
#pragma GCC warning                                                            \
    "DVHSTX_PINOUT_DEFAULT is a default pinout. If you get no video display, use the correct pinout for your board."
#endif
#define DVHSTX_PINOUT_DEFAULT                                                  \
  { 12, 14, 16, 18 }
#endif

// Other common HSTX pin configurations
#define ADAFRUIT_HSTXDVIBELL_CFG                                               \
  { 14, 12, 18, 16 }
#define ADAFRUIT_METRO_RP2350_CFG                                              \
  { 14, 18, 16, 12 }
#define ADAFRUIT_FEATHER_RP2350_CFG                                            \
  { 14, 18, 16, 12 }
#define ADAFRUIT_FRUIT_JAM_CFG                                                 \
  { 13, 15, 17, 19 }

int16_t dvhstx_width(DVHSTXResolution r);
int16_t dvhstx_height(DVHSTXResolution r);

/// A 16-bit canvas displaying to a DVI monitor
class DVHSTX16 : public GFXcanvas16 {
public:
  /**************************************************************************/
  /*!
     @brief    Instatiate a DVHSTX 16-bit canvas context for graphics
     @param    pinout Details of the HSTX pinout
     @param    res   Display resolution
     @param    double_buffered Whether to allocate two buffers
  */
  /**************************************************************************/
  DVHSTX16(DVHSTXPinout pinout, DVHSTXResolution res,
           bool double_buffered = false)
      : GFXcanvas16(dvhstx_width(res), dvhstx_height(res), false),
        pinout(pinout), res{res}, double_buffered{double_buffered} {}
  ~DVHSTX16() { end(); }

  /**************************************************************************/
  /*!
     @brief    Start the display
     @return   true if successful, false in case of error
  */
  /**************************************************************************/
  bool begin() {
    bool result =
        hstx.init(dvhstx_width(res), dvhstx_height(res),
                  pimoroni::DVHSTX::MODE_RGB565, double_buffered, pinout);
    if (!result)
      return false;
    buffer = hstx.get_back_buffer<uint16_t>();
    fillScreen(0);
    return true;
  }
  /**************************************************************************/
  /*!
     @brief    Stop the display
  */
  /**************************************************************************/
  void end() { hstx.reset(); }

  /**********************************************************************/
  /*!
    @brief    If double-buffered, wait for retrace and swap buffers. Otherwise,
    do nothing (returns immediately)
    @param copy_framebuffer if true, copy the new screen to the new back buffer.
    Otherwise, the content is undefined.
  */
  /**********************************************************************/
  void swap(bool copy_framebuffer = false);

  /**********************************************************************/
  /*!
    @brief    Convert 24-bit RGB value to a framebuffer value
    @param r The input red value, 0 to 255
    @param g The input red value, 0 to 255
    @param b The input red value, 0 to 255
    @return  The corresponding 16-bit pixel value
  */
  /**********************************************************************/
  uint16_t color565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  }

private:
  DVHSTXPinout pinout;
  DVHSTXResolution res;
  mutable pimoroni::DVHSTX hstx;
  bool double_buffered;
};

/// An 8-bit canvas displaying to a DVI monitor
class DVHSTX8 : public GFXcanvas8 {
public:
  /**************************************************************************/
  /*!
     @brief    Instatiate a DVHSTX 8-bit canvas context for graphics
     @param    pinout Details of the HSTX pinout
     @param    res   Display resolution
     @param    double_buffered Whether to allocate two buffers
  */
  /**************************************************************************/
  DVHSTX8(DVHSTXPinout pinout, DVHSTXResolution res,
          bool double_buffered = false)
      : GFXcanvas8(dvhstx_width(res), dvhstx_height(res), false),
        pinout(pinout), res{res}, double_buffered{double_buffered} {}
  ~DVHSTX8() { end(); }

  /**************************************************************************/
  /*!
     @brief    Start the display
     @return   true if successful, false in case of error
  */
  /**************************************************************************/
  bool begin() {
    bool result =
        hstx.init(dvhstx_width(res), dvhstx_height(res),
                  pimoroni::DVHSTX::MODE_PALETTE, double_buffered, pinout);
    if (!result)
      return false;
    for (int i = 0; i < 255; i++) {
      uint8_t r = (i >> 6) * 255 / 3;
      uint8_t g = ((i >> 2) & 7) * 255 / 7;
      uint8_t b = (i & 3) * 255 / 3;
      setColor(i, r, g, b);
    }
    buffer = hstx.get_back_buffer<uint8_t>();
    fillScreen(0);
    return true;
  }
  /**************************************************************************/
  /*!
     @brief    Stop the display
  */
  /**************************************************************************/
  void end() { hstx.reset(); }

  /**************************************************************************/
  /*!
     @brief    Set the given palette entry to a RGB888 color
     @param idx The palette entry number, from 0 to 255
     @param r The input red value, 0 to 255
     @param g The input red value, 0 to 255
     @param b The input red value, 0 to 255
  */
  /**************************************************************************/
  void setColor(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
    hstx.get_palette()[idx] = (r << 16) | (g << 8) | b;
  }
  /**************************************************************************/
  /*!
     @brief    Set the given palette entry to a RGB888 color
     @param idx The palette entry number, from 0 to 255
     @param rgb The 24-bit RGB value in the form 0x00RRGGBB
  */
  /**************************************************************************/
  void setColor(uint8_t idx, uint32_t rgb) { hstx.get_palette()[idx] = rgb; }

  /**********************************************************************/
  /*!
    @brief    If double-buffered, wait for retrace and swap buffers. Otherwise,
    do nothing (returns immediately)
    @param copy_framebuffer if true, copy the new screen to the new back buffer.
    Otherwise, the content is undefined.
  */
  /**********************************************************************/
  void swap(bool copy_framebuffer = false);

private:
  DVHSTXPinout pinout;
  DVHSTXResolution res;
  mutable pimoroni::DVHSTX hstx;
  bool double_buffered;
};

/// 4-bit (16-color) canvas for DVI output.
///
/// Not built on GFXcanvas8/16 like the other classes here -- Adafruit_GFX
/// doesn't have a 4-bit canvas, so pixel read/write is done directly.
/// Two pixels per byte: high nibble = left, low nibble = right. Same
/// 256-entry palette format as DVHSTX8, but only 0-15 are usable.
class DVHSTX4 : public Adafruit_GFX {
public:
  /**************************************************************************/
  /*!
     @brief    Instatiate a DVHSTX 4-bit (16-color) canvas context for graphics
     @param    pinout Details of the HSTX pinout
     @param    res   Display resolution
     @param    double_buffered Whether to allocate two buffers
  */
  /**************************************************************************/
  DVHSTX4(DVHSTXPinout pinout, DVHSTXResolution res,
          bool double_buffered = false)
      : Adafruit_GFX(dvhstx_width(res), dvhstx_height(res)),
        pinout(pinout), res{res}, double_buffered{double_buffered} {}
  ~DVHSTX4() { end(); }

  /**************************************************************************/
  /*!
     @brief    Start the display
     @return   true if successful, false in case of error
  */
  /**************************************************************************/
  bool begin() {
    bool result =
        hstx.init(dvhstx_width(res), dvhstx_height(res),
                  pimoroni::DVHSTX::MODE_PALETTE4, double_buffered, pinout);
    if (!result)
      return false;
    // default palette: black at 0, white at 15, ramp between. writes it
    // directly and rebuilds the cache once instead of going through
    // setColor() 16 times.
    for (int i = 0; i < 16; i++) {
      uint8_t v = i * 255 / 15;
      hstx.get_palette()[i] = ((uint32_t)v << 16) | ((uint32_t)v << 8) | v;
    }
    hstx.rebuild_palette4_cache();
    buffer = hstx.get_back_buffer<uint8_t>();
    fillScreen(0);
    return true;
  }
  /**************************************************************************/
  /*!
     @brief    Stop the display
  */
  /**************************************************************************/
  void end() { hstx.reset(); }

  /**************************************************************************/
  /*!
     @brief    Set the given palette entry to a RGB888 color
     @param idx The palette entry number, from 0 to 15 (values above 15
     are masked off, since only the low nibble is ever used as an index)
     @param r The input red value, 0 to 255
     @param g The input green value, 0 to 255
     @param b The input blue value, 0 to 255
  */
  /**************************************************************************/
  void setColor(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
    hstx.get_palette()[idx & 0x0F] =
        ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    hstx.rebuild_palette4_cache();
  }
  /**************************************************************************/
  /*!
     @brief    Set the given palette entry to a RGB888 color
     @param idx The palette entry number, from 0 to 15
     @param rgb The 24-bit RGB value in the form 0x00RRGGBB
  */
  /**************************************************************************/
  void setColor(uint8_t idx, uint32_t rgb) {
    hstx.get_palette()[idx & 0x0F] = rgb;
    hstx.rebuild_palette4_cache();
  }

  /**********************************************************************/
  /*!
    @brief    If double-buffered, wait for retrace and swap buffers. Otherwise,
    do nothing (returns immediately)
    @param copy_framebuffer if true, copy the new screen to the new back buffer.
    Otherwise, the content is undefined.
  */
  /**********************************************************************/
  void swap(bool copy_framebuffer = false);

  /**************************************************************************/
  /*!
     @brief    Set one pixel's 4-bit palette index (0-15; values above 15
     are masked off)
     @param x  Horizontal position
     @param y  Vertical position
     @param color  Palette index, 0-15
  */
  /**************************************************************************/
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (!buffer)
      return;
    if ((x < 0) || (x >= _width) || (y < 0) || (y >= _height))
      return;

    // same rotation transform Adafruit_GFX canvases use
    int16_t t;
    switch (rotation) {
    case 1:
      t = x;
      x = WIDTH - 1 - y;
      y = t;
      break;
    case 2:
      x = WIDTH - 1 - x;
      y = HEIGHT - 1 - y;
      break;
    case 3:
      t = x;
      x = y;
      y = HEIGHT - 1 - t;
      break;
    }

    uint32_t byteIdx = (uint32_t)(x >> 1) + (uint32_t)y * ((WIDTH + 1) >> 1);
    uint8_t nib = color & 0x0F;
    if (x & 1) {
      buffer[byteIdx] = (buffer[byteIdx] & 0xF0) | nib;
    } else {
      buffer[byteIdx] = (buffer[byteIdx] & 0x0F) | (nib << 4);
    }
  }

  /**************************************************************************/
  /*!
     @brief    Read back one pixel's 4-bit palette index
     @param x  Horizontal position
     @param y  Vertical position
     @return   Palette index, 0-15, or 0 if out of bounds / not started
  */
  /**************************************************************************/
  uint8_t getPixel(int16_t x, int16_t y) const {
    if (!buffer)
      return 0;
    if ((x < 0) || (x >= _width) || (y < 0) || (y >= _height))
      return 0;

    int16_t t;
    switch (rotation) {
    case 1:
      t = x;
      x = WIDTH - 1 - y;
      y = t;
      break;
    case 2:
      x = WIDTH - 1 - x;
      y = HEIGHT - 1 - y;
      break;
    case 3:
      t = x;
      x = y;
      y = HEIGHT - 1 - t;
      break;
    }

    uint32_t byteIdx = (uint32_t)(x >> 1) + (uint32_t)y * ((WIDTH + 1) >> 1);
    uint8_t byteVal = buffer[byteIdx];
    return (x & 1) ? (byteVal & 0x0F) : (byteVal >> 4);
  }

  /**************************************************************************/
  /*!
     @brief    Fill the whole screen with one palette index
     @param color  Palette index, 0-15
  */
  /**************************************************************************/
  void fillScreen(uint16_t color) override {
    if (!buffer)
      return;
    uint8_t nib = color & 0x0F;
    uint8_t fillByte = (nib << 4) | nib;
    uint32_t nbytes = ((uint32_t)WIDTH * HEIGHT + 1) / 2;
    memset(buffer, fillByte, nbytes);
  }

  /**************************************************************************/
  /*!
     @brief    Fill a rectangle with one palette index (0-15; values above
     15 are masked off). Fast-path override -- memsets whole bytes where
     possible instead of going pixel-by-pixel.
     @param x  Left edge
     @param y  Top edge
     @param w  Width
     @param h  Height
     @param color  Palette index, 0-15
  */
  /**************************************************************************/
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                uint16_t color) override {
    if (!buffer)
      return;
    if (rotation != 0) {
      // rotated fill isn't implemented, fall back to the generic path
      Adafruit_GFX::fillRect(x, y, w, h, color);
      return;
    }

    // clip to screen bounds, same as drawPixel() does per-pixel
    if (x < 0) {
      w += x;
      x = 0;
    }
    if (y < 0) {
      h += y;
      y = 0;
    }
    if (x + w > _width)
      w = _width - x;
    if (y + h > _height)
      h = _height - y;
    if (w <= 0 || h <= 0)
      return;

    uint8_t nib = color & 0x0F;
    uint8_t fillByte = (nib << 4) | nib;
    uint32_t rowStrideBytes = (WIDTH + 1) >> 1;

    for (int16_t row = y; row < y + h; row++) {
      int16_t xEnd = x + w; // exclusive
      int16_t col = x;

      // leading partial byte -- only the low nibble is ours
      if (col & 1) {
        uint32_t byteIdx = (col >> 1) + (uint32_t)row * rowStrideBytes;
        buffer[byteIdx] = (buffer[byteIdx] & 0xF0) | nib;
        col++;
      }

      // fully-covered bytes, bulk memset
      int16_t fullBytes = (xEnd - col) >> 1;
      if (fullBytes > 0) {
        uint32_t byteIdx = (col >> 1) + (uint32_t)row * rowStrideBytes;
        memset(&buffer[byteIdx], fillByte, fullBytes);
        col += fullBytes * 2;
      }

      // trailing partial byte -- only the high nibble is ours
      if (col < xEnd) {
        uint32_t byteIdx = (col >> 1) + (uint32_t)row * rowStrideBytes;
        buffer[byteIdx] = (buffer[byteIdx] & 0x0F) | (nib << 4);
      }
    }
  }

private:
  DVHSTXPinout pinout;
  DVHSTXResolution res;
  mutable pimoroni::DVHSTX hstx;
  bool double_buffered;
  uint8_t *buffer = nullptr;
};

using TextColor = pimoroni::DVHSTX::TextColour;

/// A text-mode canvas displaying to a DVI monitor
///
/// The text mode display is always 91x30 characters at a resolution of 1280x720
/// pixels.
class DVHSTXText : public GFXcanvas16 {
public:
  /// Each element of the canvas is a Cell, which comprises a character and an
  /// attribute.
  struct Cell {
    uint16_t value; ///< A value where the low 8 bits are the character and the
                    ///< high 8 bits are the attribute
    /**************************************************************************/
    /*!
       @brief    Create a cell object from an ASCII character and an attribute
       @param    c ASCII character value. Glyphs from 32 to 126 inclusive are
       available
       @param    attr Attribute value. Can be the logical OR of a text color, a
       background color, and an intensity flag
    */
    /**************************************************************************/
    Cell(uint8_t c, uint8_t attr = TextColor::TEXT_WHITE)
        : value(c | (attr << 8)) {}
  };
  /**************************************************************************/
  /*!
     @brief    Instatiate a DVHSTX 8-bit canvas context for graphics
     @param    pinout Details of the HSTX pinout
     @param    double_buffered Whether to allocate two buffers
  */
  /**************************************************************************/
  DVHSTXText(DVHSTXPinout pinout, bool double_buffered = false)
      : GFXcanvas16(91, 30, false), double_buffered{double_buffered},
        pinout(pinout), res{}, attr{TextColor::TEXT_WHITE} {}
  ~DVHSTXText() { end(); }

  /**************************************************************************/
  /*!
     @brief    Check if the display is running
     @return   true if the display is running
  */
  /**************************************************************************/
  operator bool() const { return hstx.get_back_buffer<uint16_t>(); }

  /**************************************************************************/
  /*!
     @brief    Start the display
     @return   true if successful, false in case of error
  */
  /**************************************************************************/
  bool begin() {
    bool result = hstx.init(91, 30, pimoroni::DVHSTX::MODE_TEXT_RGB111,
                            double_buffered, pinout);
    if (!result)
      return false;
    buffer = hstx.get_back_buffer<uint16_t>();
    return true;
  }
  /**************************************************************************/
  /*!
     @brief    Stop the display
  */
  /**************************************************************************/
  void end() { hstx.reset(); }

  /**************************************************************************/
  /*!
     @brief    Clear the display. All cells are set to character 32 (space) with
     the current color
  */
  /**************************************************************************/
  void clear();

  /**************************************************************************/
  /*!
     @brief    Set the color used by write()
     @param    a Attribute value. Can be the logical OR of a text color, a
     background color, and an intensity flag
  */
  /**************************************************************************/
  void setColor(uint8_t a) { attr = a; }
  /**************************************************************************/
  /*!
     @brief    Set the color used by write()
     @param    fg A foreground color
     @param    bg A background color
     @param    inten An intensity flag
  */
  /**************************************************************************/
  void setColor(TextColor fg, TextColor bg,
                TextColor inten = TextColor::ATTR_NORMAL_INTEN) {
    attr = fg | bg | inten;
  }

  /**************************************************************************/
  /*!
     @brief    Hide the block cursor
  */
  /**************************************************************************/
  void hideCursor() {
    cursor_visible = false;
    hstx.cursor_off();
  }

  /**************************************************************************/
  /*!
     @brief    Show the block cursor
  */
  /**************************************************************************/
  void showCursor() {
    cursor_visible = true;
    sync_cursor_with_hstx();
  }

  /**************************************************************************/
  /*!
     @brief    Move the position for write(), and the block cursor (if shown)
     @param x  The screen location horizontally, from 0 to 91 inclusive
     @param y  The screen location vertically, from 0 to 29 inclusive

     Note that when the cursor's X position is (zero-based) 91 it is shown on
     (zero-based) column 90 but the next character written will be wrapped and
     shown at the beginning of the next line.
  */
  /**************************************************************************/
  void setCursor(int x, int y) {
    if (x < 0)
      x = 0;
    if (x > _width)
      x = _width;
    if (y < 0)
      y = 0;
    if (y >= _height)
      y = _height - 1;
    cursor_x = x;
    cursor_y = y;
    sync_cursor_with_hstx();
  }

  /**************************************************************************/
  /*!
     @brief    Write a character to the display

     @param c  The character to write

     Because this class is a subclass of Print, you can also use methods like
     print, println, and printf to write text.

     Text is written with the current attribute, and the screen automatically
     wraps and scrolls when needed.

     This function fails (returns 0) if the display was not successfully started
     with begin().

     @return 1 if the character is written, 0 if it was not
  */
  /**************************************************************************/
  size_t write(uint8_t c);

  /**************************************************************************/
  /*!
     @brief    Get the current cursor X position, from 0 to 91
     @return   The column position of the cursor
  */
  /**************************************************************************/
  int getCursorX() const { return cursor_x; }
  /**************************************************************************/
  /*!
     @brief    Get the current cursor Y position, from 0 to 29
     @return   The row position of the cursor
  */
  /**************************************************************************/
  int getCursorY() const { return cursor_y; }

  /**********************************************************************/
  /*!
    @brief    If double-buffered, wait for retrace and swap buffers. Otherwise,
    do nothing (returns immediately)
    @param copy_framebuffer if true, copy the new screen to the new back buffer.
    Otherwise, the content is undefined.
  */
  /**********************************************************************/
  void swap(bool copy_framebuffer = false);

private:
  DVHSTXPinout pinout;
  DVHSTXResolution res;
  mutable pimoroni::DVHSTX hstx;
  bool double_buffered;
  bool cursor_visible = false;
  uint8_t attr;
  uint8_t cursor_x = 0, cursor_y = 0;

  void sync_cursor_with_hstx() {
    if (cursor_visible) {
      hstx.set_cursor(cursor_x == _width ? _width - 1 : cursor_x, cursor_y);
    }
  }
};
