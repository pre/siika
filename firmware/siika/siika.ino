// Siikapaneeli — detection show + idle stats (single-panel prototype, USB-powered).
//
// Two states (see plans/detection-and-idle.md):
//   idle      -> cycle stat pages (last hour / today / yesterday / total)
//   detected  -> record the catch, play the next detection animation, back to idle
//
// Trigger is one swap point (siikaDetected): loudness sensor on GPIO34 now
// (see plans/loudness-trigger.md), digitalRead of the listener board's GPIO
// later. Counts persist in NVS across power loss. Wall-clock
// time comes from NTP over WiFi (creds in secrets.h) so today/yesterday/hour are real.
//
// Board: WEMOS D1 R32 (esp32:esp32:d1_uno32), data GPIO16, 256 LEDs, GRB.
// POWER: still USB — BRIGHTNESS stays capped low; animations never fill the canvas.

#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include <assert.h>
#include "secrets.h"          // WIFI_SSID, WIFI_PASS

// ---- Canvas config (the scalability knob) ----
#define PANELS_X 1              // panels across  (final 5-6)
#define PANELS_Y 1              // panels stacked (final 2)
#define PANEL_W  16
#define PANEL_H  16
#define W        (PANELS_X * PANEL_W)   // logical canvas width
#define H        (PANELS_Y * PANEL_H)   // logical canvas height
#define NUM_LEDS (W * H)

#define DATA_PIN   16
#define BRIGHTNESS 12          // USB-safe cap. Raise only on the MEAN WELL PSU.

// Loudness sensor (Particle kit, analog out) — interim trigger until the
// XIAO listener board arrives. GPIO34 = A3, ADC1: input-only, no WiFi clash.
#define MIC_PIN           34
#define LOUD_PP_THRESHOLD 800  // CALIBRATION KNOB — 12-bit peak-to-peak; tune via serial

// Per-panel serpentine. CALIBRATION KNOBS — fixed against a real panel (see
// panel_test): true flips X across every row, matching this panel's wiring.
#define SERPENTINE true
#define FIRST_ROW_REVERSED true

// Helsinki time incl. DST — day/hour buckets need local, not UTC, day boundaries.
#define TZ_HELSINKI "EET-2EEST,M3.5.0/3,M10.5.0/4"

Adafruit_NeoPixel strip(NUM_LEDS, DATA_PIN, NEO_GRB + NEO_KHZ800);

const uint32_t TEXT_COLOR  = Adafruit_NeoPixel::Color(160, 210, 220);  // cyan-white
const uint32_t LABEL_COLOR = Adafruit_NeoPixel::Color(60, 90, 100);    // dim label
const uint32_t FISH_COLOR  = Adafruit_NeoPixel::Color(230, 90, 0);     // warm orange

struct Glyph { char ch; uint8_t w; uint8_t rows[5]; };

// ---- XY mapping: logical (x,y) -> LED index in the data chain ----
uint16_t localIndex(uint8_t lx, uint8_t ly) {   // index within one panel
  bool rev = (ly & 1) ? SERPENTINE : false;
  if (FIRST_ROW_REVERSED) rev = !rev;
  if (rev) lx = (PANEL_W - 1) - lx;
  return ly * (uint16_t)PANEL_W + lx;
}

// A panel's position in the chain. Row-major placeholder — CALIBRATION KNOB,
// fixed when real panels arrive. For 1 panel it is always 0.
uint16_t panelIndex(uint8_t px, uint8_t py) { return py * PANELS_X + px; }

uint16_t XY(int x, int y) {
  uint8_t px = x / PANEL_W, py = y / PANEL_H;
  uint8_t lx = x % PANEL_W, ly = y % PANEL_H;
  return panelIndex(px, py) * (PANEL_W * PANEL_H) + localIndex(lx, ly);
}

void setPx(int x, int y, uint32_t c) {          // bounds-checked write
  if (x < 0 || x >= W || y < 0 || y >= H) return;
  strip.setPixelColor(XY(x, y), c);
}

