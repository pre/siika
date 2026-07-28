# Six-panel 2x3 rig (two rows of three, two data pins)

Current rig: 6 panels, 2 stacked x 3 across, 48x32 px. Data on the screw
shield pins **5** and **4**, which on the Wemos D1 R32 map to **GPIO16**
(current DATA_PIN, so the old chain stayed on pin 5) and **GPIO17**.
Assumption: each pin drives one row of 3 panels (768 LEDs per chain).

## Firmware changes (firmware/siika/siika.ino)

1. **Two addLeds slices over the same leds[] array** (the architecture.md
   plan): pin template args are compile-time, so:
   - `addLeds<WS2812B, 16, GRB>(leds, 0, half)` and
     `addLeds<WS2812B, 17, GRB>(leds, half, NUM_LEDS - half)`
     where `half = NUM_LEDS / 2` when `panelsY == 2`.
   - `panelsY == 1` keeps everything on GPIO16 (old 3x1 rig still works).
   - FastLED drives both RMT channels in parallel in one `show()`.

2. **panelIndex stays row-major** — with one chain per row, row-major
   already lands py=0 on the first slice (pin 5) and py=1 on the second
   (pin 4). Two compile-time calibration knobs, fixed against the real rig:
   - `ROWS_SWAPPED` — flips py if pin 4 turns out to be the *top* row.
   - `ROW1_MIRRORED` — flips px on the second row if its chain enters from
     the right instead of the left.

3. **Grid defaults** `PANELS_X 3` / `PANELS_Y 2`. NVS settings override the
   defaults, so after flashing the grid is set to 3x2 in the web UI
   (Settings -> panels -> reboot). No migration code.

4. **Idle stat pages scale**: `drawStatPage` hardcodes scale 1 and y=1/y=9
   (16 px layout). Change to `scale = H / 16` and multiply the y offsets —
   32 px tall canvas gets 2x label + 2x number. Everything else already
   adapts: text via `fitScale`, fish/clock center on W/H, milestone density
   already multiplies by panel count.

## What does NOT change

- Power limiter stays at 5 A global. Two fused 5 A groups could in theory
  take 10 A total, but a global limiter can't stop one row from hogging all
  10 A and blowing its 5 A fuse; 5 A global is the only safe cap
  (multi-panel-power.md). Revisit only with per-group measurement.
- Animations' logic, web API, draw/clock modes: all render through
  `setPx`/`XY`, which absorbs the new geometry.
- `MAX_LEDS` (3072) already covers 1536 LEDs; draw-mode hex body grows to
  9216 chars, well within WebServer limits.

## Test on hardware

1. Flash, set grid 3x2 in web UI, reboot.
2. TEST_MODE=0 idle stats: label and number 2x size, centered.
3. `POST /api/trigger`: fish swims the full 48 px width across both rows —
   any row swap/mirror shows up immediately; fix the two knobs if needed.
4. Draw mode from the web UI: paint corners, verify all four map correctly.
