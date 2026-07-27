// Siikapaneeli — detection show + idle stats (three-panel test rig, MEAN WELL power).
//
// Two states (see plans/detection-and-idle.md):
//   idle      -> cycle stat pages (last hour / today / yesterday / total)
//   detected  -> record the catch, play the next detection animation, back to idle
//
// Trigger is one swap point (micSample): loudness sensor on GPIO34 now
// (see plans/loudness-trigger.md), digitalRead of the listener board's GPIO
// later. loop() is a free-running frame loop (plans/nonblocking-render.md):
// each renderer draws a frame, holds it, steps again; the mic is sampled
// between frames, so every detection during an animation queues one more
// replay and one more count (see plans/detection-and-idle.md).
// Counts persist in NVS across power loss. Wall-clock
// time comes from NTP over WiFi (creds in secrets.h) so today/yesterday/hour are real.
//
// Board: WEMOS D1 R32 (esp32:esp32:d1_uno32), data GPIO16, GRB.
// POWER: MEAN WELL LRS-150F-5 feeds the panels directly; the FastLED power
// limiter below is the content-aware hard cap.

#include <FastLED.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include <assert.h>
#include "secrets.h"          // WIFI_SSID, WIFI_PASS

// Test rig switch: 1 = play every animation back-to-back forever, mic and
// WiFi off. 0 = normal detection operation.
#define TEST_MODE 1

// ---- Canvas config (the scalability knob) ----
#define PANELS_X 3              // panels across  (final 5-6)
#define PANELS_Y 1              // panels stacked (final 2)
#define PANEL_W  16
#define PANEL_H  16
#define W        (PANELS_X * PANEL_W)   // logical canvas width
#define H        (PANELS_Y * PANEL_H)   // logical canvas height
#define NUM_LEDS (W * H)

#define DATA_PIN   16
#define BRIGHTNESS 64          // MEAN WELL phase (~25%); the power limiter in
                               // setup() is the real ceiling. On USB drop back to 12.

// Loudness sensor (Particle kit, analog out) — interim trigger until the
// XIAO listener board arrives. GPIO34 = A3, ADC1: input-only, no WiFi clash.
// Measured: the output is an envelope resting at 0 in silence; loud sound
// lifts it to ~250-670. It also emits brief spikes after loud events, so
// "loud" additionally requires the level to be SUSTAINED (see trigger block).
#define MIC_PIN    34
#define LOUD_LEVEL 250         // CALIBRATION KNOB — envelope level that counts as loud;
                               // measured: quiet room = 0, shout at 1 m ≈ 260-670

// Per-panel serpentine. CALIBRATION KNOBS — fixed against a real panel (see
// panel_test): true flips X across every row, matching this panel's wiring.
#define SERPENTINE true
#define FIRST_ROW_REVERSED true

// Helsinki time incl. DST — day/hour buckets need local, not UTC, day boundaries.
#define TZ_HELSINKI "EET-2EEST,M3.5.0/3,M10.5.0/4"

CRGB leds[NUM_LEDS];

const CRGB TEXT_COLOR  = CRGB(160, 210, 220);  // cyan-white
const CRGB LABEL_COLOR = CRGB(60, 90, 100);    // dim label
const CRGB FISH_COLOR  = CRGB(230, 90, 0);     // warm orange

struct Glyph { char ch; uint8_t w; uint8_t rows[5]; };

// ---- XY mapping: logical (x,y) -> LED index in the data chain ----
static_assert(PANEL_W == PANEL_H, "90-degree rotation needs square panels");

uint16_t localIndex(uint8_t lx, uint8_t ly) {   // index within one panel
  // CALIBRATION KNOB — wiring mounts each panel 90° CCW of the logical image
  // (seen on the 1x1 rig), so pre-rotate every panel's frame 90° CW.
  uint8_t rx = (PANEL_H - 1) - ly;
  uint8_t ry = lx;
  bool rev = (ry & 1) ? SERPENTINE : false;
  if (FIRST_ROW_REVERSED) rev = !rev;
  if (rev) rx = (PANEL_W - 1) - rx;
  return ry * (uint16_t)PANEL_W + rx;
}

// A panel's position in the chain. Row-major placeholder — CALIBRATION KNOB,
// fixed when real panels arrive. For 1 panel it is always 0.
uint16_t panelIndex(uint8_t px, uint8_t py) { return py * PANELS_X + px; }