// ---- Compact proportional font, 5px tall. MSB = leftmost column ----
// Letters for the words + digits and labels (H/E/Y) for the idle stats + '-'
// for "value not known yet". Add glyphs when a new animation needs them.
const Glyph FONT[] = {
  {'S', 3, {0b111, 0b100, 0b111, 0b001, 0b111}},
  {'I', 1, {0b1,   0b1,   0b1,   0b1,   0b1  }},
  {'K', 3, {0b101, 0b101, 0b110, 0b101, 0b101}},
  {'A', 3, {0b111, 0b101, 0b111, 0b101, 0b101}},
  {'O', 3, {0b111, 0b101, 0b101, 0b101, 0b111}},
  {'T', 3, {0b111, 0b010, 0b010, 0b010, 0b010}},
  {'P', 3, {0b111, 0b101, 0b111, 0b100, 0b100}},
  {'H', 3, {0b101, 0b101, 0b111, 0b101, 0b101}},   // last hour
  {'E', 3, {0b111, 0b100, 0b111, 0b100, 0b111}},   // eilen (yesterday)
  {'Y', 3, {0b101, 0b101, 0b010, 0b010, 0b010}},   // yhteensa (total)
  {'0', 3, {0b111, 0b101, 0b101, 0b101, 0b111}},
  {'1', 3, {0b010, 0b110, 0b010, 0b010, 0b111}},
  {'2', 3, {0b111, 0b001, 0b111, 0b100, 0b111}},
  {'3', 3, {0b111, 0b001, 0b111, 0b001, 0b111}},
  {'4', 3, {0b101, 0b101, 0b111, 0b001, 0b001}},
  {'5', 3, {0b111, 0b100, 0b111, 0b001, 0b111}},
  {'6', 3, {0b111, 0b100, 0b111, 0b101, 0b111}},
  {'7', 3, {0b111, 0b001, 0b010, 0b010, 0b010}},
  {'8', 3, {0b111, 0b101, 0b111, 0b101, 0b111}},
  {'9', 3, {0b111, 0b101, 0b111, 0b001, 0b111}},
  {'-', 3, {0b000, 0b000, 0b111, 0b000, 0b000}},
  {' ', 2, {0,     0,     0,     0,     0    }},
};
const int FONT_LEN = sizeof(FONT) / sizeof(FONT[0]);
#define FONT_H 5
#define GAP    1               // px between glyphs (at scale 1)

const Glyph* glyphFor(char ch) {
  for (int i = 0; i < FONT_LEN; i++) if (FONT[i].ch == ch) return &FONT[i];
  return nullptr;              // unknown -> blank
}

uint8_t charWidth(char ch) {
  const Glyph* g = glyphFor(ch);
  return g ? g->w : 3;
}

// Pixel width of a string at the given integer scale (pixel-doubling).
int textWidth(const char* s, int scale) {
  int w = 0;
  for (int i = 0; s[i]; i++) {
    w += charWidth(s[i]);
    if (s[i + 1]) w += GAP;
  }
  return w * scale;
}

// Draw one glyph, each source pixel a scale x scale block.
void drawChar(int x, int y, char ch, uint32_t c, int scale) {
  const Glyph* g = glyphFor(ch);
  if (!g) return;
  for (int r = 0; r < FONT_H; r++)
    for (int col = 0; col < g->w; col++)
      if ((g->rows[r] >> (g->w - 1 - col)) & 1)
        for (int sy = 0; sy < scale; sy++)
          for (int sx = 0; sx < scale; sx++)
            setPx(x + col * scale + sx, y + r * scale + sy, c);
}

void drawText(int x, int y, const char* s, uint32_t c, int scale) {
  for (int i = 0; s[i]; i++) {
    drawChar(x, y, s[i], c, scale);
    x += (charWidth(s[i]) + GAP) * scale;
  }
}

void drawCentered(const char* s, uint32_t c, int scale) {
  drawText((W - textWidth(s, scale)) / 2, (H - FONT_H * scale) / 2, s, c, scale);
}

// Blink a centred string `times`, onMs lit / offMs dark, at the given scale.
void blinkCentered(const char* s, uint32_t c, int onMs, int offMs, int times, int scale) {
  for (int i = 0; i < times; i++) {
    strip.clear(); drawCentered(s, c, scale); strip.show(); delay(onMs);
    strip.clear();                            strip.show(); delay(offMs);
  }
}

