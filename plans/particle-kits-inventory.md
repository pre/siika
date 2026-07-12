# Particle Kits Component Inventory

Inventory of the three Particle kits (ordered 2018) and which components are
useful for the Siikapaneeli project. The Particle boards themselves (Electron,
2x Photon) are not useful: the ESP32 already provides WiFi, Particle has
end-of-lifed Gen 2 devices and their SIM service, and Finnish 3G networks are
shut down. The value is in the sensors and small parts.

## Kit contents

### Sensor Kit w/ Electron + Data, 3G Eur/Afr/Asia (SNSRKIT3G270)

Particle never published the exact 10-sensor list; items below are identified
from the official datasheet photo and description ("acceleration, distance,
temperature, humidity, motion, vibration, gases, loudness"). Verify against
the physical box.

- Particle Electron 3G (U270) PCB, Particle SIM, Taoglas antenna — obsolete
- Li-Po battery 2000 mAh
- Sound/loudness sensor (electret microphone on breakout board)
- PIR motion sensor (white dome lens)
- Ultrasonic distance sensor (HC-SR04 style, two cylinders)
- Gas sensor (MQ series, round metal mesh)
- Accelerometer, vibration sensor, temperature/humidity sensor (per description)
- Breadboard, jumper wires, USB cable, pinout card

### Photon Maker Kit (MKITPH)

Full official list (Digikey datasheet):

- Photon with headers, flex antenna — obsolete
- Mini breadboard, proto-board, deluxe jumper wire pack, 10x M-F jumper wires
- USB micro B cable
- Battery holder 4xAA
- 30x resistors, 10x ceramic capacitors (each value), 5x electrolytic 100 uF
- 10x LEDs, 1x RGB LED, 1x IR LED, 6x diodes
- 2x photoresistors, 1x 10K rotary potentiometer
- 1x temperature sensor, 1x sealed (waterproof) temperature sensor
- 1x PIR sensor, 1x pancake vibration motor, 1x micro servo
- 1x piezo buzzer, 3x mini pushbuttons, 2x SPDT switches, 1x SPDT relay
- 1x NPN transistor, 7x headers
- 1x 0.96" serial OLED screen

### Photon Kit (PHOTONKIT)

- Photon with headers — obsolete
- Mini breadboard, USB micro B cable
- 1x LED, 1x photoresistor, a few resistors

## Usefulness for Siikapaneeli

| Component | Kit | Use | Priority |
|---|---|---|---|
| Resistors (330–470 ohm) | Maker Kit | WS2812B data line series resistor, protects first LED | High — wire into prototype now |
| Electrolytic caps 100 uF | Maker Kit | Across panel 5V/GND to smooth current spikes (parallel several) | High — wire into prototype now |
| Diodes | Maker Kit | Drop panel VDD to ~4.3 V if 3.3 V data signal proves unreliable (avoids level shifter) | Medium — only if data glitches appear |
| PIR motion sensor | Both kits | Wake panel / start animation on passer-by, blank otherwise (saves power) | High — fits detection-and-idle plan |
| Photoresistor | Maker Kit, Photon Kit | Auto-brightness from ambient light via ADC | High |
| Sound sensor (microphone) | Sensor Kit | Music/sound-reactive effects | Medium — fits voice-trigger plan |
| Ultrasonic distance sensor | Sensor Kit | Hand-wave gesture control, proximity effects | Medium |
| Mini pushbuttons | Maker Kit | Effect/mode switching without network | Medium |
| 10K potentiometer | Maker Kit | Manual brightness knob on ADC pin | Low — photoresistor covers it |
| Breadboards, jumper wires, proto-board | All kits | Prototyping; proto-board for final soldered install | High |
| USB micro B cables | Maker Kit, Photon Kit | Spare cables for Wemos | Low |
| Temperature sensors | Maker Kit, Sensor Kit | Show temperature on the 8x32 matrix | Low — nice-to-have effect |
| SPDT relay | Maker Kit | Hard power-off of panel 5V rail | Low — software blank is enough |
| Accelerometer, vibration sensor | Sensor Kit | No obvious use on a wall-mounted panel | Skip |
| Gas sensor (MQ) | Sensor Kit | Not panel-related; MQ sensors are power-hungry | Skip |
| OLED screen, servo, vibration motor, buzzer, RGB/IR LEDs | Maker Kit | Panel is already the display; no moving parts needed | Skip |
| Li-Po battery, 4xAA holder | Sensor/Maker Kit | Panel needs stable 5 V at amps; batteries unsuitable | Skip |
| Electron, 2x Photon, SIM, antennas | All kits | EOL boards, dead networks, redundant WiFi | Skip |

## Suggested first additions to the prototype

1. 330–470 ohm resistor in the WS2812B data line (measure Maker Kit resistors to find values).
2. 100 uF electrolytic cap(s) across panel power at the screw terminals.
3. PIR sensor on a GPIO for wake-on-motion.
4. Photoresistor + fixed resistor divider on an ADC pin for auto-brightness.
