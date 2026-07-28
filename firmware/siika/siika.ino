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
// Board: WEMOS D1 R32 (esp32:esp32:d1_uno32), GRB. Data: GPIO16 (shield pin 5,
// row 0) + GPIO17 (shield pin 4, row 1) — one chain of 3 panels per row
// (plans/six-panel-2x3.md); both RMT channels clock out in one show().
// POWER: MEAN WELL LRS-150F-5 feeds the panels directly; the FastLED power
// limiter below is the content-aware hard cap.

#include <FastLED.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <time.h>
#include <assert.h>
#include "secrets.h"          // WIFI_SSID, WIFI_PASS, OTA_PASS
#include "ui.h"               // INDEX_HTML (the whole web UI, PROGMEM)

// Test rig switch: 1 = play every animation back-to-back forever, mic, WiFi
// and web off. 0 = normal detection operation.
#define TEST_MODE 0

// ---- Canvas config ----
// PANELS_X/Y are first-boot defaults; the runtime grid comes from settings
// (panelsX/panelsY, applied at boot). leds[] is sized for the final wall.
#define PANELS_X 3              // panels across  (final 5-6)
#define PANELS_Y 2              // panels stacked (final 2)
#define PANEL_W  16
#define PANEL_H  16
#define PANELS_MAX 12           // final wall: 6 across, 2 stacked
#define MAX_LEDS (PANELS_MAX * PANEL_W * PANEL_H)   // 3072 = 9.2 KB

#define DATA_PIN_A 16           // shield pin 5: row 0 chain
#define DATA_PIN_B 17           // shield pin 4: row 1 chain (panelsY == 2 only)

// Chain-layout CALIBRATION KNOBS — fixed against the real rig:
#define ROWS_SWAPPED  false     // true if pin 4 turns out to drive the TOP row
#define ROW1_MIRRORED false     // true if the second chain enters from the right
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
#define LOUD_MIN_SAMPLES 200   // CALIBRATION KNOB — ~20 ms of sustained sound at ADC
                               // speed; the sensor's after-spikes are a few samples
#define QUIET_GAP_MS     1250  // CALIBRATION KNOB — silence required between two
                               // siikas; anything closer merges into one
#define STAT_MS          2000  // how long each idle counter page stays up

// Per-panel serpentine. CALIBRATION KNOBS — fixed against a real panel (see
// panel_test): true flips X across every row, matching this panel's wiring.
#define SERPENTINE true
#define FIRST_ROW_REVERSED true

// Helsinki time incl. DST — day/hour buckets need local, not UTC, day boundaries.
#define TZ_HELSINKI "EET-2EEST,M3.5.0/3,M10.5.0/4"

CRGB leds[MAX_LEDS];

// ---- Settings: one JSON blob in NVS; the #defines above are the first-boot
// defaults. Everything applies live except the grid size (addLeds locks the
// LED count at boot). Owned by the render task; the web task hands changes
// over via g_pendingSet (see the web section).
struct Settings {
  uint8_t  brightness     = BRIGHTNESS;
  uint16_t loudLevel      = LOUD_LEVEL;
  uint16_t loudMinSamples = LOUD_MIN_SAMPLES;
  uint32_t quietGapMs     = QUIET_GAP_MS;
  uint16_t statMs         = STAT_MS;
  uint8_t  panelsX        = PANELS_X;      // applied at next boot
  uint8_t  panelsY        = PANELS_Y;      // applied at next boot
  bool     serpentine     = SERPENTINE;
  bool     firstRowRev    = FIRST_ROW_REVERSED;
  CRGB     textColor      = CRGB(160, 210, 220);  // cyan-white
  CRGB     labelColor     = CRGB(60, 90, 100);    // dim label
  CRGB     fishColor      = CRGB(230, 90, 0);     // warm orange
};
Settings g_set;
int W, H, NUM_LEDS;    // derived from g_set in setup(); grid changes need a reboot

// Render modes. ANIM is an overlay: a detection plays its animation in every
// mode, then returns to the selected base mode (see plans/ui-features.md).
enum Mode { MODE_IDLE, MODE_ANIM, MODE_DRAW, MODE_CLOCK };

