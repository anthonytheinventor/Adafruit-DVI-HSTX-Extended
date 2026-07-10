# Adafruit DVI HSTX Extended <!-- omit in toc -->

My ([anthonytheinventor](https://github.com/anthonytheinventor)) fork of Adafruit's `Adafruit_dvhstx` library — an Adafruit GFX compatible DVI driver for RP2 chips with HSTX (Adafruit Metro RP2350, Adafruit Fruit Jam, etc). It adds a 4bpp indexed-color mode, a native 720x480 resolution, and some DMA-path performance work. All credit for the original driver goes to Jeff Epler / Adafruit and the Pimoroni `dvhstx` driver underneath it.

**Important note on overclocking:** This library overclocks your RP2 chip to 240MHz just by including `<Adafruit_dvhstx_extended.h>`, separate from the Tools menu setting (which must stay at 150MHz). Like any overclock, there's some risk to component lifespan that's hard to quantify. Proceed at your own discretion.

- [Supported modes and resolutions](#supported-modes-and-resolutions)
- [Why this fork exists](#why-this-fork-exists)
- [New features](#new-features)
- [Performance improvements](#performance-improvements)
- [Hardening](#hardening)
- [Installation](#installation)
- [Quick start](#quick-start)
- [Documentation](#documentation)

## Supported modes and resolutions

Rows marked **⭐ fork** are additions of this fork and don't exist in Adafruit's original library.

### Display modes

| Class | Mode | Colors | RAM per pixel | Buffering | |
|---|---|---|---|---|---|
| `DVHSTX16` | RGB565 | 65,536 direct | 2 bytes | single or double | |
| `DVHSTX8` | 8bpp indexed | 256-entry RGB888 palette | 1 byte | single or double | |
| `DVHSTX4` | 4bpp indexed | 16-entry RGB888 palette | 1/2 byte | single or double | **⭐ fork** |
| `DVHSTX8Turbo` | 8bpp RGB332, zero-ISR | 256 direct (rrrgggbb) | 1 byte | single only | **⭐ fork** |
| `DVHSTXText` | text, RGB111 attributes | 8 fg / 8 bg colors | 2 bytes per cell | single or double | |

`DVHSTX16`, `DVHSTX8`, and `DVHSTX4` accept any resolution in the first table below, RAM permitting. `DVHSTX8Turbo` supports exactly the two resolutions in the second table. `DVHSTXText` is fixed at 91x30 characters on a 1280x720 signal.

### Resolutions (`DVHSTX16` / `DVHSTX8` / `DVHSTX4`)

Framebuffer sizes are for a single buffer; double buffering doubles them. The RP2350 has 520KB of SRAM and the heap shares it with your sketch, so treat anything within ~40KB of the ceiling as unlikely to fit.

| Resolution | Actual signal | Aspect | 4bpp | 8bpp | 16bpp | |
|---|---|---|---|---|---|---|
| `320x180` | 1280x720 @50Hz (4x4) | 16:9 | 28KB | 56KB | 113KB | |
| `640x360` | 1280x720 @50Hz (2x2) | 16:9 | 113KB | 225KB | 450KB | |
| `480x270` | 1920x1080 @30Hz (4x4) | 16:9 | 63KB | 127KB | 253KB | |
| `400x225` | 800x450 @60Hz (2x2) | 16:9 | 44KB | 88KB | 176KB | |
| `320x240` | 640x480 @60Hz (2x2) | 4:3 | 38KB | 75KB | 150KB | |
| `640x480` | 640x480 @60Hz (native) | 4:3 | 150KB | 300KB | won't fit | |
| `360x240` | 720x480 @60Hz (2x2) | 3:2 | 42KB | 84KB | 169KB | |
| `720x480` | 720x480 @60Hz (native, CEA-861) | 4:3* | 169KB | 338KB | won't fit | **⭐ fork** |
| `360x200` | 720x400 @70Hz (2x2) | 9:5 | 35KB | 70KB | 141KB | |
| `720x400` | 720x400 @70Hz (native) | 9:5 | 141KB | 281KB | won't fit | |
| `360x288` | 720x576 @50Hz (2x2) | 5:4 | 51KB | 101KB | 203KB | |
| `400x300` | 800x600 @60Hz (2x2) | 4:3 | 59KB | 117KB | 234KB | |
| `512x384` | 1024x768 @60Hz (2x2) | 4:3 | 96KB | 192KB | 384KB | |
| `400x240` | 800x480 @60Hz (2x2) | 5:3 | 47KB | 94KB | 188KB | |

Enum names are `DVHSTX_RESOLUTION_<WxH>`. "(2x2)"/"(4x4)" is the pixel repetition applied in both axes to produce the signal. *720x480 is a 16:9 anamorphic TV mode, but DVI can't signal that, so most displays show it 4:3 — use the display's stretch setting.

### Turbo resolutions (`DVHSTX8Turbo`)

Zero-ISR scanout: HSTX command words are unrolled into the frame buffer and streamed by a free-running DMA pair — no per-scanline CPU work, so display underruns from blocked interrupts are structurally impossible. Sizes include the interleaved command words. Single-buffered only.

| Resolution | Actual signal | Frame buffer | |
|---|---|---|---|
| `720x480` (default) | 720x480 @60Hz (native, CEA-861) | 352KB | **⭐ fork** |
| `640x480` | 640x480 @60Hz (native, VESA) | 314KB | **⭐ fork** |

640x480 is the mandatory base mode every DVI/HDMI display must support — the fallback for picky monitors.

Turbo mode also provides `blitRows()` — a scatter-gather DMA copy of contiguous rows into the strided frame (sync or async), for presenting content composed off-screen in SRAM or PSRAM without per-pixel drawing into the live buffer.

## Why this fork exists

I wanted higher resolution for data displays and dashboards — enough pixels to actually show something useful — without giving up color or double buffering. Double buffering matters here because a dashboard that redraws constantly will flicker and tear without it. The problem is the RP2350 only has 520KB of RAM, and a double-buffered framebuffer at the original library's 8bpp color mode gets expensive fast — 720x480 at 8bpp double-buffered doesn't fit at all. Dropping to 4bpp (16 colors) cuts the framebuffer cost in half, which is what makes a double-buffered 720x480 display possible in the first place.

## New features

Stuff that doesn't exist in the original library:

- **`MODE_PALETTE4`**: a new 4bpp indexed-color mode — 16 colors, 2 pixels per byte, half the RAM of the original 8bpp palette mode at the same resolution.
- **`DVHSTX4`**: the Arduino-facing canvas class for that mode, with the same API as `DVHSTX8`/`DVHSTX16` (`setColor()`, `drawPixel()`/`getPixel()`, double-buffered `swap()`).
- **Native 720x480** (`DVHSTX_RESOLUTION_720x480`), using real CEA-861 NTSC timing pulled from a display's EDID and checked on hardware. Full resolution, not pixel-doubled like the original library's `DVHSTX_RESOLUTION_360x240` — and it only fits in RAM at 4bpp.
- **Palette lookup caching**: `rebuild_palette4_cache()` precomputes two 256-entry lookup tables so the DMA path is a straight lookup per pixel, no bit-shifting. Runs automatically on `init()` and `DVHSTX4::setColor()`.
- **Two new examples**: `03_4bpp_test` and `04_720x480_test`.
- **`DVHSTX8Turbo` (1.2.0, 640x480 added in 1.3.0)**: a zero-ISR 8bpp RGB332 mode at 720x480 or 640x480. The HSTX command words are unrolled directly into a whole-frame buffer with the pixel bytes interleaved between them, and a free-running pair of DMA channels streams that buffer to the HSTX FIFO forever — one channel moves the frame, the other re-arms it by writing the buffer address back into the first channel's read-address trigger. The HSTX TMDS encoder expands RGB332 to full pixels in hardware, so no CPU code runs per scanline at all. The interrupt-latency underrun problem the 8-deep DMA ring defends against simply cannot occur in this mode: there is no ISR to be late, no matter what USB or `noInterrupts()` does. Trade-offs: single-buffered only (~352KB at 720x480, ~314KB at 640x480 — a second buffer doesn't fit in SRAM), fixed RGB332 color instead of a palette (the expander's bit-to-lane mapping replaces the lookup), and strided rows (28 bytes of command words precede each 720-byte pixel row; `drawPixel`/`fillRect` handle it, `rowAddr()`/`rowStride()` expose it for direct blits). Ships with example `05_turbo_720x480_test`.

## Performance improvements

These speed up the DMA scanline fill for the original library's `MODE_PALETTE` (8bpp) and `MODE_RGB565` (16bpp) modes too, not just the new 4bpp one. Nothing about how you use those modes changes — they just run faster:

- **Word-aligned reads**: the scanline fill used to read the framebuffer one byte (or one 16-bit pixel for RGB565) at a time. It now reads 4 bytes at once and unpacks from there — roughly 4x fewer loads for 4bpp/8bpp, 2x fewer for RGB565. Safe because every supported resolution is a multiple of 8 pixels wide, so a row is always a whole number of 4-byte words.
- **Pointer-bound loops**: those same loops compare the destination pointer to a precomputed end pointer instead of counting with an integer.
- **Fast line drawing in `DVHSTX4`**: `drawFastHLine`/`drawFastVLine` route through the byte-wise `fillRect` fast path, so Adafruit_GFX primitives built on them (`drawLine`, `drawRect`, `drawCircle`, etc.) don't fall back to pixel-by-pixel drawing.
- **Deeper DMA ring (3 → 8 scanline buffers)**: raises tolerance for interrupt-blocked stretches from ~95us to ~250us, fixing display dropouts caused by heavy USB host traffic.
- **Scratch Y for the 4bpp lookup tables**: `palette4_hi`/`palette4_lo` live in Scratch Y, a small dedicated SRAM bank, so the DMA ISR's per-pixel reads aren't fighting the DMA controller for bus access on the same striped SRAM. The 8bpp palette stays in normal SRAM — Scratch Y is only 4KB and a default 2KB core1 stack already lives there.

## Hardening

- **Checked allocations**: frame buffer and line buffer `malloc()` failures are detected and cleaned up, and `begin()` returns `false` instead of running with a null buffer.
- **Dynamic DMA channels**: the driver claims channels through the SDK instead of hardcoding channels 0-2, so it can't collide with other DMA users. Channels are released on `end()`.
- **Underrun diagnostics**: the scanline ISR counts late services (`late_isr_count` / `max_pending`, readable via `display.driver()`), so DMA-starvation issues can be measured instead of guessed at.

## Installation

1. Download the zip from the [Releases](https://github.com/anthonytheinventor/Adafruit-DVI-HSTX-Extended/releases) page (don't unzip it).
2. In the Arduino IDE: **Sketch → Include Library → Add .ZIP Library...** and select the downloaded file.
3. Restart the IDE if the examples don't show up right away. You'll find them under **File → Examples → Adafruit DVI HSTX Extended**.

This fork installs cleanly alongside Adafruit's original library — the main header is renamed to `Adafruit_dvhstx_extended.h` so the Arduino IDE never confuses the two. Just make sure your sketch includes the right one:

```cpp
#include <Adafruit_dvhstx_extended.h>   // this fork
```

## Quick start

A double-buffered 16-color display at native 720x480:

```cpp
#include <Adafruit_dvhstx_extended.h>

// If your board defines PIN_CKP etc. (Feather/Metro RP2350, Fruit Jam),
// DVHSTX_PINOUT_DEFAULT just works. Otherwise pass pins as {CKP, D0P, D1P, D2P}.
DVHSTX4 display(DVHSTX_PINOUT_DEFAULT, DVHSTX_RESOLUTION_720x480, true);

void setup() {
  if (!display.begin()) for (;;); // out of RAM or unsupported resolution

  display.setColor(1, 0xFF0000);  // palette slot 1 = red
  display.fillScreen(0);
  display.fillRect(20, 20, 200, 100, 1);
  display.swap();                 // present the frame
}

void loop() {}
```

Draw with any Adafruit_GFX call — colors are palette indices 0-15. See `04_720x480_test` for a full example with per-frame redraws and render timing.

## Documentation

The examples should work unmodified on the Adafruit Feather RP2350, Metro RP2350, and Fruit Jam, or any board that defines `PIN_CKP`, `PIN_D0P`, `PIN_D1P`, and `PIN_D2P` — just use `DVHSTX_PINOUT_DEFAULT`.

Otherwise, give the pinout as 4 numbers: `{ckp, d0p, d1p, d2p}` — the GPIO numbers for the clock pair's positive pin, then the positive pins of the D0/D1/D2 pairs.

One note on the native 720x480 mode: this library is DVI-only, so it doesn't send the HDMI flag that marks 720x480 as 16:9 anamorphic. Most TVs will show it as 4:3 by default; use the TV's stretch/zoom setting if you want widescreen.
