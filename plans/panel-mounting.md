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

## Wire pass-through

- Drill **one 6–8 mm hole per panel** near its input corner (data-in + power
  injection point). Data can chain panel→panel on the *front* with short
  jumpers, so you don't strictly need a hole per data hop — but one hole per
  tile keeps power injection and the first data feed flexible.
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
- [ ] Do the panels ship with usable adhesive backing? (only a helper to VHB.)
- [ ] Diffuser yes/no, and depth → affects whether a front frame is wanted.
- [ ] Data chain order / orientation per cell → coordinate with firmware mapping.

## Out of scope

- Electrical distribution, fusing, PSU wiring → multi-panel-power.md.
- Firmware XY / serpentine coordinate mapping → animation/firmware plan (only
  the *orientation-marking* dependency is noted here).