// Web -> render crossing (architecture.md rule: the web task never touches
// leds[] or FastLED). The render loop applies these between frames in
// applyWebInput(); status reads snapshot the counters under the same mutex.
SemaphoreHandle_t g_mux;
Settings g_pendingSet;
volatile bool g_setPending  = false;  // g_pendingSet waits to be applied
volatile bool g_webTrigger  = false;  // POST /api/trigger: fake one siika
volatile bool g_otaActive   = false;  // flash write in progress: render freezes
volatile bool g_modePending = false;  // g_pendingMode waits to be applied
volatile Mode g_pendingMode = MODE_IDLE;
volatile bool g_drawPending = false;  // g_drawBuf holds a fresh frame
uint8_t g_drawBuf[MAX_LEDS * 3];      // browser frame, row-major logical RGB;
                                      // written by web, read by render, under g_mux

struct Glyph { char ch; uint8_t w; uint8_t rows[5]; };

// ---- XY mapping: logical (x,y) -> LED index in the data chain ----
static_assert(PANEL_W == PANEL_H, "90-degree rotation needs square panels");

uint16_t localIndex(uint8_t lx, uint8_t ly) {   // index within one panel
  // CALIBRATION KNOB — wiring mounts each panel 90° CCW of the logical image
  // (seen on the 1x1 rig), so pre-rotate every panel's frame 90° CW.
  uint8_t rx = (PANEL_H - 1) - ly;
  uint8_t ry = lx;
  bool rev = (ry & 1) ? g_set.serpentine : false;
  if (g_set.firstRowRev) rev = !rev;
  if (rev) rx = (PANEL_W - 1) - rx;
  return ry * (uint16_t)PANEL_W + rx;
}

// A panel's position in the data order: one chain per row, chains laid out
// row-major in leds[] (row 0 = pin A slice, row 1 = pin B slice). The knobs
// above absorb whichever way the rig was actually wired.
uint16_t panelIndex(uint8_t px, uint8_t py) {
  if (ROWS_SWAPPED) py = (g_set.panelsY - 1) - py;
  if (ROW1_MIRRORED && py == 1) px = (g_set.panelsX - 1) - px;
  return py * g_set.panelsX + px;
}

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
  {':', 1, {0b0,   0b1,   0b0,   0b1,   0b0  }},   // clock mode
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

// WiFi stays ON (architecture.md): the web server needs it, SNTP re-syncs the
// clock periodically for free, and flicker is handled by the core split + RMT
// hardware buffering. No NTP wait: timeKnown() flips when the sync lands.
void setupWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) delay(200);
  configTzTime(TZ_HELSINKI, "pool.ntp.org", "time.nist.gov");
  if (MDNS.begin("siika")) MDNS.addService("http", "tcp", 80);
  Serial.printf("WiFi %s, IP %s, http://siika.local\n",
                WiFi.status() == WL_CONNECTED ? "connected" : "NOT connected (auto-retry)",
                WiFi.localIP().toString().c_str());
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

// ---- Settings persistence: one JSON blob in the same NVS namespace ----
void colorToHex(CRGB c, char *buf) { sprintf(buf, "%02X%02X%02X", c.r, c.g, c.b); }

CRGB hexToColor(const char *h, CRGB fallback) {
  if (!h || strlen(h) != 6) return fallback;
  char *end; uint32_t v = strtoul(h, &end, 16);
  if (*end) return fallback;
  return CRGB(v >> 16, (v >> 8) & 0xFF, v & 0xFF);
}

void settingsJson(const Settings &s, String &out) {
  JsonDocument d; char hex[7];
  d["brightness"]     = s.brightness;
  d["loudLevel"]      = s.loudLevel;
  d["loudMinSamples"] = s.loudMinSamples;
  d["quietGapMs"]     = s.quietGapMs;
  d["statMs"]         = s.statMs;
  d["panelsX"]        = s.panelsX;
  d["panelsY"]        = s.panelsY;
  d["serpentine"]     = s.serpentine;
  d["firstRowRev"]    = s.firstRowRev;
  colorToHex(s.textColor,  hex); d["textColor"]  = hex;
  colorToHex(s.labelColor, hex); d["labelColor"] = hex;
  colorToHex(s.fishColor,  hex); d["fishColor"]  = hex;
  serializeJson(d, out);
}

