# Adafruit DVI HSTX Extended <!-- omit in toc -->

My ([anthonytheinventor](https://github.com/anthonytheinventor)) fork of Adafruit's `Adafruit_dvhstx` library — an Adafruit GFX compatible DVI driver for RP2 chips with HSTX (Adafruit Metro RP2350, Adafruit Fruit Jam, etc). The headline additions are a **zero-ISR "turbo" mode** that generates a rock-solid 720x480 or 640x480 DVI signal with no CPU involvement whatsoever, and a **scatter-gather DMA blitter** (`blitRows()`) for presenting off-screen-composed content in a fraction of a millisecond. The fork also adds a 4bpp indexed-color mode, a native 720x480 resolution for the standard driver, and DMA-path performance work. All credit for the original driver goes to Jeff Epler / Adafruit and the Pimoroni `dvhstx` driver underneath it.

**Important note on overclocking:** This library overclocks your RP2 chip to 240MHz just by including `<Adafruit_dvhstx_extended.h>`, separate from the Tools menu setting (which must stay at 150MHz). Like any overclock, there's some risk to component lifespan that's hard to quantify. Proceed at your own discretion.

- [The turbo mode and the blitter](#the-turbo-mode-and-the-blitter)
- [Supported modes and resolutions](#supported-modes-and-resolutions)
- [Why this fork exists](#why-this-fork-exists)
- [New features](#new-features)
- [Performance improvements](#performance-improvements)
- [Hardening](#hardening)
- [Installation](#installation)
- [Quick start](#quick-start)
- [Documentation](#documentation)

## The turbo mode and the blitter

**`DVHSTX8Turbo` is a display mode with no interrupt handler.** Every other mode in this library (and in the original) works the same basic way: a per-scanline ISR feeds pixel data to the HSTX peripheral through a ring of line buffers, and if anything blocks interrupts for too long — heavy USB host traffic, flash writes, a long `noInterrupts()` section — the ring runs dry and the picture drops out. The 8-deep ring in this fork raises that tolerance a lot, but the failure mode still exists; it's just further away.

Turbo mode removes the failure mode instead of pushing it back. The HSTX command words (sync sequences and pixel-run headers) are unrolled directly into the frame buffer, interleaved with the pixel bytes, and a free-running pair of DMA channels streams the whole thing to the HSTX FIFO forever — one channel moves the frame, the second re-arms the first by writing the buffer address back into its read-address trigger. RGB332 pixels are expanded to full TMDS symbols by the HSTX hardware itself. **No CPU code runs per scanline, per frame, or ever.** There is no ISR to be late, so the DVI signal's integrity is completely decoupled from what your sketch and its interrupts are doing. The scanout DMA is also given bus priority, so even flat-out memory traffic from your code can't starve the feed.

The cost is that it's single-buffered (a second 8bpp buffer doesn't fit in SRAM at these resolutions) and the color format is fixed RGB332 — the pixel byte *is* the color, no palette. Single-buffering is a smaller problem than it sounds, because of the second feature:

**`blitRows()` is a DMA blitter for presenting composed content.** The turbo frame's rows are strided (28 bytes of command words sit before each pixel row), so `blitRows()` uses a scatter-gather chain — a control DMA channel feeds one {source, destination} pair per row to a data channel — to copy contiguous rows from anywhere in memory into the live frame without ever touching the command words. Compose your content off-screen at your leisure (an SRAM scratch tile, or big assets staged in PSRAM), then present it in one call, synchronously or async. A full-width numeral band lands in a few hundred microseconds; an entire 720x480 frame presents in well under 1ms — fast enough to swap the whole screen inside the vertical blanking interval, which gets you tear-free full-screen updates on a single buffer. Falls back to `memcpy` transparently if the source is unaligned or no DMA channels are free, and the channels are only claimed on first use.

See `05_turbo_720x480_test` for both in action: a full-screen presentation via `blitRows()` measured every frame (0.7ms on real hardware), with the changing elements drawn inside vblank.

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

I wanted a high-resolution color dashboard driven by an RP2350 — enough pixels to show real data at a glance on a TV, in color, without flicker. That ambition ran into the chip's constraints from two directions, and the two halves of this fork are the answers.

The first constraint is RAM. The RP2350 has 520KB, and a double-buffered framebuffer at the original library's 8bpp color mode gets expensive fast — 720x480 at 8bpp double-buffered doesn't fit at all. Double buffering matters for a dashboard that redraws constantly, or it will flicker and tear. Dropping to 4bpp (16 colors) cuts the framebuffer cost in half, which is what makes a double-buffered native 720x480 display possible. That's `DVHSTX4` and the native 720x480 timing.

The second constraint is that a display node in a real system doesn't get to *just* be a display. Mine also services USB, radios, and sensors — interrupt-driven work that's bursty and not always polite. In the ISR-fed display modes, every interrupt source in the sketch is implicitly part of the video signal path: block interrupts a little too long and the scanline ring underruns and the picture drops. I widened the ring to push that boundary out, but I kept designing around it — auditing every library for interrupt behavior, being paranoid about `noInterrupts()`. The turbo mode is the escape from that whole class of problem: the DVI signal is generated entirely by DMA hardware with zero CPU involvement, so the CPU is free to take as many interrupts as the application wants, for as long as they take, without ever compromising signal integrity. The display can't glitch because of software, full stop — which is exactly the property you want when the display is bolted to a wall doing a job. The result is a more reliable system *and* a simpler one: one less real-time constraint to design the rest of the firmware around.

## New features

Stuff that doesn't exist in the original library:

- **`MODE_PALETTE4`**: a new 4bpp indexed-color mode — 16 colors, 2 pixels per byte, half the RAM of the original 8bpp palette mode at the same resolution.
- **`DVHSTX4`**: the Arduino-facing canvas class for that mode, with the same API as `DVHSTX8`/`DVHSTX16` (`setColor()`, `drawPixel()`/`getPixel()`, double-buffered `swap()`).
- **Native 720x480** (`DVHSTX_RESOLUTION_720x480`), using real CEA-861 NTSC timing pulled from a display's EDID and checked on hardware. Full resolution, not pixel-doubled like the original library's `DVHSTX_RESOLUTION_360x240` — and it only fits in RAM at 4bpp.
- **Palette lookup caching**: `rebuild_palette4_cache()` precomputes two 256-entry lookup tables so the DMA path is a straight lookup per pixel, no bit-shifting. Runs automatically on `init()` and `DVHSTX4::setColor()`.
- **Two new examples**: `03_4bpp_test` and `04_720x480_test`.
- **`DVHSTX8Turbo`** (1.2.0; 640x480 added in 1.3.0) and **`blitRows()`** (1.4.0): the zero-ISR turbo mode and its DMA blitter — see [the section above](#the-turbo-mode-and-the-blitter). Implementation details worth knowing: rows are strided (28 bytes of command words precede each pixel row; `drawPixel`/`fillRect` handle it, `rowAddr()`/`rowStride()` expose it), and the scanout channel takes both fabric-level (`bus_ctrl`) and DMA-internal high priority. Ships with example `05_turbo_720x480_test`.

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
