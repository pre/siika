# Voice Trigger Plan — detect the word "siika"

Goal: a microphone listens continuously and fires a trigger when someone says
**"siika"**, which then starts a panel animation. Decide the architecture,
the hardware, and the recognition software before buying or coding anything.

## Direct answers to the two questions

**"Does a separate ESP32 have enough power to listen to a mic all the time and
recognize one word?"** — Yes. Keyword spotting for a single wake word is the
one always-on voice task microcontrollers are *built* for. On a dedicated chip,
100 % CPU is fine — listening is its only job (draws ~100–260 mA). The
frameworks below (WakeNet, Porcupine, Edge Impulse) are designed for exactly
this and stay well inside an ESP32's budget.

**The always-on CPU worry is backwards.** It is the argument *for* the
microcontroller, not against it. On a PC, always-on listening keeps the whole
machine awake and steals cycles from everything else. On a dedicated MCU there
is nothing else to steal from. So the "microcontroller is cooler" instinct and
the correct engineering choice point the same way. Recommendation: go MCU.

## Architecture — a separate listener board (as the user guessed)

```
[mic] -> [listener ESP32: keyword spotting "siika"] --trigger--> [panel Wemos: LEDs]
```

- **Keep the listener separate from the panel Wemos.** WS2812B is bit-banged
  and timing-critical; running audio I2S DMA + ML inference on the same chip
  invites LED flicker. A dedicated listener is cheap and keeps the *working*
  panel firmware (`firmware/panel_test`) untouched.
- **Trigger link between the two boards** — simplest first:
  1. One GPIO wire: listener pulls a pin high on "siika", panel reads it. Zero
     protocol, works if the boards sit together. **Start here.**
  2. ESP-NOW (both are ESP32, no WiFi router needed) if they end up apart.

## Hardware — DECIDED: buy the XIAO ESP32-S3 Sense

**Seeed XIAO ESP32-S3 Sense (~15 €) — one purchase, nothing else to buy.**
Edge Impulse (the chosen recognizer) has this as its reference keyword-spotting
board, and its mic is onboard:
- ESP32-**S3** → has the vector instructions Edge Impulse inference wants (the
  current Wemos is a plain ESP32-D0WD, no SIMD).
- 8 MB PSRAM + onboard digital (PDM) microphone → no mic to wire, enough RAM for
  the model + audio buffers, and samples record straight from the EI Studio.

**Only-if-reusing-the-spare-Wemos fallback: Wemos D1 R32 + INMP441 I2S mic
(~3–5 €).** Wire the INMP441 SCK/WS/SD/L-R to 4 GPIOs. Works for a small model,
but the plain ESP32 has no PSRAM and no SIMD → marginal, slower inference, more
tuning pain. Not recommended given the Edge Impulse choice.

Skip analog/electret mics + ADC (KY-038 etc.) — too noisy for keyword spotting.
Digital I2S/PDM MEMS only.

## Software — DECIDED: Edge Impulse (DIY)

Chosen for full local control and its first-class XIAO ESP32-S3 Sense support.
Porcupine and ESP-SR below are kept only as fallbacks.

## Software options (kept for reference; laziest first)

The hard part is not CPU, it is that **"siika" is a custom Finnish word** no
off-the-shelf model knows. The three options differ mainly in how you get a
model for that word:

1. **Picovoice Porcupine — least effort.** Type "siika" into their web console;
   transfer-learning trains a model in seconds, no data collection. Free
   personal tier. Runs on desktop, Raspberry Pi, and ESP32/ESP32-S3 (there is a
   microcontroller API + an `mcu` demo folder). **Try this first.**
   *Verify before committing:* current SDK support for the chosen board and the
   personal-license terms.
2. **Edge Impulse keyword spotting — most control, proven on the XIAO S3.** DIY:
   record samples of "siika" + "noise" + "unknown", train a small DS-CNN, export
   an Arduino library. Free. First-class XIAO ESP32-S3 Sense support with an
   official keyword-spotting tutorial. Costs an afternoon of collecting samples
   but is fully in your hands and the safe fallback if Porcupine's board/license
   annoys.
3. **Espressif ESP-SR (WakeNet) — skip unless the above fail.** Runs even on the
   plain ESP32, but custom "siika" needs either a 500+ speaker corpus or the
   TTS-sample training route = the most friction of the three. Its built-in wake
   words are Chinese/English, not useful here.

## PC / Windows alternative (honest comparison)

Also fine, and the CPU fear is unfounded here too: **Porcupine on Windows** uses
single-digit % of one core, and would give the same custom "siika". **Vosk**
(offline, has a Finnish model, grammar-limited to one word) is a heavier full-STT
option. The real cost of the PC route is not CPU — it is that a PC must stay
powered and running. Only pick it if a PC already lives next to the panel.
Otherwise the standalone MCU wins on both "cooler" and "correct".

## Phases

- **Phase 0 — decide & buy.** Confirm architecture (separate listener + GPIO
  trigger) and hardware (XIAO ESP32-S3 Sense vs. spare Wemos + INMP441). Order.
- **Phase 1 — prove recognition alone.** Get "siika" detected on the listener
  board, printing to serial on each hit. No LEDs yet. Success = reliable
  detection of "siika" in a normal room, few false triggers.
- **Phase 2 — wire the trigger.** GPIO high on detect → panel Wemos runs a test
  animation. Success = say "siika", panel reacts.
- **Phase 3 — tune.** Detection threshold, mic gain, false-trigger rate. This is
  the calibration knob the physical world needs; budget real time for it.

## Decisions (2026-07-11)

- Hardware: **XIAO ESP32-S3 Sense** (onboard mic, one purchase).
- Recognizer: **Edge Impulse (DIY)**.
- Next action: order the board. Phase 1 begins when it arrives.

## Out of scope

- What the panel *does* on trigger (animation choice) — that is the
  `animation-system.md` plan.
- MEAN WELL power, multi-panel — separate plans.