// Overlay json onto s — missing keys keep their current values, everything
// clamped at this trust boundary. Loads the NVS blob and POST /api/settings.
bool settingsFromJson(const String &json, Settings &s, String &err) {
  JsonDocument d;
  if (deserializeJson(d, json)) { err = "bad json"; return false; }
  s.brightness     = constrain((int)(d["brightness"]     | (int)s.brightness),     0, 255);
  s.loudLevel      = constrain((int)(d["loudLevel"]      | (int)s.loudLevel),      0, 4095);
  s.loudMinSamples = constrain((int)(d["loudMinSamples"] | (int)s.loudMinSamples), 1, 20000);
  s.quietGapMs     = constrain((int)(d["quietGapMs"]     | (int)s.quietGapMs),     0, 600000);
  s.statMs         = constrain((int)(d["statMs"]         | (int)s.statMs),       100, 60000);
  s.panelsX        = constrain((int)(d["panelsX"]        | (int)s.panelsX),        1, PANELS_MAX);
  s.panelsY        = constrain((int)(d["panelsY"]        | (int)s.panelsY),        1, PANELS_MAX);
  if (s.panelsX * s.panelsY > PANELS_MAX) { err = "grid exceeds 12 panels"; return false; }
  s.serpentine  = d["serpentine"]  | s.serpentine;
  s.firstRowRev = d["firstRowRev"] | s.firstRowRev;
  s.textColor   = hexToColor(d["textColor"]  | "", s.textColor);
  s.labelColor  = hexToColor(d["labelColor"] | "", s.labelColor);
  s.fishColor   = hexToColor(d["fishColor"]  | "", s.fishColor);
  return true;
}

void loadSettings() {
  String j = prefs.getString("set", "");
  String err;
  if (j.length() && !settingsFromJson(j, g_set, err))
    Serial.printf("settings blob rejected (%s), using defaults\n", err.c_str());
}

void saveSettings(const Settings &s) {
  String out; settingsJson(s, out);
  prefs.putString("set", out);          // NVS API is internally thread-safe
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
  xSemaphoreTake(g_mux, portMAX_DELAY);    // /api/status snapshots under the same mutex
  g_total++;
  bumpLastHour();                          // H is uptime-based: counts with or without NTP
  if (timeKnown()) {                       // today/yesterday need the wall clock
    rollDay(localDayIndex(t), g_curDay, g_today, g_yest);
    g_today++;
  }
  xSemaphoreGive(g_mux);
  saveCounters();
  // ponytail: one NVS write per catch. Catches are minutes+ apart => nowhere near
  // NVS endurance. Add batching only if catches ever become high-frequency.
}

// ---- Frame loop: modes + per-frame hold (plans/nonblocking-render.md) ----
// Every renderer is a step function: called when its hold expires, draws the
// next frame, holds it, returns true when finished. loop() samples the mic
// between frames.
Mode g_mode = MODE_IDLE;
volatile Mode g_baseMode = MODE_IDLE;  // where ANIM returns; volatile: web reads it
bool g_drawNew = false;                // render-local: draw buffer needs repainting
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
  int sc = H / 16;               // 16 px layout, pixel-doubled on taller walls
  char lb[2] = {label, 0};
  drawText((W - textWidth(lb, sc)) / 2, 1 * sc, lb, g_set.labelColor, sc);
  char nb[8];
  if (known) numToStr(val, nb); else strcpy(nb, "--");
  drawText((W - textWidth(nb, sc)) / 2, 9 * sc, nb, g_set.textColor, sc);
  FastLED.show();
}

// Idle: sweep the four counters, statMs each, so every number is readable.
// Never done — runs until a detection switches the mode.
int g_idlePage = 0;
void stepIdle() {
  bool known = timeKnown();
  switch (g_idlePage) {
    case 0: drawStatPage('H', lastHourCount(), true);  break;  // last hour (uptime-based, no clock needed)
    case 1: drawStatPage('T', g_today,         known); break;  // today
    case 2: drawStatPage('E', g_yest,          known); break;  // yesterday
    case 3: drawStatPage('Y', g_total,         true ); break;  // total (always known)
  }
  hold(g_set.statMs);
  g_idlePage = (g_idlePage + 1) % 4;
}

