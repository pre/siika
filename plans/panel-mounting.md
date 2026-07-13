# Panel Mounting Plan (2×6 = 12 panels on plywood, wall-mounted)

Goal: mount 12 flexible WS2812B 16×16 panels in a 2-row × 6-column grid onto a
rigid substrate that hangs vertically on a wall. Panels are thin, flexible
plastic with **no screw holes**. Wires exit through the substrate to the back
where the fuse box / terminal blocks live.

## Physical facts that drive every decision

- **The substrate carries all structural load. The panels are light** (a
  flexible 16×16 tile is ~50–80 g). So the mount does NOT need to be strong —
  it needs to (a) keep a floppy sheet flat, (b) not peel at corners on a
  vertical surface, (c) survive panel heat, (d) let a dead panel be replaced.
- **Corners peel first.** Flexible panels lift at the corners under their own
  spring-back long before the middle fails. Any adhesive plan must pin corners.
- **Panels fail.** WS2812B tiles die (one dead LED can kill a whole panel's
  downstream chain). A *permanent* mount on a center panel = painful rework.
  Reversibility is a real requirement, not a nicety.
- **Full-white = heat.** Big current makes warmth. Adhesive must tolerate it;
  don't seal panels in an airtight box. Minor with the software current cap.

## Substrate: plywood, not cardboard

- **9–12 mm birch plywood.** Rigid, takes drilled wire holes cleanly, holds
  adhesive/screws, won't sag vertically. Panel area = **320 × 960 mm**
  (confirmed: 160 mm tile, 10 mm pitch, 2 rows × 6 cols), plus margin/frame.
- Cardboard: only for a throwaway single-panel bench test. Vertical + wire
  holes + adhesive peel forces will tear it. Not for the 12-panel piece.

## Mounting options (ranked for THIS case)

| # | Method | Reversible? | Strength | Notes |
|---|--------|-------------|----------|-------|
| **1** | **3M Dual Lock / Velcro tape** | ✅ easy swap | plenty for light panels | Best "replace a dead panel" story. Raises panel ~2–5 mm off board. |
| **2** | **VHB double-sided foam tape** | ⚠ with floss+heat | strong, clean | Border frame + 2 center strips per tile. Tolerates heat, fills gaps. |
| **3** | **Batten/muntin grid** (thin wood strips screwed to plywood, trapping panel edges) | ✅ | mechanical | No adhesive on panel. Doubles as diffuser frame. Strips shadow edge LEDs, more build work. |
| 4 | Neutral-cure silicone | ⚠ peel later | strong, flexible | Cheap, heat-ok, but 24 h cure and messy. Alt to VHB. |
| 5 | Panel's own 3M backing (if present) | ✗ | weak | Peels at corners on vertical over time. Only as a helper, never alone. |
| 6 | **Hot glue** | ✗ hard to remove | strong | User's own concern is right: fights replaceability, and heat gun near flex PCB is a risk. **Corner-tacking supplement only, not primary.** |
| 7 | Zip-tie / lacing through drilled holes | ✅ | mechanical | Only if the panel has a clear edge margin to punch without hitting a trace. Risky on flex PCB — skip unless verified. |

**Decision: VHB foam tape (option 2).** User won't swap panels often, so the
set-and-forget mount wins over the fully-reversible Velcro. One cheap product,
strong, clean, heat-tolerant, and still removable with fishing-line + gentle
heat on the rare occasion a panel dies. Don't mix in a second primary method.
Apply as a border frame per tile + 2 center strips; corners first.

## Revised design: edge-to-edge, no front wires (chosen)

New constraints (2026-07): panels butt edge-to-edge, NO wiring on the front,
one data line per group. This kills the "chain data on the front" assumption,
so every wire must reach the back. Chosen build:

- **Rigid backer per group.** Bond each group's panels to a 3–4 mm birch
  plywood backer with VHB (option 2). The backer keeps the floppy film flat;
  thin battens/spacers (~15–18 mm) screw the backer to the main plywood,
  leaving a wire plenum behind the whole array.
- **Grouping: 4 groups of 3 (1×3 horizontal).** Backer ~160×480 mm, easy to
  handle and pre-wire on the bench. Alt: 2 groups of 6 (2×3 block) — bigger,
  floppier backer, needs 14 AWG feeds; pick only if fewer feed points beat
  weight.
- **One feed hole per group (4 total), not per panel.** Inter-panel jumpers
  live in the plenum; only the group's +5V/GND feed + data line pass through.