uint16_t XY(int x, int y) {
  uint8_t px = x / PANEL_W, py = y / PANEL_H;
  uint8_t lx = x % PANEL_W, ly = y % PANEL_H;
  return panelIndex(px, py) * (PANEL_W * PANEL_H) + localIndex(lx, ly);
}

void setPx(int x, int y, CRGB c) {              // bounds-checked write
  if (x < 0 || x >= W || y < 0 || y >= H) return;
  leds[XY(x, y)] = c;
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

// Largest integer scale at which the string fits the canvas — text grows
// with the panel count instead of floating tiny in the middle.
int fitScale(const char* s) {
  int sc = 1;
  while (textWidth(s, sc + 1) <= W && FONT_H * (sc + 1) <= H) sc++;
  return sc;
}

// Draw one glyph, each source pixel a scale x scale block.
void drawChar(int x, int y, char ch, CRGB c, int scale) {
  const Glyph* g = glyphFor(ch);
  if (!g) return;
  for (int r = 0; r < FONT_H; r++)
    for (int col = 0; col < g->w; col++)
      if ((g->rows[r] >> (g->w - 1 - col)) & 1)
        for (int sy = 0; sy < scale; sy++)
          for (int sx = 0; sx < scale; sx++)
            setPx(x + col * scale + sx, y + r * scale + sy, c);
}

void drawText(int x, int y, const char* s, CRGB c, int scale) {
  for (int i = 0; s[i]; i++) {
    drawChar(x, y, s[i], c, scale);
    x += (charWidth(s[i]) + GAP) * scale;
  }
}

void drawCentered(const char* s, CRGB c, int scale) {
  drawText((W - textWidth(s, scale)) / 2, (H - FONT_H * scale) / 2, s, c, scale);
}

// ---- Fish sprite: 9x7, mirrored by facing ----
#define FISH_W 9
#define FISH_H 7
const uint16_t FISH[FISH_H] = {30, 127, 375, 511, 383, 127, 30};

void drawFish(int x, int y, CRGB c, bool faceRight) {
  for (int r = 0; r < FISH_H; r++)
    for (int col = 0; col < FISH_W; col++)
      if ((FISH[r] >> (FISH_W - 1 - col)) & 1) {
        int dx = faceRight ? col : (FISH_W - 1 - col);
        setPx(x + dx, y + r, c);
      }
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
Preferences prefs;
uint32_t g_total = 0;
int32_t  g_curDay = -1;        // -1 = no dated catch recorded yet
uint16_t g_today = 0, g_yest = 0;

void loadCounters() {
  prefs.begin("siika", false);
  g_total  = prefs.getUInt  ("total",  0);
  g_curDay = prefs.getInt   ("curDay", -1);
  g_today  = prefs.getUShort("today",  0);
  g_yest   = prefs.getUShort("yest",   0);
}

void saveCounters() {
  prefs.putUInt  ("total",  g_total);
  prefs.putInt   ("curDay", g_curDay);
  prefs.putUShort("today",  g_today);
  prefs.putUShort("yest",   g_yest);
}

// ---- Last-60-minutes rolling count: 60 one-minute buckets on uptime ----
// H = sum of the buckets covering the last 60 minutes. Uptime-based (millis) so it
// needs no wall clock — works before/without NTP, and it's a true rolling window,
// uncapped (unlike the old 64-slot ring that pinned H at 64). Not persisted: millis
// resets on reboot, and a >1 h outage legitimately zeroes the last hour anyway.
// ponytail: millis wraps at ~49 days => H briefly disturbed for one hour then; fine
// for a prototype, revisit only for a permanent install.
uint16_t g_min[60];            // detections in each minute
uint32_t g_minStamp[60];       // uptime-minute each bucket currently represents

uint32_t uptimeMin() { return millis() / 60000UL; }

// Pure window-sum, split out so selfTest() can exercise it with synthetic data.
uint16_t sumLastHour(uint32_t m, const uint16_t* mins, const uint32_t* stamps) {
  uint16_t c = 0;
  for (int i = 0; i < 60; i++)
    if (m - stamps[i] < 60) c += mins[i];   // bucket within the last 60 minutes
  return c;
}

uint16_t lastHourCount() { return sumLastHour(uptimeMin(), g_min, g_minStamp); }

void bumpLastHour() {
  uint32_t m = uptimeMin();
  uint8_t idx = m % 60;
  if (g_minStamp[idx] != m) { g_min[idx] = 0; g_minStamp[idx] = m; }  // recycle stale bucket
  g_min[idx]++;
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
  bumpLastHour();                          // H is uptime-based: counts with or without NTP
  if (timeKnown()) {                       // today/yesterday need the wall clock
    rollDay(localDayIndex(t), g_curDay, g_today, g_yest);
    g_today++;
  }
  saveCounters();
  // ponytail: one NVS write per catch. Catches are minutes+ apart => nowhere near
  // NVS endurance. Add batching only if catches ever become high-frequency.
}

// ---- Frame loop: modes + per-frame hold (plans/nonblocking-render.md) ----
// Every renderer is a step function: called when its hold expires, draws the
// next frame, holds it, returns true when finished. loop() samples the mic
// between frames.
enum Mode { MODE_IDLE, MODE_ANIM };    // DRAW, CLOCK arrive with the web UI
Mode g_mode = MODE_IDLE;
uint32_t g_holdUntil = 0;              // current frame stays up until this millis()
int g_ph = 0, g_i = 0;                 // per-animation phase/step, reset by beginAnim

void hold(uint32_t ms) { g_holdUntil = millis() + ms; }

// ---- Idle stats display (single panel: label on top, number below) ----
void numToStr(uint32_t n, char* buf) {
  if (n > 9999) { strcpy(buf, "9999"); return; }   // 1-panel cap; wall shows real value
  snprintf(buf, 8, "%lu", (unsigned long)n);
}

void drawStatPage(char label, uint32_t val, bool known) {
  FastLED.clear();
  char lb[2] = {label, 0};
  drawText((W - textWidth(lb, 1)) / 2, 1, lb, LABEL_COLOR, 1);
  char nb[8];
  if (known) numToStr(val, nb); else strcpy(nb, "--");
  drawText((W - textWidth(nb, 1)) / 2, 9, nb, TEXT_COLOR, 1);
  FastLED.show();
}

// Idle: sweep the four counters, STAT_MS each, so every number is readable.
// Never done — runs until a detection switches the mode.
const int STAT_MS = 2000;     // how long each counter stays up (tunable)
int g_idlePage = 0;
void stepIdle() {
  bool known = timeKnown();
  switch (g_idlePage) {
    case 0: drawStatPage('H', lastHourCount(), true);  break;  // last hour (uptime-based, no clock needed)
    case 1: drawStatPage('T', g_today,         known); break;  // today
    case 2: drawStatPage('E', g_yest,          known); break;  // yesterday
    case 3: drawStatPage('Y', g_total,         true ); break;  // total (always known)
  }
  hold(STAT_MS);
  g_idlePage = (g_idlePage + 1) % 4;
}

// ---- Detection animations (single-panel prototype content) ----
// Each is a step function: entry check for done (so the last frame's hold
// runs out before the transition), draw, hold, advance.

// 1. Fish swims across left->right (g_ph 0), then right->left (g_ph 1).
bool stepFishSwim() {
  if (g_ph >= 2) return true;
  int span = W + FISH_W;                      // x runs -FISH_W..W inclusive
  int y = (H - FISH_H) / 2;
  FastLED.clear();
  if (g_ph == 0) drawFish(-FISH_W + g_i, y, FISH_COLOR, true);
  else           drawFish(W - g_i,       y, FISH_COLOR, false);
  FastLED.show(); hold(45);
  if (++g_i > span) { g_i = 0; g_ph++; }
  return false;
}

// 2. "SIIKA" blinks 3x at 0.5 s: g_i counts half-cycles, even = lit.
bool stepSiikaTriple() {
  if (g_i >= 6) return true;
  FastLED.clear();
  if (!(g_i & 1)) drawCentered("SIIKA", TEXT_COLOR, fitScale("SIIKA"));
  FastLED.show(); hold(250);
  g_i++;
  return false;
}

// 3. "OTA SIIKA POIS" one word at a time, two passes (whole-phrase blink
//    needs the full wall). SIIKA is the widest word, so its fitScale keeps
//    all three words uniform.
bool stepOtaSiikaPois() {
  if (g_i >= 6) return true;
  const char* words[] = {"OTA", "SIIKA", "POIS"};
  FastLED.clear();
  drawCentered(words[g_i % 3], TEXT_COLOR, fitScale("SIIKA"));
  FastLED.show(); hold(500);
  g_i++;
  return false;
}

// 4. Spell S-I-I-K-A one big letter at a time (g_ph 0), then blink 5x (g_ph 1).
bool stepBigSiika() {
  if (g_ph == 1 && g_i >= 10) return true;    // 5 blinks = 10 half-cycles
  const char* letters = "SIIKA";
  FastLED.clear();
  if (g_ph == 0) {
    char one[2] = {letters[g_i], 0};
    drawCentered(one, TEXT_COLOR, fitScale("A"));   // as big as the height allows
    FastLED.show(); hold(300);
    if (++g_i >= 5) { g_ph = 1; g_i = 0; }
  } else {
    if (!(g_i & 1)) drawCentered("SIIKA", TEXT_COLOR, fitScale("SIIKA"));
    FastLED.show(); hold(250);
    g_i++;
  }
  return false;
}

// Milestone: every 10th catch gets a sparkle/rainbow burst instead of rotation.
bool stepMilestone() {
  if (g_i >= 60) return true;
  FastLED.clear();
  for (int i = 0; i < 12 * PANELS_X * PANELS_Y; i++)   // same density per panel
    setPx(random(W), random(H), CHSV(random(256), 255, 255));
  FastLED.show(); hold(30);
  g_i++;
  return false;
}

// ---- Registry + rotation ----
typedef bool (*AnimStep)();
AnimStep animations[] = { stepFishSwim, stepSiikaTriple, stepOtaSiikaPois, stepBigSiika };
const int NUM_ANIMS = sizeof(animations) / sizeof(animations[0]);
int animIdx   = 0;             // rotation position for detection animations
int g_testIdx = 0;             // TEST_MODE rotation position (anims + milestone)
AnimStep g_anim = nullptr;     // currently running animation

// Plain fn-pointer type in the signature: the .ino preprocessor hoists the
// generated prototype above the AnimStep typedef.
void beginAnim(bool (*a)()) { g_anim = a; g_ph = 0; g_i = 0; g_holdUntil = 0; }

void beginNextDetectionAnim() {
  if (g_total % 10 == 0) { beginAnim(stepMilestone); return; }   // celebrate every 10th
  beginAnim(animations[animIdx]);
  animIdx = (animIdx + 1) % NUM_ANIMS;
}

// ---- Trigger (single swap point) ----
// "Loud" = envelope >= LOUD_LEVEL sustained for LOUD_MIN_SAMPLES (a leaky
// score) — a shout holds the level for hundreds of ms and passes; the
// sensor's brief after-spikes are a few samples and never do. A loud moment
// counts as a NEW siika only when >= QUIET_GAP_MS of silence precedes it;
// ongoing noise keeps refreshing g_lastLoudMs, so back-to-back
// SIIKA,SIIKA,SIIKA with no pause merges into one. loop() calls micSample()
// on every pass, so the mic never closes between frames; the loop drains
// g_pending one animation per catch.
// ponytail: swap the mic sampling for digitalRead(TRIGGER_PIN) when the
// listener board arrives (see voice-trigger.md) — this stays the only swap point.
const int      LOUD_MIN_SAMPLES = 200;  // CALIBRATION KNOB — ~20 ms of sustained sound
                                        // at ADC speed; spikes are a few samples
const uint32_t QUIET_GAP_MS     = 1250; // CALIBRATION KNOB — silence required between
                                        // two siikas; anything closer merges into one
const uint8_t  PENDING_MAX      = 10;   // ponytail: queue cap so sustained noise can't
                                        // lock the panel into animations forever
uint8_t  g_pending    = 0;              // detections waiting for their animation
uint32_t g_lastLoudMs = 0;              // last moment the room was genuinely loud
int      g_loudScore  = 0;              // leaky count of above-threshold samples;
                                        // global so a shout bridges animation frames

int      g_micPeak = 0;                 // peak level since the last calibration print
uint32_t g_micN = 0;                    // samples since the last calibration print
uint32_t g_micPrintMs = 0;

// One mic sample + trigger scoring per loop() pass — the mic stays open
// between frames, closed only during FastLED.show(), same as before. Prints
// peak level + sample count for calibration every 2 s while idle (animation-
// time prints would spam serial).
void micSample() {
  int v = analogRead(MIC_PIN);
  g_micN++;
  if (v > g_micPeak) g_micPeak = v;
  if (v >= LOUD_LEVEL) { if (g_loudScore < LOUD_MIN_SAMPLES) g_loudScore++; }
  else                 { if (g_loudScore > 0)                g_loudScore--; }
  if (g_loudScore >= LOUD_MIN_SAMPLES) {        // sustained loud right now
    bool newSiika = millis() - g_lastLoudMs >= QUIET_GAP_MS;
    g_lastLoudMs = millis();
    if (newSiika) {
      if (g_pending < PENDING_MAX) g_pending++;
      Serial.printf("mic TRIGGER peak=%d pending=%u\n", g_micPeak, g_pending);
    }
  }
  if (g_mode == MODE_IDLE && millis() - g_micPrintMs >= 2000) {
    Serial.printf("mic peak=%d n=%lu\n", g_micPeak, (unsigned long)g_micN);
    g_micPrintMs = millis(); g_micPeak = 0; g_micN = 0;
  }
}

bool takeDetection() {
  if (!g_pending) return false;
  g_pending--;
  return true;
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

  // last-60-min buckets: sum only buckets within the last 60 uptime-minutes
  uint16_t mins[60] = {0}; uint32_t stamps[60] = {0};
  mins[0] = 5; stamps[0] = 100;               // this minute
  mins[1] = 3; stamps[1] = 41;                // 59 min ago -> included
  mins[2] = 7; stamps[2] = 40;                // 60 min ago -> excluded
  assert(sumLastHour(100, mins, stamps) == 8);

  assert(daysFromCivil(1970, 1, 1) == 0);     // day-index anchor + consecutiveness
  assert(daysFromCivil(1970, 1, 2) == 1);
  assert(daysFromCivil(2000, 1, 1) == 10957);
  Serial.println(F("selfTest OK"));
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\nSiikapaneeli — %dx%d panels (%dx%d px), GPIO16, TEST_MODE=%d\n",
                PANELS_X, PANELS_Y, W, H, TEST_MODE);
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  // MEAN WELL LRS-150F-5 (30 A): 6 A ≈ 2 A/panel — 5x PSU margin, safe on
  // hookup wire. Raise once the final 10 AWG distribution is wired.
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 6000);
  FastLED.clear();
  FastLED.show();

  selfTest();
  randomSeed(micros());
  loadCounters();
#if !TEST_MODE
  setupTime();                   // test rig boots fast, no WiFi/NTP
#endif

  Serial.printf("total=%lu today=%u yest=%u curDay=%ld timeKnown=%d\n",
                (unsigned long)g_total, g_today, g_yest, (long)g_curDay, timeKnown());
  Serial.printf("SIIKA fitScale=%d width=%d (must be <=%d)\n",
                fitScale("SIIKA"), textWidth("SIIKA", fitScale("SIIKA")), W);
}

void loop() {
#if TEST_MODE
  if (millis() < g_holdUntil) return;          // current frame / gap still up
  if (!g_anim) {                               // gap over: next in rotation,
    beginAnim(g_testIdx < NUM_ANIMS ? animations[g_testIdx] : stepMilestone);
    g_testIdx = (g_testIdx + 1) % (NUM_ANIMS + 1);   // milestone included
  }
  if (g_anim()) { g_anim = nullptr; FastLED.clear(); FastLED.show(); hold(400); }
#else
  micSample();
  if (g_mode == MODE_IDLE && takeDetection()) {   // reacts within one pass
    recordDetection(nowEpoch());
    beginNextDetectionAnim();
    g_mode = MODE_ANIM;
  }
  if (millis() < g_holdUntil) return;          // current frame still up
  if (g_mode == MODE_ANIM) {
    if (g_anim()) {                            // finished: drain the queue —
      FastLED.clear(); FastLED.show();         // one animation per catch;
      if (takeDetection()) {                   // shouts during a replay queued more
        recordDetection(nowEpoch());
        beginNextDetectionAnim();
      } else {
        g_mode = MODE_IDLE; g_idlePage = 0;
      }
    }
  } else {
    stepIdle();
  }
#endif
}