// Draw: show the last browser-posted frame; repaint only when a new one
// arrived (or the mode was re-entered). The buffer survives detection
// animations, so the drawing comes back after them.
void stepDraw() {
  if (!g_drawNew) { hold(50); return; }
  g_drawNew = false;
  xSemaphoreTake(g_mux, portMAX_DELAY);   // web may be writing the next frame
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      const uint8_t *p = &g_drawBuf[(y * W + x) * 3];
      leds[XY(x, y)] = CRGB(p[0], p[1], p[2]);
    }
  xSemaphoreGive(g_mux);
  FastLED.show();
  hold(50);
}

// Clock: HH:MM centered, colon blinking at 1 Hz, "--:--" until NTP lands.
// Fixed layout so the blink can't shift the digits: every digit (and '-')
// is 3 px wide, ':' is 1 px, gaps 1 px -> offsets HH=0, colon=8, MM=10,
// total 17 px per scale unit.
void stepClock() {
  char hh[3] = "--", mm[3] = "--";
  if (timeKnown()) {
    time_t tt = time(nullptr); struct tm lt; localtime_r(&tt, &lt);
    snprintf(hh, 3, "%02d", lt.tm_hour);
    snprintf(mm, 3, "%02d", lt.tm_min);
  }
  int sc = fitScale("00:00");
  int x = (W - 17 * sc) / 2;
  int y = (H - FONT_H * sc) / 2;
  FastLED.clear();
  drawText(x, y, hh, g_set.textColor, sc);
  if ((millis() / 500) & 1) drawChar(x + 8 * sc, y, ':', g_set.textColor, sc);
  drawText(x + 10 * sc, y, mm, g_set.textColor, sc);
  FastLED.show();
  hold(500);
}

// Switch the visible renderer; every mode restarts cleanly.
void enterMode(Mode m) {
  g_mode = m;
  g_holdUntil = 0;
  g_idlePage = 0;
  g_drawNew = true;      // draw repaints its buffer when (re-)entered
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
  if (g_ph == 0) drawFish(-FISH_W + g_i, y, g_set.fishColor, true);
  else           drawFish(W - g_i,       y, g_set.fishColor, false);
  FastLED.show(); hold(45);
  if (++g_i > span) { g_i = 0; g_ph++; }
  return false;
}

// 2. "SIIKA" blinks 3x at 0.5 s: g_i counts half-cycles, even = lit.
bool stepSiikaTriple() {
  if (g_i >= 6) return true;
  FastLED.clear();
  if (!(g_i & 1)) drawCentered("SIIKA", g_set.textColor, fitScale("SIIKA"));
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
  drawCentered(words[g_i % 3], g_set.textColor, fitScale("SIIKA"));
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
    drawCentered(one, g_set.textColor, fitScale("A"));   // as big as the height allows
    FastLED.show(); hold(300);
    if (++g_i >= 5) { g_ph = 1; g_i = 0; }
  } else {
    if (!(g_i & 1)) drawCentered("SIIKA", g_set.textColor, fitScale("SIIKA"));
    FastLED.show(); hold(250);
    g_i++;
  }
  return false;
}

// Milestone: every 10th catch gets a sparkle/rainbow burst instead of rotation.
bool stepMilestone() {
  if (g_i >= 60) return true;
  FastLED.clear();
  for (int i = 0; i < 12 * g_set.panelsX * g_set.panelsY; i++)   // same density per panel
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
  if (v >= g_set.loudLevel) { if (g_loudScore < g_set.loudMinSamples) g_loudScore++; }
  else                      { if (g_loudScore > 0)                    g_loudScore--; }
  if (g_loudScore >= g_set.loudMinSamples) {    // sustained loud right now
    bool newSiika = millis() - g_lastLoudMs >= g_set.quietGapMs;
    g_lastLoudMs = millis();
    if (newSiika) {
      if (g_pending < PENDING_MAX) g_pending++;
      Serial.printf("mic TRIGGER peak=%d pending=%u\n", g_micPeak, g_pending);
    }
  }
  if (g_mode != MODE_ANIM && millis() - g_micPrintMs >= 2000) {
    Serial.printf("mic peak=%d n=%lu\n", g_micPeak, (unsigned long)g_micN);
    g_micPrintMs = millis(); g_micPeak = 0; g_micN = 0;
  }
}

bool takeDetection() {
  if (!g_pending) return false;
  g_pending--;
  return true;
}

// ---- Web server: own FreeRTOS task on core 0 (plans/web-and-settings.md) ----
// Built-in blocking WebServer — single connection is fine for a one-user LAN
// admin UI. The UI itself lives in ui.h (single PROGMEM index.html).
WebServer server(80);
bool g_otaAuthed = false;

