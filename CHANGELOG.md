# Changelog

## [1.4.0] - 2026-07-10

### Added
- `DVHSTX8Turbo::blitRows(y, src, nRows, block)` (+ `blitBusy()`/`blitWait()`): DMA scatter-gather copy of contiguous pixel rows into the strided frame. A control channel feeds per-row {read, write-trigger} pairs into a data channel through the AL2 alias window, ending on a null trigger, so the interleaved command words between rows are never touched. Synchronous or async; falls back to memcpy for unaligned sources or if no DMA channels are free. Channels are claimed lazily on first use and released in `reset()`. Intended as the presentation primitive for off-screen composition (SRAM scratch or PSRAM staging).
- Scanout data channel now sets DMA-internal high priority (`channel_config_set_high_priority`) so a flat-out mem-to-mem blit can't add latency to the display feed.

### Changed
- Example `05_turbo_720x480_test`: the rainbow fields are precomposed into a 63-row cache at setup (they only contain 63 unique rows) and the per-frame redraw is now pure presentation via `blitRows()` -- no per-pixel work. Also builds with sketch-local `#pragma GCC optimize("O2")`. Expected full-frame present time drops roughly an order of magnitude versus 1.3.0's per-pixel LUT loop.


## [1.3.0] - 2026-07-09

### Added
- 640x480 support in turbo mode: `DVHSTX8Turbo(pinout, DVHSTX_RESOLUTION_640x480)`. `DVHSTXTurbo::init()` now takes width/height and selects between the 720x480 CEA and 640x480 VESA 60Hz timing tables; the command-stream layout, DMA loop, and RGB332 expander config are resolution-independent. Frame buffer is ~314KB at 640x480 (vs ~352KB at 720x480). 720x480 remains the default.
- Example `05_turbo_720x480_test` is now width-agnostic (fixed-point hue segmentation, all loops driven by `display.width()`), so switching the constructor to 640x480 renders correctly.

### Changed
- `DVHSTXTurbo::WIDTH`/`HEIGHT` compile-time constants replaced by runtime `width()`/`height()` accessors.

## [1.2.0] - 2026-07-09

### Added
- `DVHSTX8Turbo`: zero-ISR 720x480 8bpp RGB332 mode. HSTX command words are unrolled into a whole-frame buffer (pixel bytes interleaved between the sync/TMDS opcodes) and streamed by a free-running two-channel DMA loop -- a data channel paced by `DREQ_HSTX` chains to a restart channel that writes the buffer address back into the data channel's read-address trigger. RGB332 is expanded to full pixels by the HSTX TMDS encoder (`expand_shift` 4x8-bit, lane rotations 0/29/26), so no per-scanline CPU work exists and interrupt-latency underruns are structurally impossible. Single-buffered only (~352KB); rows strided by 748 bytes with the pixel data 28 bytes in.
- `pimoroni::DVHSTXTurbo` low-level driver (`dvhstx_turbo.hpp/.cpp`) with `row()`, `row_stride_bytes()`, and a polled `wait_for_vsync()` (no IRQ anywhere in this mode).
- Turbo mode elevates DMA bus priority (`bus_ctrl_hw->priority`, DMA_R|DMA_W) so the naked scanout feed can't be starved by core SRAM traffic during a bank collision -- this is the bus-contention underrun path, distinct from the interrupt-latency one the 8-deep ring guards in the other driver. Restored to fair arbitration on `reset()`.
- Example `05_turbo_720x480_test`: hue-x-value and hue-x-saturation rainbow fields plus R/G/B ramps, a vblank-synced animated marker, and a timed full-screen redraw every frame (identical-content overwrite -- tear-free in a single buffer) with on-screen ms/frame stats.
- `keywords.txt` entries for the new class and methods.

## [1.1.0] - 2026-07-08

### Added
- Word-aligned framebuffer reads for the original `MODE_PALETTE` (8bpp) and `MODE_RGB565` (16bpp) scanline paths (the 4bpp path already had them in 1.0.0).
- `drawFastHLine`/`drawFastVLine` overrides in `DVHSTX4` — Adafruit_GFX primitives (`drawLine`, `drawRect`, `drawCircle`, etc.) now use the byte-wise `fillRect` fast path instead of per-pixel drawing.
- DMA underrun diagnostics: `late_isr_count` and `max_pending` counters on the driver, reachable via `DVHSTX4::driver()`.
- Compile-time `#error` when building for a non-RP2350 board.
- Populated `keywords.txt` for Arduino IDE syntax highlighting.
- README: installation, quick-start, and hardening sections.

### Changed
- DMA scanline ring deepened from 3 to 8 channels, raising interrupt-latency tolerance from ~95us to ~250us. Fixes display dropouts / permanent no-signal under heavy USB host traffic.
- DMA channels are claimed dynamically through the SDK instead of hardcoding channels 0-2, and are released on `end()`.
- Pixel-repeat scanline variants use compact loops so the ISR fits in Scratch X at `-O3` (fixes a linker overflow).
- Rotated `fillRect` falls back to a direct pixel loop.
- Code comments trimmed throughout.

### Fixed
- Unchecked line-buffer allocation: `begin()` now returns `false` on failure instead of running with a null buffer.
- Stale "264MHz" in the CPU-speed error message (the library sets 240MHz).

### Removed
- Adafruit CI workflow and issue templates (`.github/`) — they require Adafruit-internal credentials and always failed on forks.

## [1.0.0] - 2026-07-01

Initial release. Fork of Adafruit's `Adafruit_dvhstx` adding `MODE_PALETTE4` (4bpp, 16 colors) with the `DVHSTX4` canvas class, native 720x480 (CEA-861) resolution, palette lookup caching, word-aligned 4bpp scanline reads, Scratch Y placement for the 4bpp lookup tables, checked frame-buffer allocation, and two new examples. Header renamed to `Adafruit_dvhstx_extended.h` for side-by-side installs with the original library.
