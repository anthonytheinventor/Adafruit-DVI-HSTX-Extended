# Changelog

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
