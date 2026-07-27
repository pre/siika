# Three-Panel Test: scale animations to 1x3, add TEST_MODE

Goal: run the existing detection animations on three side-by-side panels
(48x16 canvas) and add a test mode that plays every animation back-to-back,
bypassing the loudness sensor. All changes in `firmware/siika/siika.ino`.

## Changes

1. **Canvas:** `PANELS_X 1 -> 3` → W=48, NUM_LEDS=768. `panelIndex()` is
   already row-major, correct for a left→right data chain
   (GPIO16 → left panel DIN → ... → right panel). If the chain is wired
   right→left, that knob flips — test mode reveals it immediately.
   **Panel orientation (found on the 1x1 rig):** wiring mounts each panel 90°
   CCW of the logical image, so `localIndex()` pre-rotates every panel's
   16x16 frame 90° CW before the serpentine mapping. The serpentine knobs
   stay as calibrated — they produced a coherent, just-rotated image.

2. **Text auto-scale:** new helper `fitScale(s)` = largest integer scale where
   `textWidth(s, scale) <= W` and `FONT_H * scale <= H`. Animations use it so
   text grows with the canvas instead of floating tiny in the middle:
   - `animSiikaTriple`: "SIIKA" blinks at fitScale → **scale 3** (45x15 px).
   - `animOtaSiikaPois`: words at the min fitScale over the three words →
     uniform **scale 3**.
   - `animBigSiika`: letter-by-letter spell at scale 3 (15 px tall), then the
     word blink also lands at scale 3.
   - Idle stat pages stay at scale 1, centered — unchanged code, still legible.

3. **Milestone sparkles:** 12 lit px → `12 * PANELS_X * PANELS_Y` (36), same
   density as before; the FastLED power limiter remains the safety net.

4. **TEST_MODE** (`#define TEST_MODE 1`, set 0 for normal operation):
   - `listenDelay()` becomes plain `delay()` — mic never read, no triggers.
   - Skip WiFi/NTP in `setup()` — boots in ~1 s for fast iteration.
   - `loop()` plays all 4 animations + milestone in order, forever.

5. **Power cap:** MEAN WELL LRS-150F-5 (5 V / 30 A) has arrived and feeds the
   panels directly. Chosen sizing: `BRIGHTNESS 64` (~25%) with
   `setMaxPowerInVoltsAndMilliamps(5, 6000)` — ~2 A per panel, a 5x margin to
   the PSU and safe on ordinary hookup wire. Raise later once the final 10 AWG
   distribution is in (separate plan). The limiter is content-aware, so
   full-white frames auto-dim instead of browning out.

## Out of scope

- Frame-based rendering, web UI (architecture.md phases).
- Panel rotation/flip config — compile-time knobs suffice for the test rig.
