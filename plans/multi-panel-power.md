# Multi-Panel Power Plan

Goal: power 2+ WS2812B 16x16 panels at once. This is the power-distribution
plan that bridges the single-panel USB prototype and the final MEAN WELL
install.

## Core principle

**Powering the Wemos "better" does NOT let you drive more panels.** Panel count
is set by *where the high current comes from and what path it takes*, not by how
the Wemos is fed.

**Never route panel current through the Wemos board.** The 5V pin, PCB traces,
and shield headers are rated ~1-2 A. One panel at low brightness (400 mA cap)
survived that path in the prototype; multiple panels at real brightness would
overheat the traces / screw terminal.

## Correct topology (5V supply as a distribution bus)

The 5V switching supply acts as a mini MEAN WELL — a shared 5V bus that panels
tap directly, *if it has the amps*.

```
5V PSU (+) ──┬── Panel 1  5V
             ├── Panel 2  5V
             ├── ...
             └── Wemos 5V pin   (small branch, board only)
5V PSU (−) ──┬── Panel 1  GND
             ├── Panel 2  GND
             ├── ...
             └── Wemos GND      (common ground MANDATORY)
Wemos GPIO16 ── P1 DIN → P1 DOUT → P2 DIN → ...   (data chains through panels)
```

- Panels draw their big current straight from the PSU via the distribution
  (fuse box / terminal blocks), never through the Wemos.
- Wemos shares the common ground and taps a small branch for itself. (Or keep
  the Wemos on USB — but ground must still be tied to the PSU/panels.)
- Only the data line goes Wemos → panel chain.

## Safety rules

1. **Never feed 5V into the barrel jack.** It feeds a linear regulator needing
   7.5 V+; 5 V won't work and is forbidden (see prototype-phase.md).
2. **The 5V pin bypasses the onboard regulator.** Voltage into it must be a
   regulated, measured 5.0 V (≤5.5 V max). If the "5V supply" is actually the
   adjustable 5-15 V wall unit and it's set too high, the ESP32 dies. Use a
   fixed, confirmed 5.0 V source only.
3. **Common ground** between PSU, all panels, and Wemos — or the data signal
   has no reference and the panels won't work.
4. **Match wire gauge and fuses to the current** on each branch. Keep panel
   current out of the Wemos traces and shield headers entirely.

## Panel count = PSU amps ÷ per-panel current

Rough draw per 16x16 (256 LED) panel:

| Condition | Per-panel current |
|-----------|-------------------|
| Prototype brightness (~12/255 ≈ 5%) | ~0.5-1 A |
| Full white, full brightness | up to ~15 A |

So the count depends entirely on the supply's amp rating and the brightness
cap you run. Keep a software brightness/current limit as the real ceiling.

## Open questions (need before wiring)

- [ ] **5V supply amp rating** — read off the label. Sets how many panels and
      at what brightness cap.
- [ ] **Which supply is it** — a fixed 5V brick, or the adjustable 5-15 V unit
      set to 5V? (Safety rule 2 hinges on this.)
- [ ] How many panels in this stage, and physical layout / data chain order.

## Worked example: 5V 2.4A supply, capped dim light

Budget: 2.4 A → derate 80% (~1.9 A) → minus Wemos ~0.3 A → **~1.6 A for panels.**
Per panel full white full brightness is ~15.4 A, so this supply is
brightness-limited, not panel-limited.

Decision: **accept dim light and let a hard ~1600 mA software cap enforce
safety.** Then panel count is limited by "how dim is still useful", not by
brown-out.

| Panels | Brightness at 1.6 A shared | Feel |
|--------|----------------------------|------|
| 2 | ~5% | pleasant dim (= prototype 12/255) |
| 3 | ~3.5% | dim |
| 4 | ~2.5% | night-light |
| >4 | — | not worthwhile, wait for MEAN WELL |

**Recommendation: up to ~4 panels, hard 1600 mA cap.** Two ways to cap:

- **2 panels → stay on Adafruit NeoPixel** (current firmware), cap via
  `brightness` ≤13. Simple, proven. Note: a brightness cap is NOT
  content-aware, so full-white at that brightness is the worst case — the
  table's brightness limits apply.
- **3-4 panels → switch to FastLED** with
  `setMaxPowerInVoltsAndMilliamps(5, 1600)`. Content-aware: it auto-dims even
  full-white content, so it can't brown out. Only this justifies more panels
  safely.

## Out of scope

- MEAN WELL LRS-150F-5 wiring, ANL fuse, 10 AWG distribution — separate plan
  when the PSU arrives (this plan is the low-power bridge before that).
- Panel mounting on plywood/cardboard — separate plan.
