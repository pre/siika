# Phase 1: Non-blocking render refactor

Phase 1 of plans/architecture.md. No WiFi changes, no web server. Behavior on
the three-panel rig must be visually identical before and after — that is the
whole acceptance test. This restructure is what lets phase 2 add a web task
that can change modes/settings between frames instead of waiting out whole
animations.

## One deviation from architecture.md wording

The architecture says "fixed-tick frame loop (~50 fps)". Current animation
steps hold 30/45/250/300/400/500/2000 ms — none are multiples of a sane fixed
tick, so a fixed tick would audibly/visibly retime every animation (45 ms swim
steps would land on 60 ms ticks: 33 % slower).

Instead: **free-running `loop()` + a per-frame `holdUntil` timestamp.** Every
pass samples the mic once, then steps the current renderer only when the hold
has expired. `FastLED.show()` runs only when a new frame was drawn.

- Timing preserved exactly (each step sets its own hold, same ms values as today).
- Mic sampling profile matches today: near-ADC-speed sampling while a frame
  is held, closed during `show()` — exactly as the blocking version behaves.
- Frame rate is naturally capped by `show()` duration; the parallel-output
  plan (4 lines) keeps that ~23 ms at 12 panels.
- Phase 2's "apply web changes between frames" gets <1 ms latency for free.

## The shape

```c
enum Mode { MODE_IDLE, MODE_ANIM };        // DRAW, CLOCK arrive in phase 3

void loop() {
  micSample();                             // one ADC read + leaky-score trigger
  if (mode == MODE_IDLE && takeDetection())   // instant reaction, like breakOnHit today
    { recordDetection(nowEpoch()); beginAnim(next); mode = MODE_ANIM; }
  if (millis() < holdUntil) return;        // current frame still on display
  bool done = stepCurrentRenderer();       // draws next frame, sets holdUntil, shows
  if (done) { /* ANIM: drain queue (next anim) or back to IDLE */ }
}
```

Each animation converts from "function with delays inside" to a **step
function**: called when its hold expires, draws the next frame, sets the next
hold, returns `true` when finished. Shared state (a couple of ints + phase
counter) is reset by `beginAnim()`; the animation registry becomes an array of
step functions.

## Conversion list

| Today (blocking)            | Becomes                                              |
|-----------------------------|------------------------------------------------------|
| `swim()` loops in `animFishSwim` | step: x position + direction phase, 45 ms hold  |
| `animSiikaTriple` (`blinkCentered` 3x) | step: on/off phase counter, 250 ms holds  |
| `animOtaSiikaPois`          | step: cycle + word index, 500 ms holds               |
| `animBigSiika`              | step: letter index, then 5x blink phase, 300/250 ms  |
| `animMilestone`             | step: frame counter 0..59, 30 ms holds               |
| `showCounterSweep`          | idle renderer: page index 0..3, `STAT_MS` holds, never done |
| `listenDelay(ms, breakOnHit)` | deleted; body becomes `micSample()` (one sample per `loop()` pass) |
| `blinkCentered()` helper    | deleted (its two callers are now step machines)      |

`breakOnHit` disappears: the queue check at the top of `loop()` reacts within
one pass, which is what `breakOnHit=true` bought the idle sweep.

The 2 s calibration serial print (`mic peak=… n=…`) survives as a print every
2 s from `micSample()` while in idle.

## TEST_MODE

Same visible behavior as today: all animations back-to-back forever, 400 ms
blank gap, milestone included, mic and WiFi off. Implemented in the anim-done
transition: instead of draining the detection queue, begin the next animation
in rotation after a 400 ms blank hold.

## Untouched

- Drawing helpers: font, `drawText`/`drawCentered`, `drawFish`, `setPx`, XY
  mapping — already frame-shaped, survive as-is.
- Trigger logic and all calibration knobs (`LOUD_LEVEL`, `LOUD_MIN_SAMPLES`,
  `QUIET_GAP_MS`, `PENDING_MAX`) — the leaky score is already per-sample.
- Counters, NVS, day rollover, last-hour buckets, `selfTest()`.
- WiFi sync-then-off in `setupTime()` — WiFi-always-on is phase 2.

## Verification

1. `selfTest OK` on serial after boot.
2. TEST_MODE=1 upload (`UploadSpeed=460800`, port via
   `ls /dev/cu.* | grep -i usbserial`): user confirms all five animations look
   identical to before (speed, blink rhythm, gaps).
3. TEST_MODE=0 quick check: idle sweep pages at 2 s, a shout triggers the
   animation + counter, shouts during an animation queue replays.
4. Commit only after visual OK.
