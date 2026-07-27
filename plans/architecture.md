# Architecture: web-controlled Siikapaneeli

Target state for the 12-panel wall: browser UI at `http://siika.local` for
drawing, effects, clock, and settings; flicker-free rendering while the UI is
used; panel layout as configuration; OTA updates. Decisions locked with the
user 2026-07-27:

1. **Own firmware**, built on the current `siika.ino` — not WLED/usermod.
2. **Effects are compiled code, parameters are data** (WLED's actual model).
   A new effect ships via OTA; everything tunable ships via settings/presets.
3. **Siika core stays compiled**: mic trigger, queue, NVS counters, idle/detected
   state machine. Its calibration knobs (LOUD_LEVEL, QUIET_GAP_MS, colors,
   STAT_MS, brightness…) move from `#define`s to settings.

## Guiding principle

Built-in ESP32 Arduino libraries only, plus ArduinoJson. No filesystem, no
async web stack, no build-step asset pipeline:

| Need        | Choice                                            | Rejected alternative |
|-------------|---------------------------------------------------|----------------------|
| Web server  | `WebServer.h` (built-in), own FreeRTOS task, core 0 | ESPAsyncWebServer + AsyncTCP deps |
| UI assets   | Single `ui.h`, raw-string HTML in PROGMEM         | LittleFS + mklittlefs upload toolchain |
| Settings    | JSON blob in NVS (`Preferences`), <4 KB           | LittleFS settings.json |
| Presets     | JSON blob in NVS                                  | Filesystem |
| OTA         | `Update.h` behind `POST /update`                  | ElegantOTA dependency |
| mDNS        | `ESPmDNS.h` (built-in) → `siika.local`            | — |
| JSON        | ArduinoJson (the one new library)                 | hand-rolled parsing |

UI is one hand-written `index.html` (vanilla JS, no framework) embedded as a
raw string literal — edit in place, ships inside every OTA image, so firmware
and UI can never drift apart.

## Concurrency model

Arduino-ESP32 already pins the WiFi/IP stack to core 0 and `loop()` to core 1.
We add one task and one rule:

- **Core 1, `loop()` (render task):** fixed-tick frame loop (~50 fps for one
  panel; see "Frame rate at 12 panels"). Renders the current mode, calls
  `FastLED.show()`, samples the mic while waiting for the next frame — the
  same continuous ADC sampling as today's `listenDelay`, so the calibrated
  leaky-score trigger is untouched.
- **Core 0, web task:** `xTaskCreatePinnedToCore` running
  `server.handleClient()` + mDNS. Single-connection blocking server is fine
  for a one-user LAN admin UI.
- **The rule:** the web task never touches `leds[]` or FastLED. All
  communication goes through one mutex-guarded shared struct: pending
  settings, pending draw frame, mode-change command, and read-only status
  (counters, uptime) going the other way. The render task applies pending
  changes between frames.

WiFi stays ON permanently (the old sync-then-off trick dies). SNTP then
re-syncs the clock for free. FastLED's RMT driver has hardware buffering and
coexists with WiFi on the other core — the exact pattern WLED uses.

## Blocking → frame-based rendering (the real refactor)

Today every animation is a blocking function with delays inside. That must
become per-frame stepping or the web task starves the shared state and mode
changes lag by whole animations:

- Render modes: `IDLE` (stat page sweep), `ANIM` (detection animation),
  `DRAW` (show the browser-posted frame), `CLOCK` (new effect).
- Each animation becomes `begin()` + `step(frameNo) -> done`. The existing
  drawing helpers (font, fish, XY) are already frame-shaped and survive as-is;
  only the outer `for`-loops with `listenDelay` unroll into step functions.
- The detection queue/drain logic in `loop()` keeps its shape, just driven by
  "animation reported done" instead of function return.

## Panels as configuration

- `leds[]` statically sized for the 12-panel max (3072 LEDs = 9.2 KB — fine).
- Settings describe: grid size (panels across/down), and per panel: which
  data chain, position in chain, rotation/flip. Serpentine + first-row flags
  stay as per-install settings.
- On settings-apply, precompute a full `XY -> led index` lookup table
  (uint16_t, 6 KB max). Rendering never recomputes mapping.
- Settings page shows the grid; user assigns chain order and orientation —
  replaces the compile-time `panelIndex()` placeholder.

### Parallel output & frame rate (absorbed fastled-migration.md)

WS2812 is ~30 µs/LED: one 3072-LED chain = ~92 ms per show ≈ 10 fps. The fix
is multiple data lines transmitting in parallel: FastLED drives multiple
`addLeds` pins on separate RMT channels concurrently in one `show()`.
Decided layout (panel-mounting.md): **4 lines, one per group of 3 panels →
4 × 768 LEDs ≈ 23 ms ≈ 43 fps** — and a dead line can't take down the others.

- The FastLED migration itself is DONE (2026-07-13): single panel runs on
  FastLED ≥ 3.9, selfTest OK, counters survived NVS-intact.
- Multi-line needs no structural prep: 4 `addLeds` calls slice the same
  `leds[]` array (`addLeds<WS2812B, PIN_A, GRB>(leds, 0, 768)` …), and
  `panelIndex()` maps panels to per-line offsets — FastLED's shared-buffer
  model is the whole point of the migration.
- **Panel config therefore includes a chain→GPIO map, and the mounting plan
  routes one data wire per chain.**
- Superseded risk from the migration plan: "RMT vs WiFi flicker, mitigated by
  WiFi-off after NTP sync." WiFi now stays on; the mitigation is the core
  split + RMT hardware buffering (WLED's pattern), with FastLED's I2S driver
  as fallback if flicker ever appears.

## HTTP API sketch

- `GET /` → UI from PROGMEM
- `GET/POST /api/settings` → full settings JSON (panels, knobs, colors)
- `GET /api/status` → counters, mode, uptime, heap
- `POST /api/mode` → idle / draw / clock; `POST /api/trigger` → fake a siika
- `GET/POST /api/presets` → effect parameter sets, rotation order
- `POST /api/draw` → raw RGB frame (≤9.2 KB) for draw mode
- `POST /update` → OTA (firmware). Guarded by a fixed password from
  `secrets.h` — LAN-only device, but flashing stays behind auth.

## Phasing (each phase gets its own plan file when implemented)

1. **Non-blocking render refactor.** No WiFi changes, no web. Behavior
   identical on the prototype panel — proves the frame loop and mic sampling
   survived the restructure.
2. **WiFi-on + web task + mDNS + OTA + settings in NVS.** Knobs and panel
   config move to settings; `#define`s become defaults for first boot.
3. **UI features:** effects/presets page, draw page, clock mode, status page.

## Skipped (add only when the need is real)

- Scripting/interpreted effects (Pixelblaze model) — decided against; OTA is
  the path for new effects.
- LittleFS — revisit only if presets outgrow NVS blobs (~4 KB).
- WebSocket live streaming for draw mode — repeated POSTs are fine to start.
- WiFi provisioning portal — `secrets.h` works; the device has one home.
- ESPAsyncWebServer — upgrade path if the blocking server ever feels slow.
