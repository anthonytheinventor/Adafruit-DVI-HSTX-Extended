// 4-bit (16 color) Adafruit_GFX-compatible framebuffer for RP2350 HSTX.
//
// True double-buffered 640x480 fits in ~300KB. At 8bpp double-buffered
// that's ~600KB, which doesn't fit in the RP2350's 520KB of SRAM.
//
// Status info is drawn on the display itself instead of Serial, since
// that's more useful once the display is actually running.
//
// Palette (16 entries total):
//   0     = black  -- status panel background
//   1-14  = green -> magenta gradient bars
//   15    = white  -- status panel text

#include <Adafruit_dvhstx_extended.h>

// If your board definition has PIN_CKP etc., DVHSTX_PINOUT_DEFAULT works.
DVHSTX4 display(DVHSTX_PINOUT_DEFAULT, DVHSTX_RESOLUTION_640x480, /*double_buffered=*/true);
// Otherwise give pins explicitly: {CKP, D0P, D1P, D2P}
// DVHSTX4 display({12, 14, 16, 18}, DVHSTX_RESOLUTION_640x480, true);

const int STATUS_PANEL_HEIGHT = 28; // 2 lines + margin

uint32_t heapBefore = 0;
uint32_t heapAfter = 0;
bool beginOk = false;

// Redraws the whole frame: status panel, gradient bars, bouncing line.
// Called every frame (see loop()) so both buffers stay fully in sync.
//
// renderMs/cpuPct < 0 means "not measured yet" (first frame, from setup()).
void drawFrame(int lineX, float renderMs, float cpuPct) {
  display.fillRect(0, 0, display.width(), STATUS_PANEL_HEIGHT, 0);
  display.setTextColor(15);
  display.setCursor(4, 4);
  display.printf("DVHSTX4 test   Heap: %u -> %u bytes   begin: %s\n",
                  heapBefore, heapAfter, beginOk ? "yes" : "NO");
  if (renderMs < 0) {
    display.println("Render: measuring...");
  } else {
    // no %f -- not every embedded printf supports it without extra flags
    int msTenths = (int)(renderMs * 10.0f + 0.5f);
    int pctTenths = (int)(cpuPct * 10.0f + 0.5f);
    display.printf("Render: %d.%dms/redraw = %d.%d%% of 60Hz slot "
                    "(full-screen worst case, not your real redraw rate)\n",
                    msTenths / 10, msTenths % 10, pctTenths / 10,
                    pctTenths % 10);
  }

  // last bar absorbs the leftover pixels from 640/14 not dividing evenly,
  // so all 14 bars cover the full width with no gap left un-redrawn
  int barTop = STATUS_PANEL_HEIGHT;
  int barHeight = display.height() - STATUS_PANEL_HEIGHT;
  int barWidth = display.width() / 14;
  for (int i = 0; i < 14; i++) {
    int bx = i * barWidth;
    int bw = (i == 13) ? (display.width() - bx) : barWidth;
    display.fillRect(bx, barTop, bw, barHeight, i + 1);
  }

  display.drawFastVLine(lineX, barTop + 40, 50, 15);
}

void setup() {
  Serial.begin(115200);
  // while (!Serial);

#if defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_RP2350)
  heapBefore = rp2040.getFreeHeap();
#endif

  beginOk = display.begin();
  if (!beginOk) { // no display to show this on, so just blink
    pinMode(LED_BUILTIN, OUTPUT);
    for (;;)
      digitalWrite(LED_BUILTIN, (millis() / 500) & 1);
  }

#if defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_RP2350)
  heapAfter = rp2040.getFreeHeap();
#endif

  display.setColor(0, 0, 0, 0);       // black
  display.setColor(15, 255, 255, 255); // white
  for (int i = 1; i <= 14; i++) {
    int t = i - 1; // 0..13
    display.setColor(i, t * 255 / 13, 255 - t * 255 / 13, t * 120 / 13);
  }

  drawFrame(0, -1.0f, -1.0f);
  display.swap(true); // sync both buffers
}

void loop() {
  // Redraw fully every frame -- with double buffering, swap() alternates
  // which buffer is front/back, so partial edits land inconsistently
  // unless every frame starts from a known state.
  //
  // No delay() here: swap() calls flip_blocking(), which already waits
  // for vsync, so the loop is naturally capped at ~60Hz on its own.
  static int x = 0;
  static int dx = 1;
  static float renderMs = -1.0f;
  static float cpuPct = -1.0f;

  x += dx;
  if (x <= 0 || x >= display.width() - 1)
    dx = -dx;

  // timed separately from swap() so render cost doesn't get mixed in
  // with vsync wait time
  uint32_t renderStart = micros();
  drawFrame(x, renderMs, cpuPct); // shows the previous iteration's numbers
  uint32_t renderUs = micros() - renderStart;

  uint32_t swapStart = micros();
  display.swap();
  uint32_t swapUs = micros() - swapStart;

  uint32_t periodUs = renderUs + swapUs;
  renderMs = renderUs / 1000.0f;
  cpuPct = (periodUs > 0) ? (100.0f * renderUs / periodUs) : 0.0f;
}