// ---- Fish sprite: 9x7, mirrored by facing ----
#define FISH_W 9
#define FISH_H 7
const uint16_t FISH[FISH_H] = {30, 127, 375, 511, 383, 127, 30};

void drawFish(int x, int y, uint32_t c, bool faceRight) {
  for (int r = 0; r < FISH_H; r++)
    for (int col = 0; col < FISH_W; col++)
      if ((FISH[r] >> (FISH_W - 1 - col)) & 1) {
        int dx = faceRight ? col : (FISH_W - 1 - col);
        setPx(x + dx, y + r, c);
      }
}

void swim(uint32_t c, bool faceRight, int stepMs) {
  int y = (H - FISH_H) / 2;
  if (faceRight)
    for (int x = -FISH_W; x <= W; x++) { strip.clear(); drawFish(x, y, c, true);  strip.show(); delay(stepMs); }
  else
    for (int x = W; x >= -FISH_W; x--) { strip.clear(); drawFish(x, y, c, false); strip.show(); delay(stepMs); }
}

// ---- Time (NTP over WiFi) ----
uint32_t nowEpoch()   { return (uint32_t) time(nullptr); }
bool     timeKnown()  { return time(nullptr) > 1700000000UL; }   // after ~2023-11 => synced

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm).
int32_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  int32_t era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

// Local day index for day buckets — derived from the local calendar date, so
// consecutive local days differ by 1 (DST/year boundaries handled by localtime_r).
int32_t localDayIndex(uint32_t t) {
  time_t tt = t; struct tm lt; localtime_r(&tt, &lt);
  return daysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

void setupTime() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) delay(200);
  if (WiFi.status() == WL_CONNECTED) {
    configTzTime(TZ_HELSINKI, "pool.ntp.org", "time.nist.gov");
    struct tm tmv;
    uint32_t s2 = millis();
    while (!getLocalTime(&tmv, 200) && millis() - s2 < 5000) { /* wait for sync */ }
  }
  // ponytail: WiFi off after the one sync — the internal RTC holds time while
  // powered, and an idle radio can't glitch the WS2812B timing. Re-sync happens
  // at the next cold boot. Upgrade path: a periodic re-sync if drift ever matters.
  WiFi.mode(WIFI_OFF);
}

// ---- Persistent counter (NVS via Preferences) ----
#define RECENT_N 64            // ring of recent catch timestamps for the 1h window
Preferences prefs;
uint32_t g_total = 0;
int32_t  g_curDay = -1;        // -1 = no dated catch recorded yet
uint16_t g_today = 0, g_yest = 0;
uint32_t g_recent[RECENT_N];
uint8_t  g_rhead = 0;

void loadCounters() {
  prefs.begin("siika", false);
  g_total  = prefs.getUInt  ("total",  0);
  g_curDay = prefs.getInt   ("curDay", -1);
  g_today  = prefs.getUShort("today",  0);
  g_yest   = prefs.getUShort("yest",   0);
  g_rhead  = prefs.getUChar ("rhead",  0);
  memset(g_recent, 0, sizeof(g_recent));
  prefs.getBytes("recent", g_recent, sizeof(g_recent));   // no-op if key absent
}

void saveCounters() {
  prefs.putUInt  ("total",  g_total);
  prefs.putInt   ("curDay", g_curDay);
  prefs.putUShort("today",  g_today);
  prefs.putUShort("yest",   g_yest);
  prefs.putUChar ("rhead",  g_rhead);
  prefs.putBytes ("recent", g_recent, sizeof(g_recent));
}

// Pure day-rollover: on a new day shift today->yest; a gap of >1 day zeroes yest.
// Split out so selfTest() can exercise it without touching NVS.
void rollDay(int32_t d, int32_t &curDay, uint16_t &today, uint16_t &yest) {
  if (curDay < 0)     { curDay = d; return; }
  if (d == curDay)    return;
  yest  = (d == curDay + 1) ? today : 0;
  today = 0;
  curDay = d;
}

