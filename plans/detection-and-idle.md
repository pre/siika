# Detection Show & Idle Stats Plan

Reframes the loop into **two states** and adds the counting/stats the panel shows
when idle. Builds on the engine, primitives, font and fish in
`animation-system.md` and the GPIO trigger in `voice-trigger.md`. Nothing here
changes the engine; it adds a state machine, a persistent counter, and more
animations.

## The two states

```
        no siika (idle)                 siika detected
   +----------------------+          +----------------------+
   |  cycle stat pages:   |  trigger |  play next detection |
   |  hour/today/yest/all | -------> |  animation, then     |
   |  (loops forever)     | <------- |  return to idle      |
   +----------------------+   done   +----------------------+
```

- **Idle** = "SIIKA not detected": loop the stats display (below).
- **Detected**: record the catch, play the next detection animation, go back to idle.

### Trigger (unchanged swap point)

Same `siikaDetected()` single-swap-point idea as `waitForTrigger()` today.

- **Real** (per `voice-trigger.md`): listener board pulls a GPIO high →
  `digitalRead(TRIGGER_PIN)`.
- **Prototype placeholder**: fire once every 2 s via `millis()` (matches "animation
  changes after a 2 s pause"). During that 2 s the idle stats show, so the panel
  is never blank.

```c
bool siikaDetected() {
  // ponytail: prototype = 2 s timer. Replace the body with digitalRead(TRIGGER_PIN)
  // when the listener board is wired — this is the only line that changes.
  static uint32_t last = 0;
  if (millis() - last >= 2000) { last = millis(); return true; }
  return false;
}

void loop() {
  if (siikaDetected()) {
    recordDetection(nowEpoch());
    playNextDetectionAnim();
  } else {
    showNextIdlePage();          // ~1 s per page, then returns to re-poll
  }
}
```

## Counter + persistence

Four numbers to show: **last hour, today, yesterday, total**. Must survive power
loss.

### Storage: NVS (Preferences library), not a hand-rolled file

You suggested "e.g. a file". On ESP32 the idiomatic equivalent is **NVS via the
`Preferences` library** — flash-backed, wear-levelled, atomic per-key writes, no
mount/format step. It *is* the file, done right. LittleFS is only worth it if we
later need a human-readable log; for a handful of counters, Preferences wins.

### Data model (all in NVS)

| key      | type   | meaning                                            |
|----------|--------|----------------------------------------------------|
| `total`  | u32    | all-time catches                                   |
| `curDay` | i32    | local day-index the daily counters refer to        |
| `today`  | u16    | catches on `curDay`                                |
| `yest`   | u16    | catches the day before `curDay`                    |
| `recent` | u32[N] | ring of recent catch epoch-seconds (last-hour window) |
| `rhead`  | u8     | ring write head                                    |

`recordDetection(t)`:
1. `d = localDay(t)` (epoch seconds → local days). If `d != curDay`:
   `yest = (d == curDay+1) ? today : 0; today = 0; curDay = d;`
   (a gap of >1 day zeroes yesterday — correct after the panel was off a while).
2. `today++; total++; recent[rhead++ % N] = t;`
3. Persist all changed keys.

**Last hour** is computed at display time: count `recent[]` entries `>= t - 3600`.

- `N = 64`. ponytail: last-hour saturates at 64 catches/hour — fine for a fish
  panel. Upgrade path if it ever matters: 60 one-minute buckets instead of a ring.
- Flash wear: one NVS write per catch. Catches are minutes+ apart, so nowhere near
  NVS's ~100k-write endurance. ponytail: no batching; add it only if catches ever
  become high-frequency.

## Wall-clock time — DECIDED: NTP over the Wemos' WiFi (2026-07-12)

"today / yesterday / last hour" need real calendar time; the Wemos only has an
uptime counter that resets on power loss. Chosen source: **NTP over the panel
Wemos' own WiFi** — zero new parts.

- Connect at boot, `configTzTime()` with the Helsinki TZ string
  (`EET-2EEST,M3.5.0/3,M10.5.0/4` — handles DST), sync once via `pool.ntp.org`.
  The chip's internal RTC then keeps time while powered; re-sync on each cold boot.
- **Prerequisite**: WiFi SSID + password for the install site. Hard-code them for
  the prototype (no provisioning UI — out of scope).
- Until the first sync, `total` still shows (always valid); hour/today/yesterday
  show `--`. If a catch fires before time is known, count it into `total` only and
  resume day/hour attribution once synced.
- ponytail: WiFi RF can glitch WS2812B timing — sync early, then the radio is mostly
  idle; revisit only if flicker actually shows.

Rejected: offline-total-only (doesn't meet the stats) and a DS3231 RTC module (a new
part to order, unnecessary once WiFi is available).

## Idle stats display (prototype, 1 panel)

16×16 can't show four labelled numbers legibly at once → **page through them**,
~1 s each, looping: `H` (last hour) → `T` (today) → `E` (yesterday, "eilen") →
`Y` (total, "yhteensä"). Each page = the label glyph small, the number centred.

- **Font extension needed**: add digits `0-9` and label letters `H E Y` (`T`, `S`,
  `I`, `K`, `A`, `O`, `P`, space already exist). ~13 new 3-px glyphs.
- Numbers > panel width (e.g. total = 1000+) → show `999+` on one panel. ponytail:
  the full wall shows all four fields at once, no paging, no cap — add that layout
  when panels exist (`branch on W`), not now.

## Detection animations (the show)

The requested four, plus a few invented ones ("keksitään lisää"). Marked
**[proto]** = buildable now on one panel, **[wall]** = needs the multi-panel wall.

Shared enabler for the size variants: add an integer **`scale`** param to
`drawChar`/`drawText` (pixel-doubling). One param gives small/medium/big text from
the single existing font — no second font. On one 16-px panel, `scale 2` (10 px
tall) is the "big" size.

1. **Fish swim** [proto] — fish crosses L→R and exits, then R→L and exits.
   `swim()` already exists; split it out as its own animation.
2. **SIIKA ×3** [proto] — blink "SIIKA" 3× at 0.5 s (`blinkCentered("SIIKA", …, 3)`).
3. **OTA SIIKA POIS** [proto] — one word at a time, 0.5 s each, cycled; the full
   phrase with a 0.5 s hold between first and last word is the **[wall]** variant
   once "OTA SIIKA POIS" fits across panels.
4. **Size fill → big SIIKA** — [wall] fills the wall with "SIIKA" at mixed `scale`s,
   then one big centre "SIIKA" blinks 5× at 0.5 s. [proto] version: spell `S·I·I·K·A`
   one big letter at a time (`scale 2`), then the whole word blinks 5× at 0.5 s. A
   single `scale-2` letter (6 px) fits 16 px; the whole word at `scale 2` (30 px)
   does not — so on one panel the *word* stays `scale 1`, only the spell-out is big.
5. **Zoom "SIIKA!"** [wall] — ramp `scale` so the whole word grows. Needs the wall:
   "SIIKA" is already ~full width at `scale 1` on one panel, so it can't grow there.
6. **Milestone celebration** [proto] — every 10th `total`, a sparkle/rainbow burst
   instead of the normal rotation. One `if (total % 10 == 0)` branch. (A per-catch
   "tally flash" is dropped for the prototype — the idle pages already show the
   counts; add it later if the reveal wants its own number.)

Rotation is the existing registry: `playNextDetectionAnim()` walks the array and
wraps. Adding an animation stays "one function + one array entry".

## Files

- New plan: this file.
- `firmware/siika/siika.ino` — extend in place: font (digits + `H E Y`), `scale`
  param on the text primitives, the counter module (`Preferences`), the idle-stats
  pages, the state-machine `loop()`, and the new animation functions. Still one
  file; split into headers only if it outgrows ~400 lines.
- `animation-system.md` — its "two animations" section was a starting subset; this
  plan is the fuller catalog. No edit needed unless we want a cross-link.

## Out of scope

- Real mic trigger — `voice-trigger.md` owns it; here it's the 2 s placeholder.
- Multi-panel [wall] variants — knobs exist, built when panels arrive.
- WiFi provisioning UI — hard-code credentials for the prototype if we pick NTP.

## Prerequisite before implementation

- **WiFi SSID + password** for the install site (for the NTP time source above).
  Everything else is decided.
