// 720x480 8bpp RGB332 "turbo" mode on RP2350 HSTX.
//
// No per-scanline ISR: HSTX command words are unrolled into the frame
// buffer and a DMA pair repeatedly streams the complete frame.
// The TMDS encoder expands RGB332 in hardware, so nothing on the CPU can
// underrun the display -- USB host traffic, long noInterrupts() sections,
// flash writes, none of it matters.
//
// Trade-offs: single-buffered (~352KB, a second buffer won't fit), fixed
// RGB332 color (no palette), and strided rows (28 command bytes in DVI
// mode, 44 in HDMI mode, between pixel rows). The class handles the stride;
// direct blits should use rowAddr()/rowStride().
//
// The rainbow fields are redrawn in full every frame and timed, like the
// full-screen benchmark in 04_720x480_test. Overwriting a single buffer
// with identical content is tear-free -- the beam never sees anything
// different -- so this doubles as a demo that full-frame redraw IS safe
// here, as long as the content doesn't change between frames. Anything
// that does change (the marker, the stats line) is drawn inside vblank.

// arduino-pico builds at -Os by default; these copy/lookup loops are
// worth compiling for speed. Sketch-local -- doesn't change core flags.
#pragma GCC optimize("O2")

#include <Adafruit_dvhstx_extended.h>

// If your board definition has PIN_CKP etc., DVHSTX_PINOUT_DEFAULT works.
DVHSTX8Turbo display(DVHSTX_PINOUT_DEFAULT);
// Otherwise give pins explicitly: {CKP, D0P, D1P, D2P}
// DVHSTX8Turbo display({12, 14, 16, 18});
// Feather RP2350: DVHSTX8Turbo display(ADAFRUIT_FEATHER_RP2350_CFG);
// 640x480 instead: DVHSTX8Turbo display(DVHSTX_PINOUT_DEFAULT,
//                                       DVHSTX_RESOLUTION_640x480);
// Experimental anamorphic HDMI mode (may help TVs that don't scale
// 720x480 properly; ~8KB more RAM; leave off for pure DVI monitors):
// DVHSTX8Turbo display(DVHSTX_PINOUT_DEFAULT, DVHSTX_RESOLUTION_720x480,
//                      true);

// Layout (rows):
//   0-38    status panel, 3 text lines
//   40-47   bouncing marker band
//   52-59   rainbow label
//   64-263  hue (x) * value (y) field
//   268-407 hue (x) * saturation (y) field
//   416-477 R / G / B gradient ramps
const int STATS_LINE_Y = 14;   // second text line, redrawn per frame
const int MARKER_Y = 40, MARKER_H = 8;
const int FIELD1_Y = 64, FIELD1_H = 200;  // hue x value
const int FIELD2_Y = 268, FIELD2_H = 140; // hue x saturation
const int RAMP_Y = 416, RAMP_H = 18, RAMP_GAP = 4;

uint32_t heapBefore = 0;
uint32_t heapAfter = 0;

// display width, cached after begin() -- the sketch is width-agnostic
int W = 720;

// hue lookup, one entry per column, pre-quantized to RGB332
uint8_t hue332[720];
// dim[q][c]: RGB332 color c scaled to brightness level q (0-31)
uint8_t dimLut[32][256];
// wmix[q][c]: RGB332 color c mixed toward white by level q (0-31)
uint8_t whiteLut[32][256];
// one precomputed row per R/G/B ramp
uint8_t rampRow[3][720];

// The two rainbow fields only contain 63 unique rows between them (the
// brightness/whiteness LUT index changes far slower than y). Composing
// each unique row once and presenting by copy turns the redraw from
// per-pixel math into a pure blit -- the same staging pattern you'd use
// for real content composed off-screen.
uint8_t rowCache[63][720] __attribute__((aligned(4)));

inline const uint8_t *field1CacheRow(int y) {
  return rowCache[(31 - (y * 30 / (FIELD1_H - 1))) - 1]; // lut 31..1
}
inline const uint8_t *field2CacheRow(int y) {
  return rowCache[31 + (y * 31 / (FIELD2_H - 1))]; // lut 0..31
}

