# Adafruit DVI HSTX Extended <!-- omit in toc -->

This is my ([anthonytheinventor](https://github.com/anthonytheinventor)) fork of Adafruit's `Adafruit_dvhstx` library — an Adafruit GFX compatible DVI driver for RP2 chips with HSTX (Adafruit Metro RP2350, Adafruit Fruit Jam, etc). All credit for the original driver goes to Jeff Epler / Adafruit and the Pimoroni `dvhstx` driver underneath it. I added a 4bpp indexed-color mode and a native 720x480 resolution on top.

**Important note on overclocking:** This library overclocks your RP2 chip to 264MHz just by including `<Adafruit_dvhstx_extended.h>`, separate from the Tools menu setting. Like any overclock, there's some risk to component lifespan that's hard to quantify. Proceed at your own discretion.

- [Introduction](#introduction)
- [Why this fork exists](#why-this-fork-exists)
- [What's different](#whats-different)
- [Installing alongside the original Adafruit library from the zip file](#Installing from the zip file)
- [Documentation](#documentation)

## Introduction

DVI HSTX lets you drive big, bold audio-visual projects over HDMI straight from Arduino.

![Text mode display](hstx-textmode.png)
![Graphics](hstx-graphicsmode.png)

## Why this fork exists

I wanted higher resolution for data displays and dashboards — enough pixels to actually show something useful — without giving up color or double buffering. Double buffering matters here because a dashboard that's redrawing constantly will flicker and tear without it. The problem is the RP2350 only has 520KB of RAM, and a double-buffered framebuffer at the existing 8bpp color mode gets expensive fast — 720x480 at 8bpp double-buffered doesn't fit at all. Dropping to 4bpp (16 colors) cuts that framebuffer cost in half, which is what makes a double-buffered 720x480 display possible in the first place.

## What's different

- **`MODE_PALETTE4`**: a new 4bpp indexed-color driver mode, 16 colors, 2 pixels per byte. Half the RAM of the existing 8bpp palette mode at the same resolution.
- **`DVHSTX4`**: a new Arduino-facing canvas class for that mode. Built directly on packed-nibble storage rather than a `GFXcanvas8`/`GFXcanvas16` subclass, since Adafruit_GFX has no native 4-bit canvas. Same API shape as `DVHSTX8`/`DVHSTX16` — `setColor()`, `drawPixel()`/`getPixel()`, double-buffered `swap()`.
- **Native 720x480** (`DVHSTX_RESOLUTION_720x480`), using real CEA-861 NTSC timing pulled from an actual display's EDID and verified on hardware. This is a genuine full-resolution mode, not the pixel-doubled 720x480 you get from `DVHSTX_RESOLUTION_360x240` — and it only fits in RAM at 4bpp.
- **Palette lookup caching**: `rebuild_palette4_cache()` precomputes two 256-entry lookup tables so the DMA scanline path is a straight lookup per pixel, no bit-shifting. Runs automatically on `init()` and on `DVHSTX4::setColor()`.
- **Safer frame buffer allocation**: `malloc()` failures are now checked and handled instead of silently continuing with a null buffer.
- **Faster `MODE_PALETTE4` DMA path**: the scanline fill reads the framebuffer 4 bytes at a time instead of 1, and the lookup tables now live in Scratch Y RAM so they're not fighting the DMA controller for bus access on the same memory banks.
- **Two new examples**: `03_4bpp_test` and `04_720x480_test`.

## Installing from the zip file

1. Download the `Adafruit-DVI-HSTX-Extended.zip` file from the Releases page (don't unzip it).
2. In the Arduino IDE, go to **Sketch → Include Library → Add .ZIP Library...**
3. Select the zip file you downloaded and click **Open**.
4. Restart the Arduino IDE if the examples don't show up right away.

You'll then find the examples under **File → Examples → Adafruit DVI HSTX Extended**.

This works alongside Adafruit's original `Adafruit_dvhstx` library if you have it
installed too — just make sure your sketch includes
`<Adafruit_dvhstx_extended.h>` rather than `<Adafruit_dvhstx.h>`.

## Documentation

See the examples in the `examples` folder — they should work unmodified on the Adafruit Feather RP2350, Metro RP2350, and Fruit Jam, or any board that defines `PIN_CKP`, `PIN_D0P`, `PIN_D1P`, and `PIN_D2P`. If those are defined, just use `DVHSTX_PINOUT_DEFAULT`.

Otherwise, give the pinout as 4 numbers: `{ckp, d0p, d1p, d2p}` — the GPIO numbers for the clock pair's positive pin, then the positive pins of the D0/D1/D2 pairs.
