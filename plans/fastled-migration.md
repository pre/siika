# FastLED migration

Status: IMPLEMENTED 2026-07-13 — compiled, uploaded, selfTest OK, counters
survived NVS-intact; mic trigger + animation visuals await a human shout/eyeball.

## Why

12 panels on one data line = 3072 LEDs × 30 µs = 92 ms per frame → ~10 fps.
The fix is multiple data lines transmitting **in parallel**. Adafruit
NeoPixel's `show()` blocks per strip, so N strips still cost N × the same
time. FastLED drives multiple `addLeds` pins on separate RMT channels
concurrently in one `show()` — 4 lines × 768 LEDs → ~23 ms → ~43 fps
(4 groups of 3 panels, per panel-mounting.md).

The old "NOT FastLED" rule (prototype-phase.md, CLAUDE.md) was circumstantial:
Adafruit NeoPixel happened to be installed when PlatformIO was dropped.
Decision 2026-07-13: switch to FastLED (≥ 3.9 for ESP32 core 3.x RMT support).

## Scope

- **In**: rewrite `firmware/siika/siika.ino` on FastLED, same behavior,
  single panel, USB power. Update the CLAUDE.md toolchain line.
- **Out**: multi-line wiring (needs the MEAN WELL PSU + 12 panels), the
  3×4-panel `XY()` chain mapping, raising brightness. Own plan when the
  hardware arrives.
- `firmware/panel_test/panel_test.ino` stays untouched (diagnostics, frozen).

## Code changes (mechanical mapping)

| Adafruit NeoPixel | FastLED |
|---|---|
| `Adafruit_NeoPixel strip(NUM_LEDS, DATA_PIN, NEO_GRB+NEO_KHZ800)` | `CRGB leds[NUM_LEDS]; FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS)` |
| `strip.begin()` | (not needed) |
| `strip.setBrightness(12)` | `FastLED.setBrightness(12)` |
| — | `FastLED.setMaxPowerInVoltsAndMilliamps(5, 400)` — hard USB cap, new safety net |
| `strip.clear()` / `strip.show()` | `FastLED.clear()` / `FastLED.show()` |
| `strip.setPixelColor(XY(x,y), c)` | `leds[XY(x,y)] = c` |
| `uint32_t` colors, `Color(r,g,b)` | `CRGB` colors, `CRGB(r,g,b)` |
| `ColorHSV(random(65536))` | `CHSV(random8(), 255, 255)` |

Everything else — XY mapping, font, animations, counters, NVS, NTP, mic
trigger, selfTest — is untouched. `setPx()` signature changes to `CRGB`.

Multi-line readiness: later, 4 lines (one per group of 3 panels) become 4
`addLeds` calls slicing the same `leds[]` array (`addLeds<WS2812B, PIN_A,
GRB>(leds, 0, 768)` …), and `panelIndex()` maps panels to per-line offsets.
No structural prep needed now — that is the whole point of FastLED's
shared-buffer model.

## Steps

1. `arduino-cli lib install FastLED` (verify version ≥ 3.9).
2. Rewrite siika.ino per the table above.
3. Compile, upload (460800 baud), monitor: `selfTest OK`, counter sweep,
   mic trigger, all four animations + milestone.
4. Update CLAUDE.md: "Adafruit NeoPixel -kirjasto (ei FastLED/PlatformIO)" →
   "FastLED-kirjasto (≥3.9, ei PlatformIO)".

## Risks

- FastLED RMT vs. WiFi interrupts can flicker — mitigated already: WiFi is
  off after the boot-time NTP sync.
- ESP32 core version too old for FastLED 3.9 RMT5 driver → check
  `arduino-cli core list` at step 1; upgrade core if compile fails on RMT.
- Power cap (400 mA) may dim frames the old code showed at full brightness
  12 — unlikely (animations light few pixels), verify visually.
