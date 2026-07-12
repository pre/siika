# Prototype Phase Plan

Goal: prove the 16x16 WS2812B panel works, powered through the Wemos D1 R32,
then get a minimal custom-code base running for own graphics.

## Power decisions

- **Wemos + panel are powered from micro-USB (Mac) only.** Budget ~500 mA,
  software-limited to 400 mA. Enough for a dim test pattern on 256 LEDs.
- The adjustable wall supply (5–15 V) is **not used** in the prototype:
  - The barrel jack feeds a linear regulator that needs 7.5 V+ input, and
    that regulator can only pass ~300–500 mA before overheating — no gain
    over USB, extra risk. Never feed 5 V or 6 V into the barrel jack.
- Micro-USB and barrel jack may be connected at the same time (the board is
  designed for it), but we simply don't need the barrel jack.
- Full brightness (15 A) waits for the MEAN WELL LRS-150F-5 — out of scope.

## Wiring (via the DIN rail screw terminal shield)

| Panel wire | Connect to |
|-----------|------------|
| 5V (red)  | Wemos `5V` terminal |
| GND (white/black) | Wemos `GND` terminal |
| DIN (green, data-in end!) | `IO16` terminal |

- WS2812B data is directional — use the connector on the **DIN** side.
- 3.3 V logic into a 5 V panel is marginal but normally works at USB voltage.
  If the first LED flickers, add a ~330 Ω resistor in the data line (have one
  ready from the small-parts box).
- Double-check polarity before plugging USB in.

## Phase A — WLED smoke test (no code)

1. ~~Verify the CH340 serial port appears on the Mac~~ **DONE** —
   `/dev/cu.usbserial-1110`.
2. ~~Flash WLED~~ **DONE** — flashed from CLI with esptool instead of the
   web installer (see debug log below).
3. In WLED settings: LED count 256, data pin 16, color order GRB,
   **current limit 400 mA**, max brightness ~25 %.
4. Success = solid colors R/G/B/white reach all 256 LEDs with no flicker
   and the board stays cool.

## Status 2026-07-11 — Phase A nearly done

WLED 0.15.3 is flashed and boots (serial shows the `Ada` banner).
Panel 5V wire is **disconnected** (removed during USB debugging).

**Serial port note:** the CH340 port name is not stable across
cables/ports. Currently enumerates as `/dev/cu.usbserial-110` (was
`-1110` earlier). Confirmed chip: CH340 (idVendor 0x1A86, idProduct
0x7523). Micro-USB tends to seat only halfway — push the plug fully home
or the board never appears on the USB bus. Check the live name with:
`ls /dev/cu.* | grep -i usbserial` before flashing/monitoring.

### Debug log

- First board + cable combo never enumerated on USB: power LED lit but no
  device in the USB tree, panel load ruled out. Suspected charge-only
  micro-USB cable. User swapped **both** cable and board → CH340 appeared
  immediately. The original board was never tested with the working cable,
  so it may still be fine — retest later before buying more boards.
- macOS quirks on this machine: `system_profiler` and `log show` return
  empty output in this environment; use `ioreg -p IOUSB -l -w0` to inspect
  the USB bus.
- Flash procedure used (repeatable):
  - esptool 5.3.1 in a Python venv (`pip install esptool`).
  - Binaries: `esp32_bootloader_v4.bin` from
    `https://install.wled.me/bin/Release/release_0_15_3/` and
    `WLED_0.15.3_ESP32.bin` from GitHub releases.
  - `esptool --chip esp32 --port /dev/cu.usbserial-1110 --baud 460800 erase-flash`
  - `esptool --chip esp32 --port /dev/cu.usbserial-1110 --baud 460800 \
     write-flash 0x0 esp32_bootloader_v4.bin 0x10000 WLED_0.15.3_ESP32.bin`
- Board identified: ESP32-D0WD-V3 rev 3.1, MAC `b0:cb:d8:c6:8e:84`.

### Next steps

1. Re-check wiring on the **current** board (it was swapped): GND → GND,
   DIN → IO16, then reconnect the panel 5V wire to the Wemos 5V terminal.
2. Join the `WLED-AP` WiFi (password `wled1234`) with phone or Mac and open
   `http://4.3.2.1`.
3. Config → LED Preferences: length 256, GPIO 16, max PSU current 400 mA
   (auto brightness limiter on), color order GRB (fix if colors are wrong).
4. Run solid red / green / blue / white — Phase A success criteria.
5. Optional: connect WLED to home WiFi for browser control.
6. Then Phase B (custom FastLED base, plan section above).

## Status 2026-07-11 — Phase B DONE, panel works

Panel test passed. The 16x16 WS2812B panel lights correctly, USB-powered
through the Wemos, running the custom test firmware. This confirms the
prototype: panel + board + wiring + power budget all work.

Working configuration:
- Firmware: `firmware/panel_test/panel_test.ino` — Adafruit NeoPixel
  (already installed), NOT FastLED/PlatformIO. GPIO16, 256 LEDs, GRB,
  brightness 12/255 (USB-safe cap), three looping tests (raw snake / XY
  scan / R-G-B wash).
- Board: the swapped board (MAC `b0:cb:d8:c6:8e:84`), then a second Wemos
  used for the panel test — both flash and run fine.
- Toolchain: `arduino-cli` with `esp32:esp32:d1_uno32`. Upload MUST use
  `UploadSpeed=460800` (default 921600 corrupts on the CH340). Port
  `/dev/cu.usbserial-110`.
- Confirm running: `arduino-cli monitor -p /dev/cu.usbserial-110 -c baudrate=115200`.

Open before scaling brightness: still on USB power, so keep brightness low
until the MEAN WELL LRS-150F-5 is wired. Verify serpentine mapping and GRB
order held on the panel (flip `SERPENTINE`/`FIRST_ROW_REVERSED` in the sketch
if a future panel scans differently).

## Phase B — custom FastLED base (own graphics)  — superseded, see status above

1. PlatformIO project in this repo:
   - board `wemos_d1_uno32`, framework Arduino, dependency FastLED.
2. Minimal sketch:
   - `FastLED.setMaxPowerInVoltsAndMilliamps(5, 400)` — hard power cap.
   - 16x16 serpentine `XY(x, y)` mapping helper.
   - Test pattern that verifies the mapping: a moving dot scanning row by
     row + a corner marker, then a slow color wash.
3. Build & upload with `pio run -t upload`, monitor with `pio device monitor`.
4. Success = the dot moves left-to-right on every row from a known corner,
   i.e. the XY mapping is proven correct for future graphics.

## Out of scope (later plans)

- MEAN WELL wiring, fuse box, 10 AWG distribution — separate plan when the
  PSU arrives.
- Mounting panels on plywood/cardboard — separate plan.
- Multi-panel (2–12) layout and mapping.
