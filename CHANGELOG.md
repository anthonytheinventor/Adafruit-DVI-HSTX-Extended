# Changelog

## [1.5.2] - 2026-07-13

### Changed

- Set the AVI InfoFrame Active Format Information Present bit so the active-format field is valid.
- Added IT-content and graphics-content signaling while retaining full-range RGB and the underscan preference.
- Moved the HDMI data island four clocks into the horizontal-sync pulse. The preamble, guard bands, and packet now use constant sync levels.
- Corrected the data-island preamble to use the H-low control symbol.
- Updated the HDMI island-line command count from 45 to 47 words.
- Added a check that the horizontal-sync pulse is long enough to contain the data island.
- Corrected documentation for turbo row headers: 28 bytes in DVI mode and 44 bytes in HDMI mode.

## [1.5.1] - 2026-07-13

### Fixed

- Added the missing active-line HDMI video preamble and guard band. Their absence in 1.5.0 caused the emitted command stream to differ from the allocated layout and prevented sinks from locking to the signal.
- Added an initialization check that compares the emitted command count with the DMA transfer size.

## [1.5.0] - 2026-07-13

### Added

- Added optional HDMI signaling to `DVHSTX8Turbo`:

  ```cpp
  DVHSTX8Turbo display(pinout, resolution, true);
  ```

- Added one AVI InfoFrame per frame. The packet identifies VIC 3 and 16:9 at 720x480, or VIC 1 and 4:3 at 640x480, and requests full-range RGB and underscan.
- Added the required HDMI video preambles, guard bands, TERC4 encoding, BCH parity, and InfoFrame checksum generation.
- Kept DVI-compatible output as the default for monitor compatibility.

## [1.4.0] - 2026-07-10

### Added

- Added `DVHSTX8Turbo::blitRows()` with `blitBusy()` and `blitWait()`.
- Added scatter-gather DMA copies from tightly packed source rows into the strided turbo framebuffer.
- Added synchronous and asynchronous operation.
- Added a row-by-row `memcpy()` fallback for unaligned sources or unavailable DMA channels.
- Changed DMA channel allocation for the blitter to occur on first use and release during `reset()`.
- Set the turbo scanout DMA channel to high priority.

### Changed

- Updated `05_turbo_720x480_test` to precompute repeated rainbow rows and present them with `blitRows()`.

## [1.3.0] - 2026-07-09

### Added

- Added 640x480 support to `DVHSTX8Turbo`.
- Updated the low-level turbo driver to select either 720x480 CEA timing or 640x480 VESA timing at initialization.
- Updated `05_turbo_720x480_test` to use the selected display width and height.

### Changed

- Replaced the turbo driver's compile-time `WIDTH` and `HEIGHT` constants with runtime accessors.

## [1.2.0] - 2026-07-09

### Added

- Added `DVHSTX8Turbo`, a single-buffered RGB332 mode for 720x480 output.
- Added a frame-sized buffer containing both HSTX commands and pixel data.
- Added a two-channel DMA loop for continuous frame transmission.
- Added hardware RGB332 expansion through the HSTX TMDS encoder.
- Added `row()`, `row_stride_bytes()`, and polled vertical-blank synchronization.
- Added temporary DMA bus priority while turbo scanout is active.
- Added `05_turbo_720x480_test`.
- Added Arduino IDE keyword entries for the turbo API.

## [1.1.0] - 2026-07-08

### Added

- Added word-aligned framebuffer reads to the 8-bit palette and RGB565 scanline paths.
- Added `drawFastHLine()` and `drawFastVLine()` overrides to `DVHSTX4`.
- Added `late_isr_count` and `max_pending` diagnostics.
- Added a compile-time error for non-RP2350 targets.
- Added Arduino IDE keyword entries.
- Added installation, quick-start, and diagnostics documentation.

### Changed

- Increased the scanline DMA ring from three channels to eight.
- Changed DMA channel assignment from fixed channel numbers to dynamic allocation.
- Reduced code size in pixel-repetition scanline routines.
- Changed rotated `DVHSTX4::fillRect()` to use direct pixel drawing.
- Simplified source comments.

### Fixed

- Added allocation checks for line buffers and framebuffers.
- Corrected the CPU-speed error message from 264 MHz to 240 MHz.

### Removed

- Removed Adafruit-specific CI and issue-template files from the fork.

## [1.0.0] - 2026-07-01

### Added

- Forked Adafruit's `Adafruit_dvhstx` library under the public header `Adafruit_dvhstx_extended.h`.
- Added `MODE_PALETTE4` and the `DVHSTX4` canvas class.
- Added native 720x480 timing.
- Added cached 4-bit palette lookup tables.
- Added word-aligned 4-bit scanline reads.
- Placed the 4-bit lookup tables in Scratch Y.
- Added framebuffer allocation checks.
- Added 4-bit and 720x480 examples.
