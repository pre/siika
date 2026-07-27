# Phase 2: WiFi-on + web task + settings in NVS + OTA

Phase 2 of plans/architecture.md, built on the phase-1 frame loop
(plans/nonblocking-render.md). Delivers the plumbing: WiFi stays on,
`http://siika.local` answers, knobs live in NVS-backed settings, firmware
updates over the air. The real UI pages (draw, presets, clock, status page)
are phase 3 — phase 2's `GET /` is a minimal placeholder.

## WiFi always on

- `setupTime()` → `setupWifi()`: STA mode, connect (keep the 8 s boot wait —
  mDNS + server need the link), `WiFi.setAutoReconnect(true)`, then
  `configTzTime(...)` and **no NTP wait loop** — SNTP lands whenever it lands
  and `timeKnown()` flips; esp32 SNTP re-syncs periodically for free.
- The sync-then-off trick and its ponytail comment die (architecture.md:
  flicker mitigation is now the core split + RMT hardware buffering).
- mDNS: `MDNS.begin("siika")` + http service → `http://siika.local`.

## Web task (core 0)

`xTaskCreatePinnedToCore(webTask, "web", 8192, ..., core 0)`: a loop of
`server.handleClient()` + `delay(2)`. Built-in `WebServer.h`, single blocking
connection — fine for a one-user LAN admin UI (arch decision).

**The rule (from architecture.md):** the web task never touches `leds[]` or
FastLED. One FreeRTOS mutex guards the crossing:

```c
Settings g_pendingSet; bool g_setPending;   // web -> render: apply between frames
bool g_webTrigger;                          // web -> render: fake one siika
bool g_otaActive;                           // web -> render: stop rendering
```

The render loop applies pending items at the top of each pass (<1 ms latency,
the phase-1 payoff). Status reads snapshot the counters under the same mutex.
During OTA the render task goes dark and stops touching FastLED; on success
`ESP.restart()`.

## Settings in NVS (ArduinoJson 7.4.3, already installed)

One JSON blob in the existing `Preferences` namespace (`siika`, key `set`).
Current `#define`s become the first-boot defaults. A `Settings` struct in RAM
is the single source of truth for the render task; missing JSON keys keep
their defaults, values are clamped at the trust boundary (POST).

| Setting                     | Today                    | Apply        |
|-----------------------------|--------------------------|--------------|
| `brightness`                | `BRIGHTNESS 64`          | live         |
| `loudLevel`                 | `LOUD_LEVEL 250`         | live         |
| `loudMinSamples`            | `LOUD_MIN_SAMPLES 200`   | live         |
| `quietGapMs`                | `QUIET_GAP_MS 1250`      | live         |
| `statMs`                    | `STAT_MS 2000`           | live         |
| `textColor/labelColor/fishColor` | `CRGB` constants    | live         |
| `serpentine`, `firstRowRev` | `#define`s               | live         |
| `panelsX`, `panelsY`        | `PANELS_X/Y`             | next boot    |

- `leds[]` statically sized for the 12-panel max (3072 = 9.2 KB, per arch);
  `W`, `H`, `NUM_LEDS` become runtime ints derived from settings.
  `addLeds` gets the boot-time LED count → grid size applies on reboot,
  everything else immediately.
- Stays compiled: `DATA_PIN` (FastLED template param), `PANEL_W/H`, `MIC_PIN`,
  `PENDING_MAX`, `TZ_HELSINKI`, the 90° rotation in `localIndex`, and the
  **power limiter (5 A — the per-group fuse limit) — safety, not taste**.
- Deferred with the arch's blessing: per-panel chain/rotation config, the
  XY lookup table, chain→GPIO map — real need arrives with the 12-panel wall,
  and `panelIndex()` stays the placeholder until then.

## HTTP API (phase-2 subset of the arch sketch)

- `GET /` → minimal placeholder page: live status values + OTA upload form
- `GET /api/status` → `{total, today, yest, lastHour, mode, uptimeS, heap, rssi, timeKnown}`
- `GET /api/settings` → current settings JSON; `POST` validates, clamps,
  saves blob to NVS, flags the render task
- `POST /api/trigger` → fake a siika (testing without shouting)
- `POST /update` → OTA via `Update.h`, guarded by HTTP Basic auth with
  `OTA_PASS` from secrets.h (added to secrets.h + secrets.h.example)

Everything else in the arch sketch (`/api/mode`, `/api/draw`, `/api/presets`)
is phase 3.

## Verification

1. TEST_MODE=0, USB upload. Serial shows IP; `http://siika.local` answers.
2. `curl` status + settings; POST brightness → visible change without a
   flicker or animation stutter; POST trigger → animation starts instantly.
3. Shout test: mic trigger + calibration prints unchanged.
4. OTA: `arduino-cli compile --export-binaries`, curl the .bin to `/update`
   → reboots into new build, counters NVS-intact.
5. Flicker watch: spam `/api/status` during an animation — RMT + WiFi
   coexistence check (fallback if it glitches: FastLED I2S driver, per arch).
6. Commit after checks pass.
