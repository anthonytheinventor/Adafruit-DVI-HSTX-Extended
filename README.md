# Adafruit DVI HSTX Extended

A fork of Adafruit's `Adafruit_dvhstx` library for RP2350 boards with the HSTX peripheral, including the Adafruit Feather RP2350, Metro RP2350, and Fruit Jam.

This fork adds:

- `DVHSTX4`: 4-bit indexed color with 16 palette entries
- native 720x480 timing for the standard scanline driver
- `DVHSTX8Turbo`: single-buffered RGB332 output with DMA-only scanout
- `blitRows()`: DMA-assisted row copies into the turbo framebuffer
- optional HDMI AVI InfoFrame signaling for 720x480 anamorphic output
- deeper DMA buffering and faster scanline conversion paths

The original driver was written by Jeff Epler and Adafruit and is based on Pimoroni's `dvhstx` driver.

> **Clock requirement:** Including `<Adafruit_dvhstx_extended.h>` configures the RP2350 system clock to 240 MHz. Leave the Arduino IDE CPU-speed setting at 150 MHz. This is an overclock and may not be suitable for every board or application.

## Contents

- [Choosing a display class](#choosing-a-display-class)
- [Turbo mode](#turbo-mode)
- [Supported resolutions](#supported-resolutions)
- [Installation](#installation)
- [Quick start](#quick-start)
- [Direct framebuffer access](#direct-framebuffer-access)
- [Examples](#examples)
- [Diagnostics and implementation notes](#diagnostics-and-implementation-notes)

## Choosing a display class

| Class | Pixel format | Colors | Buffering | Typical use |
|---|---|---:|---|---|
| `DVHSTX16` | RGB565 | 65,536 | single or double | highest color quality when RAM permits |
| `DVHSTX8` | 8-bit indexed | 256 | single or double | palette graphics |
| `DVHSTX4` | 4-bit indexed | 16 | single or double | low-RAM dashboards and interfaces |
| `DVHSTX8Turbo` | RGB332 | 256 direct colors | single only | stable scanout under heavy interrupt load |
| `DVHSTXText` | character cells with RGB111 attributes | 8 foreground and 8 background colors | single or double | text displays |

`DVHSTX16`, `DVHSTX8`, and `DVHSTX4` use the standard scanline driver. An interrupt handler prepares scanlines and feeds them to HSTX through a ring of DMA buffers.

`DVHSTX8Turbo` uses a different layout. It stores HSTX commands and RGB332 pixel data in one frame-sized buffer and streams that buffer continuously with DMA. It does not run a per-scanline or per-frame interrupt handler.

## Turbo mode

### How it works

`DVHSTX8Turbo` builds the complete HSTX command stream during `begin()`. Each active row contains a short command header followed by the row's RGB332 pixel bytes. Two DMA channels loop over the completed frame:

1. the scanout channel transfers the frame to the HSTX FIFO;
2. the restart channel resets the scanout channel's read address at the end of the frame.

This design removes scanline preparation from the CPU. Interrupt latency from USB, storage, networking, or application code does not delay scanout command generation.

### Limitations

- RGB332 only
- single-buffered only
- 720x480 and 640x480 only
- framebuffer rows are strided because HSTX command words are stored between pixel rows

At 720x480, a second full RGB332 framebuffer does not fit comfortably in internal SRAM. For staged rendering, compose into a smaller SRAM buffer or PSRAM and copy completed rows with `blitRows()`.

### `blitRows()`

```cpp
bool blitRows(int y, const uint8_t *src, int nRows, bool block = true);
```

`src` contains tightly packed rows of `display.width()` bytes each. The function copies them into the strided turbo framebuffer without overwriting the HSTX command words between rows.

When the source is 4-byte aligned and DMA channels are available, the driver uses a scatter-gather DMA transfer. Otherwise it uses `memcpy()` row by row.

For asynchronous copies, pass `false` for `block`, then use:

```cpp
if (!display.blitBusy()) {
  // The source buffer can be reused.
}

display.blitWait();
```

Do not modify or free the source buffer while an asynchronous copy is active.

### Optional HDMI signaling

Turbo mode defaults to DVI-compatible output:

```cpp
DVHSTX8Turbo display(DVHSTX_PINOUT_DEFAULT,
                     DVHSTX_RESOLUTION_720x480);
```

Pass `true` as the third argument to enable experimental HDMI signaling:

```cpp
DVHSTX8Turbo display(DVHSTX_PINOUT_DEFAULT,
                     DVHSTX_RESOLUTION_720x480,
                     true);
```

The HDMI mode adds video preambles, guard bands, and one AVI InfoFrame per frame. At 720x480, the InfoFrame declares:

- VIC 3
- 16:9 picture aspect
- active format equal to the coded frame
- full-range RGB
- underscan preference
- IT/graphics content

Display behavior varies. Some televisions honor all fields, some honor only the aspect ratio, and some use their per-input picture settings instead. Pure DVI monitors may not accept HDMI data-island symbols, so HDMI signaling remains opt-in.

HDMI mode increases the active-row command header from 28 to 44 bytes and adds approximately 8 KiB to the frame allocation.

## Supported resolutions

### Standard scanline driver

Framebuffer sizes below are for one buffer. Double buffering doubles the framebuffer allocation. The RP2350 has 520 KiB of internal SRAM shared by the framebuffer, heap, stacks, DMA structures, and application data.

| Resolution | Output timing | Nominal aspect | 4 bpp | 8 bpp | 16 bpp |
|---|---|---:|---:|---:|---:|
| 320x180 | 1280x720 at 50 Hz, 4x repetition | 16:9 | 28 KiB | 56 KiB | 113 KiB |
| 640x360 | 1280x720 at 50 Hz, 2x repetition | 16:9 | 113 KiB | 225 KiB | 450 KiB |
| 480x270 | 1920x1080 at 30 Hz, 4x repetition | 16:9 | 63 KiB | 127 KiB | 253 KiB |
| 400x225 | 800x450 at 60 Hz, 2x repetition | 16:9 | 44 KiB | 88 KiB | 176 KiB |
| 320x240 | 640x480 at 60 Hz, 2x repetition | 4:3 | 38 KiB | 75 KiB | 150 KiB |
| 640x480 | 640x480 at 60 Hz | 4:3 | 150 KiB | 300 KiB | does not fit |
| 360x240 | 720x480 at 60 Hz, 2x repetition | 3:2 | 42 KiB | 84 KiB | 169 KiB |
| 720x480 | 720x480 at 60 Hz | 4:3 over DVI* | 169 KiB | 338 KiB | does not fit |
| 360x200 | 720x400 at 70 Hz, 2x repetition | 9:5 | 35 KiB | 70 KiB | 141 KiB |
| 720x400 | 720x400 at 70 Hz | 9:5 | 141 KiB | 281 KiB | does not fit |
| 360x288 | 720x576 at 50 Hz, 2x repetition | 5:4 | 51 KiB | 101 KiB | 203 KiB |
| 400x300 | 800x600 at 60 Hz, 2x repetition | 4:3 | 59 KiB | 117 KiB | 234 KiB |
| 512x384 | 1024x768 at 60 Hz, 2x repetition | 4:3 | 96 KiB | 192 KiB | 384 KiB |
| 400x240 | 800x480 at 60 Hz, 2x repetition | 5:3 | 47 KiB | 94 KiB | 188 KiB |

Resolution constants use the form `DVHSTX_RESOLUTION_<width>x<height>`.

\* The 720x480 timing can represent anamorphic 16:9 video, but ordinary DVI output does not carry the aspect-ratio metadata. Use the optional turbo HDMI mode or the display's aspect control when widescreen presentation is required.

### Turbo driver

Sizes include interleaved HSTX command words.

| Resolution | Output timing | DVI allocation | HDMI allocation |
|---|---|---:|---:|
| 720x480 | 720x480 at 60 Hz | about 352 KiB | about 360 KiB |
| 640x480 | 640x480 at 60 Hz | about 314 KiB | about 322 KiB |

## Installation

1. Download the release ZIP from the project's Releases page.
2. In Arduino IDE, choose **Sketch → Include Library → Add .ZIP Library...**.
3. Select the downloaded ZIP.
4. Restart Arduino IDE if the examples do not appear immediately.

The fork can be installed beside Adafruit's original library because it uses a different public header:

```cpp
#include <Adafruit_dvhstx_extended.h>
```

## Quick start

The following example creates a double-buffered, 16-color, native 720x480 display:

```cpp
#include <Adafruit_dvhstx_extended.h>

DVHSTX4 display(DVHSTX_PINOUT_DEFAULT,
                DVHSTX_RESOLUTION_720x480,
                true);

void setup() {
  if (!display.begin()) {
    for (;;) {
      tight_loop_contents();
    }
  }

  display.setColor(0, 0x000000);
  display.setColor(1, 0xFF0000);

  display.fillScreen(0);
  display.fillRect(20, 20, 200, 100, 1);
  display.swap();
}

void loop() {}
```

Colors passed to `DVHSTX4` drawing functions are palette indices from 0 through 15.

For boards that define `PIN_CKP`, `PIN_D0P`, `PIN_D1P`, and `PIN_D2P`, use `DVHSTX_PINOUT_DEFAULT`. Otherwise provide the positive pin of each differential pair:

```cpp
DVHSTXPinout pinout = {clock_positive, data0_positive,
                       data1_positive, data2_positive};
```

## Direct framebuffer access

The standard canvas classes use contiguous framebuffers. Turbo rows are not contiguous because command words are stored between rows.

Use these methods instead of hard-coding a stride:

```cpp
uint8_t *row = display.rowAddr(y);
size_t stride = display.rowStride();
```

A pixel can then be addressed as:

```cpp
uint8_t *base = display.rowAddr(0);
base[(size_t)y * display.rowStride() + x] = color;
```

Do not write into the gap between the end of one row's pixels and the start of the next row.

## Examples

- `00simpletest`: RGB565 graphics
- `01palettetest`: 8-bit indexed color
- `02texttest`: text mode
- `03_4bpp_test`: 4-bit indexed color
- `04_720x480_test`: native 720x480 with `DVHSTX4`
- `05_turbo_720x480_test`: turbo scanout, RGB332 drawing, vertical-blank synchronization, and `blitRows()`

## Diagnostics and implementation notes

### Standard-driver underrun counters

The standard scanline driver records late interrupt service events:

```cpp
auto &driver = display.driver();
uint32_t late = driver.late_isr_count;
uint32_t peak = driver.max_pending;
```

These counters help identify applications that block interrupts long enough to exhaust the scanline ring.

### DMA allocation

DMA channels are claimed dynamically and released by `end()`. Turbo scanout also raises DMA and bus priority while active, then restores normal arbitration during `reset()`.

### Allocation failures

`begin()` returns `false` if a framebuffer, line buffer, or required DMA resource cannot be allocated. Check its return value before drawing.