void buildLuts() {
  // 6 hue segments across the full width: R->Y->G->C->B->M->R.
  // Fixed-point so it works for any width (640/6 isn't an integer).
  for (int x = 0; x < W; x++) {
    int h = x * 1536 / W; // 0..1535
    int seg = h >> 8;
    int t = h & 255; // 0..255 within segment
    uint8_t r, g, b;
    switch (seg) {
    case 0: r = 255; g = t;       b = 0;       break; // red -> yellow
    case 1: r = 255 - t; g = 255; b = 0;       break; // yellow -> green
    case 2: r = 0;   g = 255;     b = t;       break; // green -> cyan
    case 3: r = 0;   g = 255 - t; b = 255;     break; // cyan -> blue
    case 4: r = t;   g = 0;       b = 255;     break; // blue -> magenta
    default: r = 255; g = 0;      b = 255 - t; break; // magenta -> red
    }
    hue332[x] = display.color332(r, g, b);
  }

  // Scaling after RGB332 quantization instead of before loses a little
  // precision, but the output is 3-3-2 anyway -- not visible. It's what
  // turns 9 ops/pixel into one table load.
  for (int q = 0; q < 32; q++) {
    int v = q * 255 / 31;
    for (int c = 0; c < 256; c++) {
      uint8_t r = c & 0xE0;          // expand channels to 8-bit-ish
      uint8_t g = (c & 0x1C) << 3;
      uint8_t b = (c & 0x03) << 6;
      dimLut[q][c] = display.color332((r * v) >> 8, (g * v) >> 8,
                                      (b * v) >> 8);
      whiteLut[q][c] =
          display.color332(r + (((255 - r) * v) >> 8),
                           g + (((255 - g) * v) >> 8),
                           b + (((255 - b) * v) >> 8));
    }
  }

  // ramp rows: computed once here, memcpy'd per row in drawRainbow()
  for (int x = 0; x < W; x++) {
    uint8_t v = x * 255 / (W - 1);
    rampRow[0][x] = display.color332(v, 0, 0);
    rampRow[1][x] = display.color332(0, v, 0);
    rampRow[2][x] = display.color332(0, 0, v);
  }
}

// Render the 63 unique field rows into the cache, once.
void buildRowCache() {
  for (int i = 0; i < 31; i++) { // field 1: lut levels 31..1
    const uint8_t *lut = dimLut[i + 1];
    for (int x = 0; x < W; x++)
      rowCache[i][x] = lut[hue332[x]];
  }
  for (int i = 0; i < 32; i++) { // field 2: lut levels 0..31
    const uint8_t *lut = whiteLut[i];
    for (int x = 0; x < W; x++)
      rowCache[31 + i][x] = lut[hue332[x]];
  }
}

// Full redraw of the rainbow fields and RGB ramps -- now pure
// presentation: every row is a blitRows() of a precomposed row. No
// per-pixel work at all.
void drawRainbow() {
  for (int y = 0; y < FIELD1_H; y++)
    display.blitRows(FIELD1_Y + y, field1CacheRow(y), 1);

  for (int y = 0; y < FIELD2_H; y++)
    display.blitRows(FIELD2_Y + y, field2CacheRow(y), 1);

  for (int c = 0; c < 3; c++) {
    int top = RAMP_Y + c * (RAMP_H + RAMP_GAP);
    for (int y = 0; y < RAMP_H; y++)
      display.blitRows(top + y, rampRow[c], 1);
  }
}

void setup() {
  Serial.begin(115200);
  // while (!Serial);

#if defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_RP2350)
  heapBefore = rp2040.getFreeHeap();
#endif

  bool beginOk = display.begin();
  if (!beginOk) { // no display to show this on, so just blink
    pinMode(LED_BUILTIN, OUTPUT);
    for (;;)
      digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
  }

#if defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_RP2350)
  heapAfter = rp2040.getFreeHeap();
#endif

  W = display.width();
  buildLuts();
  buildRowCache();

  display.fillScreen(0);
  display.setTextColor(display.color332(255, 255, 255));

  display.setCursor(4, 4);
  display.printf("DVHSTX8Turbo -- 720x480 RGB332, zero-ISR scanout   "
                 "Heap: %u -> %u bytes", heapBefore, heapAfter);
  display.setCursor(4, 24);
  display.print("Try hammering USB or blocking interrupts: "
                "the picture cannot drop out.");

  display.setCursor(4, 52);
  display.print("RGB332 rainbow -- hue x value (top), "
                "hue x saturation (middle), R/G/B ramps (bottom):");

  drawRainbow();
}

void loop() {
  static int x = 0;
  static int dx = 3;
  static float redrawMs = -1.0f;
  const int w = 12;

  int oldX = x;
  x += dx;
  if (x <= 0 || x >= display.width() - w)
    dx = -dx;

  // Changing content goes inside vblank: the marker, and the stats line.
  display.waitForVsync();
  display.fillRect(oldX, MARKER_Y, w, MARKER_H, 0);
  display.fillRect(x, MARKER_Y, w, MARKER_H, display.color332(255, 255, 0));

  display.fillRect(0, STATS_LINE_Y, display.width(), 9, 0);
  display.setCursor(4, STATS_LINE_Y);
  if (redrawMs < 0) {
    display.print("Full-screen redraw: measuring...");
  } else {
    // no %f -- not every embedded printf supports it without extra flags
    int msTenths = (int)(redrawMs * 10.0f + 0.5f);
    int pctTenths = (int)(redrawMs * 1000.0f / 16.7f + 0.5f);
    display.printf("Full-screen redraw: %d.%dms = %d.%d%% of a 60Hz frame "
                   "(precomposed rows presented by DMA blit)",
                   msTenths / 10, msTenths % 10, pctTenths / 10,
                   pctTenths % 10);
  }

  // Static content redrawn in full during active scan, and timed.
  // Identical bytes over identical bytes: the beam can't tell.
  uint32_t t0 = micros();
  drawRainbow();
  redrawMs = (micros() - t0) / 1000.0f;
}
