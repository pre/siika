# Phase 3: UI features — draw, clock, real web UI

Phase 3 of plans/architecture.md, on top of the phase-2 web plumbing. Adds
the DRAW and CLOCK render modes, the mode-switch API, and replaces the
placeholder page with the real single-file UI.

## New render modes

`ANIM` becomes an overlay: a detection records the catch and plays its
animation in every mode, then returns to the selected **base mode**
(`g_baseMode`: IDLE | DRAW | CLOCK) instead of always IDLE. The siika core
works identically no matter what the wall happens to be showing.

- **DRAW** — shows the last browser-posted frame. The frame lives in its own
  buffer (`uint8_t g_drawBuf[MAX_LEDS*3]`, 9.2 KB), so it survives detection
  animations and re-renders after them. The step function only redraws when
  a new frame has arrived — no busy re-showing.
- **CLOCK** — HH:MM in the existing font (new `:` glyph, 1 px wide), centered
  at `fitScale`, colon blinking at 1 Hz. `"--:--"` until `timeKnown()`.
  Local time, DST handled by the existing TZ setup.

## API additions

- `POST /api/mode` `{"mode":"idle"|"draw"|"clock"}` — sets the base mode
  (crosses to the render task via the existing mutex-guarded pending pattern).
- `POST /api/draw` — hex-encoded frame, exactly `W*H*6` chars (RRGGBB per
  pixel, row-major logical (x,y)). Wrong length → 400. Auto-switches base
  mode to DRAW, so painting in the browser just works. Repeated POSTs are
  the streaming model (arch: WebSocket only if this ever feels slow).
  **Deviation from the arch sketch ("raw RGB frame"):** the built-in
  WebServer stores a POST body via `String(buf)`, which truncates at the
  first 0x00 byte — raw binary can't survive `arg("plain")`. Hex doubles
  the payload (18 KB at 12 panels, LAN-trivial) and stays one dumb POST.
- `GET /api/status` grows `baseMode`.

## The real UI (`ui.h`)

The placeholder page moves out of siika.ino into `ui.h` as the arch's single
raw-string PROGMEM `index.html` — hand-written vanilla JS, no framework, no
build step, ships inside every OTA image. One page, four sections:

1. **Status** — the counters as big numbers (H/T/E/Y with real labels), mode,
   uptime, heap, RSSI, WiFi; auto-refresh every 2 s; SIIKA! trigger button.
2. **Mode** — idle / draw / clock buttons showing the active one.
3. **Draw** — a W×H pixel grid canvas: click/drag to paint, color picker,
   clear button. Sends the frame (fetch, ArrayBuffer) on pointer-up —
   the panel is the preview.
4. **Settings** — form for every settings field (colors as
   `<input type=color>`, native), Save posts the JSON; shows "reboot needed"
   when the grid size changed. OTA form stays at the bottom.

## Deliberately not in this phase (add when the need is real)

- **Presets** (`/api/presets`, parameter banks, rotation order): today's
  animations have no per-effect parameters worth banking — settings already
  cover colors and timing. Add when a real parameterized effect exists.
  (This trims the arch's phase-3 list; veto if you want presets now.)
- Draw-mode persistence: the drawn frame and the selected mode are RAM-only;
  every boot starts in IDLE with a black draw buffer.
- WebSocket streaming for draw (arch: repeated POSTs first).
- API auth: only `/update` is behind a password. The rest of the API is
  open on the LAN — one-user home network, per the arch.
- Draw extras: no image upload, no fill/line tools, no animation frames —
  a pixel pen, a color picker and clear.
- Clock extras: no seconds, no date, no own color setting (uses textColor),
  no styles. HH:MM is the feature.
- Settings form is generated key→input with no client-side validation —
  the firmware clamps at the trust boundary, the form just POSTs.
- Draw-POST hex payload is length-checked but not hex-validated; invalid
  chars become garbage pixels on your own wall (LAN admin API).

## Verification

1. Clock mode via UI: correct Helsinki time, colon blinks, `--:--` before
   NTP (testable by triggering right after a cold boot).
2. Draw a picture in the browser → appears on the panel; trigger a siika →
   animation plays → drawing comes back.
3. Mode buttons switch live; detection works in every mode and returns to it.
4. Settings edit via UI form (e.g. fish color) → visible next fish.
5. OTA still works from the new page. Commit after checks.