void recordDetection(uint32_t t) {
  g_total++;
  if (timeKnown()) {                       // else: count into total only (plan rule)
    rollDay(localDayIndex(t), g_curDay, g_today, g_yest);
    g_today++;
    g_recent[g_rhead] = t;
    g_rhead = (g_rhead + 1) % RECENT_N;
  }
  saveCounters();
  // ponytail: one NVS write per catch. Catches are minutes+ apart => nowhere near
  // NVS endurance. Add batching only if catches ever become high-frequency.
}

// Catches within the last hour. ponytail: saturates at RECENT_N (64/h) — plenty
// for a fish panel. Upgrade path: 60 one-minute buckets instead of a ring.
uint16_t lastHourCount() {
  if (!timeKnown()) return 0;
  uint32_t now = nowEpoch();
  uint16_t c = 0;
  for (int i = 0; i < RECENT_N; i++)
    if (g_recent[i] && g_recent[i] + 3600 >= now) c++;
  return c;
}

// ---- Idle stats display (single panel: label on top, number below) ----
void numToStr(uint32_t n, char* buf) {
  if (n > 9999) { strcpy(buf, "9999"); return; }   // 1-panel cap; wall shows real value
  snprintf(buf, 8, "%lu", (unsigned long)n);
}

void drawStatPage(char label, uint32_t val, bool known) {
  strip.clear();
  char lb[2] = {label, 0};
  drawText((W - textWidth(lb, 1)) / 2, 1, lb, LABEL_COLOR, 1);
  char nb[8];
  if (known) numToStr(val, nb); else strcpy(nb, "--");
  drawText((W - textWidth(nb, 1)) / 2, 9, nb, TEXT_COLOR, 1);
  strip.show();
}

// Idle: sweep the four counters, STAT_MS each, so every number is readable. Shown
// before every animation and doubles as the idle view when no siika is detected.
const int STAT_MS = 2000;     // how long each counter stays up (tunable)
void showCounterSweep() {
  bool known = timeKnown();
  drawStatPage('H', lastHourCount(), known); listenDelay(STAT_MS);  // last hour
  drawStatPage('T', g_today,         known); listenDelay(STAT_MS);  // today
  drawStatPage('E', g_yest,          known); listenDelay(STAT_MS);  // yesterday
  drawStatPage('Y', g_total,         true ); listenDelay(STAT_MS);  // total (always known)
}

// ---- Detection animations (single-panel prototype content) ----

// 1. Fish swims across left->right, then right->left.
void animFishSwim() { swim(FISH_COLOR, true, 45); swim(FISH_COLOR, false, 45); }

// 2. "SIIKA" blinks 3x at 0.5 s.
void animSiikaTriple() { blinkCentered("SIIKA", TEXT_COLOR, 250, 250, 3, 1); }

// 3. "OTA SIIKA POIS" one word at a time (whole-phrase blink needs the full wall).
void animOtaSiikaPois() {
  const char* words[] = {"OTA", "SIIKA", "POIS"};
  for (int cycle = 0; cycle < 2; cycle++)     // two passes
    for (int w = 0; w < 3; w++) {
      strip.clear(); drawCentered(words[w], TEXT_COLOR, 1); strip.show(); delay(500);
    }
}

// 4. Spell S-I-I-K-A one big letter at a time, then the whole word blinks 5x.
//    (Big single letters fit 16 px; the whole word at scale 2 would not.)
void animBigSiika() {
  const char* letters = "SIIKA";
  for (int i = 0; letters[i]; i++) {
    char one[2] = {letters[i], 0};
    strip.clear();
    drawText((W - textWidth(one, 2)) / 2, (H - FONT_H * 2) / 2, one, TEXT_COLOR, 2);
    strip.show(); delay(300);
  }
  blinkCentered("SIIKA", TEXT_COLOR, 250, 250, 5, 1);
}

// Milestone: every 10th catch gets a sparkle/rainbow burst instead of rotation.
void animMilestone() {
  for (int f = 0; f < 60; f++) {
    strip.clear();
    for (int i = 0; i < 12; i++)                         // 12 lit px: USB-safe
      setPx(random(W), random(H), Adafruit_NeoPixel::ColorHSV(random(65536)));
    strip.show(); delay(30);
  }
}

