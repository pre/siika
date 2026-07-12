# Animation System Plan

Goal: a trigger-driven animation engine that plays animations in rotation, is
written for the full multi-panel canvas, and still shows something useful on the
1-panel prototype. Panel count is configurable so the same code scales from 1 to
12 panels with no rewrite.

Builds on the proven prototype (see `prototype-phase.md`): Adafruit NeoPixel,
GPIO16, 256 LEDs, GRB, serpentine `XY()`, brightness 12, USB power.

## Fitting text on 1 panel

A single panel is 16 px wide. With a **compact proportional font** (letters 3 px
wide, `I` only 1 px, 1 px gaps) each single word fits:

| Word  | Width |
|-------|-------|
| SIIKA | 15 px |
| OTA   | 11 px |
| POIS  | 13 px |

So single words blink in place on one panel. Only a whole multi-word phrase like
"OTA SIIKA POIS" (~43 px) needs several panels.

Prototype answer (per user): give the prototype its own single-panel-friendly
animations. Whole-phrase blinking waits for the full wall; on one panel we page
one word at a time. The engine (canvas config, mapping, registry, trigger) is
identical — only the concrete animation content differs, and swapping animations
is one array edit.

## Architecture

### Canvas config (the scalability knob)

```c
#define PANELS_X 1      // panels across  (prototype 1, final 5-6)
#define PANELS_Y 1      // panels stacked (prototype 1, final 2)
#define PANEL_W  16
#define PANEL_H  16
#define W  (PANELS_X * PANEL_W)      // logical canvas width
#define H  (PANELS_Y * PANEL_H)      // logical canvas height
#define NUM_LEDS (W * H)
```

Animations draw on the logical W x H canvas and never think about panels.
Changing `PANELS_X/Y` is the only edit needed to scale up.

### XY mapping (logical pixel -> LED index in the chain)

Generalises the current single-panel `XY()`:

1. panel column `px = x / PANEL_W`, panel row `py = y / PANEL_H`
2. local coords `lx = x % PANEL_W`, `ly = y % PANEL_H`
3. `panelIndex(px, py)` = panel's position in the data chain
4. `localIndex(lx, ly)` = existing per-panel serpentine
5. LED index = `panelIndex * (PANEL_W*PANEL_H) + localIndex`

For 1 panel this collapses to today's `XY()`. `panelIndex()` and the local
serpentine are **calibration knobs** — the exact chain order and per-panel flip
get fixed when real panels arrive, same as we flipped `SERPENTINE` in the test
sketch. Not solved now (we have no panels to test against).

### Drawing primitives

- `setPx(x, y, color)` — bounds-checked write via `XY()`
- `drawChar(x, y, ch, color)`, `drawText(x, y, str, color)`, `textWidth(str)`
- `showText(str, color, period, times)` — blink-if-fits / scroll-otherwise
- `drawFish(x, y, color, facingRight)` — bitmap, mirrored by facing

### Font

Compact **proportional** bitmap font, 5 px tall, **only the glyphs actually
used**: S I K A O T P + space (7 glyphs). Most letters 3 px wide, `I` is 1 px so
5-letter words fit a 16 px panel (see table above). Each glyph stores its own
width. Unknown chars render blank. Vertically centred on the canvas.
ponytail: hand-define 7 glyphs, not a full ASCII font — add letters when a new
animation needs them.

### Fish

One small pixel bitmap (~9x7: body + tail + eye), drawn from a byte array,
mirrored horizontally for the return pass. ponytail: a single fixed sprite, not
a parameterised fish — one bitmap covers both directions via mirroring.

### Animation registry + rotation

```c
typedef void (*Animation)();
Animation animations[] = { animSiika, animOtaSiikaPois };
const int NUM_ANIMS = sizeof(animations) / sizeof(animations[0]);
int idx = 0;
```

Adding an animation = write one function, add one array entry. Rotation is
`idx = (idx + 1) % NUM_ANIMS`.

### Trigger + main loop

Animations are **blocking** (each runs its own delays to completion, like the
test sketch). Nothing else needs to run during an animation in the prototype, so
a state machine would be premature.

```c
void waitForTrigger() {
  // ponytail: prototype trigger = fixed 2 s wait. This function is the single
  // swap point — replace the body with the real sensor read later.
  delay(2000);
}

void loop() {
  waitForTrigger();
  animations[idx]();
  idx = (idx + 1) % NUM_ANIMS;
}
```

## The two animations (prototype, single panel)

### Animation 1 — SIIKA

1. Blink "SIIKA" 5 times, 0.5 s period (0.25 s on / 0.25 s off), centred.
   "SIIKA" fits (15 px), so it blinks in place as originally specced.
2. Fish swims left -> right across the canvas and exits.
3. "SIIKA" appears (~1 s).
4. Fish swims right -> left (mirrored) and exits.

### Animation 2 — OTA / SIIKA / POIS (word paging)

Show one word at a time, 0.5 s each: OTA -> SIIKA -> POIS. Repeat the 3-word
cycle 5 times (~7.5 s total; count is tunable). This replaces whole-phrase
blinking, which needs the multi-panel wall.

### Full-wall variants (later)

On the full wall the whole phrases fit, so these can become static blinking of
"SIIKA" and "OTA SIIKA POIS" as first described. Add them as new registry
entries (or branch on `W`) when panels exist — not built now (YAGNI).

Remaining animations: designed later, just add functions + registry entries.

## Colors (defaults, easily changed)

Text cyan-white; fish warm orange. Kept dim by the brightness-12 cap until the
MEAN WELL PSU is wired.

## Files

- New sketch `firmware/siika/siika.ino` — the show. Keep the font + fish + anims
  in this one file to start (~200 lines); split into headers only if it grows.
- `firmware/panel_test/panel_test.ino` — untouched, stays as the diagnostic.
- Same toolchain: `arduino-cli`, `esp32:esp32:d1_uno32`, `UploadSpeed=460800`,
  port `/dev/cu.usbserial-110` (re-check the live name before upload).

## Out of scope

- Real trigger (sensor) — placeholder 2 s timer for now.
- Multi-panel chain calibration — knobs in place, tuned when panels exist.
- Power beyond the USB brightness cap — waits for the MEAN WELL PSU.
```