// Called at the top of every render pass: applies whatever the web task left
// behind. Volatile peek first, so the mutex is only taken when there is work.
void applyWebInput() {
  if (!g_setPending && !g_webTrigger && !g_modePending && !g_drawPending) return;
  xSemaphoreTake(g_mux, portMAX_DELAY);
  if (g_setPending) {
    Settings s = g_pendingSet;
    s.panelsX = g_set.panelsX;          // grid is fixed for this boot; the new
    s.panelsY = g_set.panelsY;          // values wait in NVS for the next one
    g_set = s;
    g_setPending = false;
    FastLED.setBrightness(g_set.brightness);
  }
  if (g_webTrigger) {
    g_webTrigger = false;
    if (g_pending < PENDING_MAX) g_pending++;
  }
  if (g_modePending) {
    g_modePending = false;
    g_baseMode = g_pendingMode;
    if (g_mode != MODE_ANIM) enterMode(g_baseMode);   // anim finishes, then returns
  }
  if (g_drawPending) {
    g_drawPending = false;
    g_drawNew = true;
  }
  xSemaphoreGive(g_mux);
}

const char* modeName(Mode m) {
  switch (m) {
    case MODE_ANIM:  return "anim";
    case MODE_DRAW:  return "draw";
    case MODE_CLOCK: return "clock";
    default:         return "idle";
  }
}

void handleStatus() {
  JsonDocument d;
  xSemaphoreTake(g_mux, portMAX_DELAY);
  d["total"]    = g_total;
  d["today"]    = g_today;
  d["yest"]     = g_yest;
  d["lastHour"] = lastHourCount();
  d["mode"]     = modeName(g_mode);
  d["baseMode"] = modeName(g_baseMode);
  d["pending"]  = g_pending;
  xSemaphoreGive(g_mux);
  d["timeKnown"] = timeKnown();
  d["uptimeS"]   = millis() / 1000;
  d["heap"]      = ESP.getFreeHeap();
  d["rssi"]      = WiFi.RSSI();
  String out; serializeJson(d, out);
  server.send(200, "application/json", out);
}

void handleGetSettings() {
  xSemaphoreTake(g_mux, portMAX_DELAY);
  Settings s = g_set;
  xSemaphoreGive(g_mux);
  String out; settingsJson(s, out);
  server.send(200, "application/json", out);
}

void handlePostSettings() {
  xSemaphoreTake(g_mux, portMAX_DELAY);
  Settings s = g_set;
  xSemaphoreGive(g_mux);
  String err;
  if (!settingsFromJson(server.arg("plain"), s, err)) {
    server.send(400, "text/plain", err + "\n");
    return;
  }
  saveSettings(s);                      // NVS write outside the mutex
  xSemaphoreTake(g_mux, portMAX_DELAY);
  bool reboot = s.panelsX != g_set.panelsX || s.panelsY != g_set.panelsY;
  g_pendingSet = s;
  g_setPending = true;
  xSemaphoreGive(g_mux);
  server.send(200, "application/json",
              reboot ? "{\"ok\":true,\"rebootNeeded\":true}" : "{\"ok\":true}");
}

void handleTrigger() {
  g_webTrigger = true;
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleMode() {
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) { server.send(400, "text/plain", "bad json\n"); return; }
  const char *m = d["mode"] | "";
  Mode nm;
  if      (!strcmp(m, "idle"))  nm = MODE_IDLE;
  else if (!strcmp(m, "draw"))  nm = MODE_DRAW;
  else if (!strcmp(m, "clock")) nm = MODE_CLOCK;
  else { server.send(400, "text/plain", "mode: idle|draw|clock\n"); return; }
  g_pendingMode = nm;
  g_modePending = true;
  server.send(200, "application/json", "{\"ok\":true}");
}