- **Inject power to each panel individually from the group bus. Do NOT
  daisy-chain power through the panels' 20 AWG pigtails** — the first pigtail
  would carry the whole group and overheat (budget below).
- **One data line per group** (4 lines → 4 GPIOs): short runs + data isolation
  (a dead group won't break others), AND real FPS — the 4 lines transmit in
  parallel on separate RMT channels. Decision 2026-07: switch NeoPixel →
  **FastLED** for parallel output (see fastled-migration.md). 4 × 768 LEDs
  concurrently ≈ 23 ms → ~43 fps, vs ~10 fps for one 3072-LED chain.

### Current budget / why the panel's 20 AWG is fine
- Per panel full white ≈ 256 × 60 mA ≈ 15 A — never reached: LRS-150F-5 is
  5 V / 30 A total for 12 panels ≈ 2.5 A/panel (global brightness cap forces
  this).
- Each panel's own 20 AWG pigtail carries only THAT panel's current (~2.5 A
  capped). 20 AWG safe continuous ≈ 3–5 A on a short open-air run → comfortable.
- Only the added group-feed wire carries the group sum: 3 panels ≈ 7.5 A
  → 16 AWG; 6 panels ≈ 15 A → 14 AWG.

## Wire pass-through

- Drill **one hole per group (4 total)** for that group's +5V/GND feed + data
  line. Size it to pass the JST connector housing if you feed through
  connectors (~15–20 mm), or 6–8 mm for bare wires. Inter-panel jumpers stay
  in the plenum, so no per-panel hole is needed (supersedes the earlier "hole
  per tile" plan — that assumed front data chaining, now dropped).
- **Chamfer / deburr every hole** (countersink bit or round-file), or fit a
  rubber grommet. A raw plywood hole edge will cut wire insulation over time,
  especially on a vertical piece where wires hang with their own weight.
- Back of board = wiring bay: fuse box, terminal blocks, PSU distribution
  (see multi-panel-power.md). Keep it accessible.

## Process (dry-fit before anything permanent)

1. Mark the grid on the plywood: 2×6 cells at true LED pitch so the whole array
   reads as one continuous 32×96 display (butt tiles edge-to-edge, seam gap =
   one pitch — otherwise the seam shows as a dark line).
2. **Mark panel orientation** (data arrow direction) on every cell BEFORE
   bonding. This sets the firmware XY mapping (all-same vs. serpentine rows).
   Getting it wrong after gluing = rework. Ties to the coordinate-mapping work.
3. Dry-fit all 12 with a couple loops of normal tape. Power up, check alignment
   and seams look continuous, check the coordinate mapping is what firmware
   expects. **Only then** commit to the permanent/VHB mount.
4. Drill + chamfer wire holes. Route wires to the back.
5. Bond panels: corners first, then edges/center per chosen method.
6. Optional diffuser in front on standoffs (see below).

## Optional but recommended: diffuser

Thin acrylic / paper / foam sheet 10–20 mm in front on standoffs blurs the
individual LED dots into an even glow (turns "grid of dots" into "light panel").
The batten grid (option 3) can double as the standoff frame if you go that way.
Affects mount depth — decide before drilling standoff points. Can be added later.

## Buy spares

Order **1–2 spare panels** regardless of mount choice. This is the cheapest
insurance against the replaceability problem, and it lets the default (VHB)
stay the default instead of forcing the fully-reversible mount.

## Open questions (need before mounting)

- [x] **LED pitch** → 10 mm (160 mm tile). Panel area 320 × 960 mm.
- [x] **Replace-often or set-and-forget?** → set-and-forget → **VHB chosen.**
- [x] **Backer material** → 3–4 mm birch plywood.
- [x] **Grouping / data** → 4 groups of 3 (1×3), one data line per group.
- [ ] Do the panels ship with usable adhesive backing? (only a helper to VHB.)
- [ ] Diffuser yes/no, and depth → affects whether a front frame is wanted.
- [ ] Orientation (data arrow) per cell → coordinate with firmware mapping.
- [x] **Refresh / driver** → FastLED, one parallel data line per group of 3
  (see fastled-migration.md).

## Out of scope

- Electrical distribution, fusing, PSU wiring → multi-panel-power.md.
- Firmware XY / serpentine coordinate mapping → animation/firmware plan (only
  the *orientation-marking* dependency is noted here).
