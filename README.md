# Adafruit DVI HSTX Extended <!-- omit in toc -->

This is [anthonytheinventor](https://github.com/anthonytheinventor)'s fork of Adafruit's `Adafruit_dvhstx` library, an Adafruit GFX compatible DVI driver for RP2 chips with HSTX (e.g. Adafruit Metro RP2350, Adafruit Fruit Jam). All credit for the original driver goes to Jeff Epler / Adafruit and the underlying Pimoroni `dvhstx` driver it's built on; this fork adds a 4bpp indexed-color mode and a native 720x480 resolution on top of that base.

**Important note on overclocking:** This library overclocks your RP2 chip to 264MHz. Simply including the `<Adafruit_dvhstx_extended.h>` header enables this overclocking, separate from the option in the Arduino Tools menu.
Just like PC overclocking, there's some risk of reduced component lifespan, though the extent (if any) can't be precisely quantified and could vary from one chip to another.
Proceed at your own discretion.

- [Introduction](#introduction)
- [What's different in this fork](#whats-different-in-this-fork)
- [Installing alongside the original Adafruit library](#installing-alongside-the-original-adafruit-library)
- [Documentation](#documentation)

## Introduction

DV HSTX will enable you to create big, bold audio visual projects using Arduino and an HDMI display of your choice.

![Text mode display](hstx-textmode.png)
![Graphics](hstx-graphicsmode.png)

## What's different in this fork

This fork was built to fit a larger framebuffer into the RP2350's RAM budget. Main additions over upstream:

- **New `MODE_PALETTE4` driver mode: 4bpp indexed color, 16 colors, 2 pixels per byte.** Uses half the RAM of the existing 8bpp `MODE_PALETTE` mode at the same resolution, which is what makes larger double-buffered framebuffers (like 720x480, below) fit on-chip at all.
- **New `DVHSTX4` Arduino-facing class**, an Adafruit_GFX-compatible 16-color canvas built directly on packed-nibble pixel storage (not a `GFXcanvas8`/`GFXcanvas16` subclass, since Adafruit_GFX has no native 4-bit canvas type). Supports `setColor()`, rotation-aware `drawPixel()`/`getPixel()`, and double-buffered `swap()`, matching the API shape of the existing `DVHSTX8`/`DVHSTX16` classes.
- **Native, non-doubled 720x480 resolution** (`DVHSTX_RESOLUTION_720x480`), using real CEA-861 NTSC (VIC 2/3) timing pulled from an actual display's EDID rather than computed — verified against real hardware. This is distinct from the existing `DVHSTX_RESOLUTION_360x240`, which reaches the same 720x480 physical timing by pixel-doubling a smaller buffer; the new mode has full-resolution real pixels, only usable with `MODE_PALETTE4` since 720x480 at 8bpp double-buffered doesn't fit in RAM.
- **Palette lookup caching for `MODE_PALETTE4`**: a `rebuild_palette4_cache()` step precomputes two 256-entry lookup tables (indexed by the packed byte, not the nibble) so the scanline DMA path does a straight lookup per pixel with no shifting. It runs automatically on `init()` and whenever `DVHSTX4::setColor()` is called.
- **Frame buffer allocation hardening**: `malloc()` failures for the frame buffer(s) are now checked and handled (freeing any partial allocation and returning `false`) instead of proceeding with a null buffer.
- **DMA hot-path optimizations for `MODE_PALETTE4`**: the scanline fill in `gfx_dma_handler()` reads the packed framebuffer 4 bytes (a `uint32_t`, 8 pixels) at a time instead of one byte at a time — safe because every supported resolution is a multiple of 8 pixels wide, so a packed row is always a whole number of 4-byte words. The `palette4_hi`/`palette4_lo` lookup tables were also pulled out of the `DVHSTX` class and placed as `static` globals in Scratch Y (a dedicated, non-striped SRAM bank), so the ISR's per-pixel lookups don't contend on the bus with the DMA controller's own reads from the striped banks — the same reasoning already applied to `gfx_dma_handler()` itself living in Scratch X.
- **Two new examples**: `03_4bpp_test` (4bpp/16-color mode at 640x480) and `04_720x480_test` (native 720x480 in 4bpp mode).

## Installing alongside the original Adafruit library

The main header was renamed from `Adafruit_dvhstx.h` to **`Adafruit_dvhstx_extended.h`** specifically so this fork can be installed side by side with Adafruit's original `Adafruit_dvhstx` library without the Arduino IDE getting confused about which one to use. If you have both installed, sketches using this fork should `#include <Adafruit_dvhstx_extended.h>` rather than `#include <Adafruit_dvhstx.h>`.

> **Note:** If you intend to submit this fork to the Arduino Library Manager, be aware that using "Adafruit" in the library name/metadata may run into naming/trademark review issues there, since it isn't an official Adafruit release. It's fine for a personal GitHub fork/install, but consider renaming for a Library Manager submission.

## Documentation

See the examples in the `examples` folder. These examples should all work without changes on the Adafruit Feather RP2350, Adafruit Metro RP2350, and Adafruit Fruit Jam, as well as any other boards that define the HSTX pinout with preprocessor macros `PIN_CKP`, `PIN_D0P`, `PIN_D1P`, and `PIN_D2P`. If these are defined, then you can simply use `DVHSTX_PINOUT_DEFAULT`.

If your board does not define the HSTX pin mapping, it can be written as 4 numbers inside curly braces: `{ckp, d0p, d1p, d2p}` where e.g., `ckp` is the GPIO# of the positive pin in the clock pair, d0p is the positive pin in the D0 or red pin pair, and so on.