// Body: W*H pixels as RRGGBB hex, row-major logical (x,y). Hex, not raw
// binary: WebServer's arg("plain") truncates at the first 0x00 byte.
void handleDraw() {
  const String &body = server.arg("plain");
  int need = NUM_LEDS * 6;
  if ((int)body.length() != need) {
    server.send(400, "text/plain", String("want ") + need + " hex chars\n");
    return;
  }
  xSemaphoreTake(g_mux, portMAX_DELAY);
  for (int i = 0; i < NUM_LEDS * 3; i++) {
    char h[3] = {body[i * 2], body[i * 2 + 1], 0};
    g_drawBuf[i] = strtoul(h, nullptr, 16);
  }
  g_drawPending = true;
  xSemaphoreGive(g_mux);
  if (g_baseMode != MODE_DRAW) { g_pendingMode = MODE_DRAW; g_modePending = true; }
  server.send(200, "application/json", "{\"ok\":true}");
}

// OTA upload (Update.h behind Basic auth). Render freezes via g_otaActive
// during the flash write; success reboots into the new image.
void handleUpdateUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    g_otaAuthed = server.authenticate("siika", OTA_PASS);
    if (!g_otaAuthed) return;
    g_otaActive = true;
    Serial.printf("OTA start: %s\n", up.filename.c_str());
    Update.begin();
  } else if (!g_otaAuthed) {
    return;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    Update.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    Update.end(true);
    Serial.printf("OTA end: %u bytes, %s\n", up.totalSize,
                  Update.hasError() ? Update.errorString() : "OK");
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    g_otaActive = false;
  }
}

void handleUpdateDone() {
  if (!g_otaAuthed) { g_otaActive = false; return server.requestAuthentication(); }
  bool ok = !Update.hasError();
  server.send(ok ? 200 : 500, "text/plain",
              ok ? "OK, rebooting\n" : String(Update.errorString()) + "\n");
  if (ok) { delay(500); ESP.restart(); }
  g_otaActive = false;
}

void webTask(void *) {
  for (;;) { server.handleClient(); delay(2); }
}

void setupWeb() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/status",   HTTP_GET,  handleStatus);
  server.on("/api/settings", HTTP_GET,  handleGetSettings);
  server.on("/api/settings", HTTP_POST, handlePostSettings);
  server.on("/api/trigger",  HTTP_POST, handleTrigger);
  server.on("/api/mode",     HTTP_POST, handleMode);
  server.on("/api/draw",     HTTP_POST, handleDraw);
  server.on("/update",       HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.onNotFound([]() { server.send(404, "text/plain", "not found\n"); });
  server.begin();
  xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, nullptr, 0);
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
  g_mux = xSemaphoreCreateMutex();
  loadCounters();                // opens prefs; settings share the namespace
  loadSettings();
  W = g_set.panelsX * PANEL_W;   // grid is fixed for this boot
  H = g_set.panelsY * PANEL_H;
  NUM_LEDS = W * H;
  Serial.printf("\nSiikapaneeli — %dx%d panels (%dx%d px), GPIO16+17, TEST_MODE=%d\n",
                g_set.panelsX, g_set.panelsY, W, H, TEST_MODE);
  if (g_set.panelsY == 2) {      // one chain per row, half of leds[] each
    int half = NUM_LEDS / 2;
    FastLED.addLeds<WS2812B, DATA_PIN_A, GRB>(leds, 0, half);
    FastLED.addLeds<WS2812B, DATA_PIN_B, GRB>(leds, half, half);
  } else {                       // single-row rigs: everything on pin A
    FastLED.addLeds<WS2812B, DATA_PIN_A, GRB>(leds, NUM_LEDS);
  }
  FastLED.setBrightness(g_set.brightness);
  // One panel group (3 panels) is fused at 5 A — the limiter must stay under
  // the fuse. Revisit for the 12-panel wall: 4 groups, 5 A fuse each, and a
  // global limiter can't enforce per-group caps (see multi-panel-power.md).
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 5000);
  FastLED.clear();
  FastLED.show();

  selfTest();
  randomSeed(micros());
#if !TEST_MODE
  setupWifi();                   // test rig boots fast, no WiFi/web
  setupWeb();
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
  if (g_otaActive) { delay(10); return; }      // flash write in progress: frame
                                               // freezes, mic pauses, no show()
  applyWebInput();
  micSample();
  if (g_mode != MODE_ANIM && takeDetection()) {   // any base mode; reacts within one pass
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
        enterMode(g_baseMode);                 // back to whatever the wall was showing
      }
    }
  } else if (g_mode == MODE_DRAW) {
    stepDraw();
  } else if (g_mode == MODE_CLOCK) {
    stepClock();
  } else {
    stepIdle();
  }
#endif
}