// ---- Registry + rotation ----
typedef void (*Animation)();
Animation animations[] = { animFishSwim, animSiikaTriple, animOtaSiikaPois, animBigSiika };
const int NUM_ANIMS = sizeof(animations) / sizeof(animations[0]);
int animIdx = 0;

void playNextDetectionAnim() {
  if (g_total % 10 == 0) { animMilestone(); return; }   // celebrate every 10th
  animations[animIdx]();
  animIdx = (animIdx + 1) % NUM_ANIMS;
}

// ---- Trigger (single swap point) ----
// Loudness sensor: listenDelay() samples MIC_PIN while the counter sweep shows
// and latches g_heard on a loud peak. Animations never listen -> natural cooldown.
// ponytail: swap the latch for digitalRead(TRIGGER_PIN) when the listener
// board arrives (see voice-trigger.md) — this stays the only swap point.
bool g_heard = false;

// delay(ms) that keeps the mic open: peak-to-peak over the window, early exit
// on a hit so a clap reacts instantly (the guard skips the sweep's remaining
// pages). Prints pp for threshold calibration.
void listenDelay(uint32_t ms) {
  if (g_heard) return;                  // already latched — fall through fast
  uint32_t start = millis();
  int lo = 4095, hi = 0;
  while (millis() - start < ms) {
    int v = analogRead(MIC_PIN);
    if (v < lo) lo = v;
    if (v > hi) hi = v;
    if (hi - lo >= LOUD_PP_THRESHOLD) {
      g_heard = true;
      Serial.printf("mic TRIGGER pp=%d\n", hi - lo);
      return;
    }
  }
  Serial.printf("mic pp=%d\n", hi - lo);
}

bool siikaDetected() {
  bool h = g_heard;
  g_heard = false;
  return h;
}

// ---- Self-check: the non-trivial counter logic, no NVS/time needed ----
void selfTest() {
  int32_t cur = -1; uint16_t td = 0, ye = 0;
  rollDay(100, cur, td, ye); td = 3;          // first dated day -> today = 3
  rollDay(100, cur, td, ye);                  // same day: unchanged
  assert(td == 3 && ye == 0 && cur == 100);
  rollDay(101, cur, td, ye);                  // next day: yest <- 3, today -> 0
  assert(ye == 3 && td == 0 && cur == 101);
  td = 5; rollDay(110, cur, td, ye);          // 9-day gap: yest -> 0
  assert(ye == 0 && td == 0 && cur == 110);

  uint32_t now = 100000, r[4] = {now - 10, now - 3599, now - 3601, 0};
  int c = 0; for (int i = 0; i < 4; i++) if (r[i] && r[i] + 3600 >= now) c++;
  assert(c == 2);                             // window counts the two within 1h

  assert(daysFromCivil(1970, 1, 1) == 0);     // day-index anchor + consecutiveness
  assert(daysFromCivil(1970, 1, 2) == 1);
  assert(daysFromCivil(2000, 1, 1) == 10957);
  Serial.println(F("selfTest OK"));
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println(F("\nSiikapaneeli — detection show + idle stats, GPIO16, single panel"));
  strip.begin();
  strip.setBrightness(BRIGHTNESS);
  strip.clear();
  strip.show();

  selfTest();
  randomSeed(micros());
  loadCounters();
  setupTime();

  Serial.printf("total=%lu today=%u yest=%u curDay=%ld timeKnown=%d\n",
                (unsigned long)g_total, g_today, g_yest, (long)g_curDay, timeKnown());
  Serial.printf("widths: SIIKA(s1)=%d  S(s2)=%d (must be <=16)\n",
                textWidth("SIIKA", 1), textWidth("S", 2));
}

void loop() {
  showCounterSweep();                 // idle: counters up + mic listening
  if (siikaDetected()) {              // loud sound latched during the sweep
    recordDetection(nowEpoch());
    playNextDetectionAnim();
    strip.clear(); strip.show();
  }
}
