// Siikapaneeli — WS2812B 16x16 panel test firmware (USB-powered prototype).
//
// Purpose: prove the panel lights up, reveal the physical wiring path and the
// XY mapping, and confirm color order — all at a brightness safe for USB power.
//
// Board:  WEMOS D1 R32 (esp32:esp32:d1_uno32), data on GPIO16, 256 LEDs, GRB.
// Serial: 115200 baud — watch over USB to follow which test is running.
//
// POWER: 256 WS2812B draw ~256 mA just idling (1 mA each) before any light.
// USB gives ~500 mA, so we keep BRIGHTNESS low and light few pixels at once.
// ponytail: fixed low brightness cap instead of a current estimator — raise
// BRIGHTNESS only once the MEAN WELL 5V supply feeds the panel, not USB.

#include <Adafruit_NeoPixel.h>

#define DATA_PIN   16
#define W          16
#define H          16
#define NUM_LEDS   (W * H)
#define BRIGHTNESS 12          // 0-255. Safe on USB. Bump up on the MEAN WELL PSU.

// Serpentine guess: row 0 at top, left-to-right; odd rows reversed (snake).
// This is the calibration knob — if TEST 2 does not scan cleanly L->R row by
// row, flip these two defines until it does, then that's the real mapping.
#define SERPENTINE true        // false = every row same direction (progressive)
#define FIRST_ROW_REVERSED false

Adafruit_NeoPixel strip(NUM_LEDS, DATA_PIN, NEO_GRB + NEO_KHZ800);

uint16_t XY(uint8_t x, uint8_t y) {
  bool rev = (y & 1) ? SERPENTINE : false;
  if (FIRST_ROW_REVERSED) rev = !rev;
  if (rev) x = (W - 1) - x;
  return y * (uint16_t)W + x;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(F("\nSiikapaneeli panel test — GPIO16, 256 LEDs, GRB"));
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.clear();
  strip.show();
}

// TEST 1 — raw snake: walk the strip in wiring order 0..255 so you can SEE the
// physical path the data takes (reveals serpentine direction and pixel 0's
// corner without assuming any mapping).
void rawSnake() {
  Serial.println(F("TEST 1: raw index walk 0..255 (physical wiring path)"));
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.clear();
    if (i > 0) strip.setPixelColor(i - 1, strip.Color(0, 8, 0));  // faint green trail
    strip.setPixelColor(i, strip.Color(60, 60, 60));              // white head
    strip.show();
    delay(25);
  }
}

// TEST 2 — XY scan: use XY() to move a dot logically row by row, left to right,
// with a fixed red marker at logical (0,0). If the mapping is correct the dot
// sweeps each row cleanly L->R starting from the red corner.
void xyScan() {
  Serial.println(F("TEST 2: XY scan row-by-row L->R, red = origin (0,0)"));
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      strip.clear();
      strip.setPixelColor(XY(0, 0), strip.Color(40, 0, 0));       // origin marker
      strip.setPixelColor(XY(x, y), strip.Color(0, 40, 40));      // scanning dot
      strip.show();
      delay(15);
    }
  }
}

// TEST 3 — color wash: fill the whole panel R, then G, then B at low
// brightness to confirm every pixel is alive and the color order is GRB.
void colorWash() {
  Serial.println(F("TEST 3: color wash R/G/B (checks all pixels + GRB order)"));
  uint32_t colors[] = { strip.Color(30, 0, 0), strip.Color(0, 30, 0), strip.Color(0, 0, 30) };
  for (uint32_t c : colors) {
    strip.fill(c);
    strip.show();
    delay(900);
  }
  strip.clear();
  strip.show();
  delay(400);
}

void loop() {
  rawSnake();
  xyScan();
  colorWash();
}
