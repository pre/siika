# Loudness Trigger — interim mic trigger (placeholder for voice trigger)

Replace the always-true placeholder trigger in `firmware/siika/siika.ino` with
the Particle Sensor Kit loudness sensor: a loud sound (clap, shout) triggers
the detection animation. This is an interim stand-in until the XIAO ESP32-S3
Sense arrives and the real "siika" keyword spotting is built
(see voice-trigger.md). No listener board yet — the sensor wires straight
into the panel Wemos.

## Hardware

Particle Sensor Kit sound/loudness sensor (electret mic breakout, analog out):

| Sensor pin | Wemos D1 R32 |
|---|---|
| VCC | 3.3V (keeps AOUT inside ADC range) |
| GND | GND |
| AOUT | GPIO34 (labeled A3 on the board) |

GPIO34 = ADC1_CH6: input-only, and ADC1 does not conflict with WiFi
(setup uses WiFi briefly for NTP).

## Detection

Peak-to-peak amplitude over the counter sweep delays. Each `delay(STAT_MS)`
in `showCounterSweep()` becomes `listenDelay(STAT_MS)`: same wait, but
sampling the ADC and latching `g_heard` the moment peak-to-peak ≥
`LOUD_PP_THRESHOLD` (early exit + a latched-guard so a clap skips the rest of
the sweep and reacts instantly). `siikaDetected()` consumes the latch — it
stays the single swap point the listener-board GPIO replaces later. The
always-true placeholder body is deleted.

- Panel is deaf during animations → natural cooldown, no extra code.
- `LOUD_PP_THRESHOLD` is the calibration knob; each window's peak-to-peak is
  printed to serial for tuning.

## Success criteria

Clap or shout near the sensor → animation plays. Normal room ambience → panel
stays in the idle sweep.
