# Siikapaneeli

Siikapaneeli on WS2812B LED-valopaneeleista (16x16, 256 LED, 5V, GRB) tehty
asennus, jota ohjaa Wemos D1 R32 (ESP32) mikrokontrolleri. Lopullinen asennus:
12 paneelia (2 päällekkäin, 6 vierekkäin) ja MEAN WELL LRS-150F-5 -virtalähde
(tilattu, ei vielä saapunut).

Nykytila: prototyyppi VALMIS — yksi paneeli toimii USB-virralla
(ks. plans/prototype-phase.md). Seuraavat vaiheet omissa plans/-tiedostoissaan.

# Komponentit

- WS2812B 16x16 -paneeli(t), Wemos D1 R32, DIN-kisko-ruuviliitinshield
- Sulakerasia (12-paikkainen), ANL 60A -sulake + pohja, virtakaapelia
  10/14/16 AWG, pientarvikkeita
- Tilattu: MEAN WELL LRS-150F-5
- Particle-kittien sensorit ja pienosat: ks. plans/particle-kits-inventory.md

# Sähköturvallisuus (aina voimassa)

- ÄLÄ syötä 5 V barrel jackiin — sen lineaariregulaattori vaatii 7.5 V+.
- ÄLÄ reititä paneelivirtaa Wemos-levyn läpi monipaneelivaiheessa; 5V-pinni ja
  liuskat kestävät vain ~1-2 A. Paneelit ottavat virran suoraan PSU:lta.
- Wemosin 5V-pinniin vain säädelty, mitattu 5.0 V (max 5.5 V) — pinni ohittaa
  regulaattorin.
- Yhteinen maa (PSU + paneelit + Wemos) on pakollinen.
- USB-virralla kirkkauskatto 12/255 (~400 mA) — ei nosteta ennen MEAN WELLiä.

# Työkaluketju

- ESP32 on kytketty micro-USB:llä tähän MacOS-koneeseen, jossa Claude Code
  ajaa. CH340-portin nimi ei ole vakaa — tarkista aina ennen käyttöä:
  `ls /dev/cu.* | grep -i usbserial`.
- `arduino-cli`, board `esp32:esp32:d1_uno32`. Uploadissa PAKKO käyttää
  `UploadSpeed=460800` (oletus 921600 korruptoi CH340:llä). Monitor 115200 baud.
- Adafruit NeoPixel -kirjasto (ei FastLED/PlatformIO), data GPIO16,
  serpentiini-XY-mappaus.
- Firmware: `firmware/siika/siika.ino` = varsinainen ohjelma,
  `firmware/panel_test/panel_test.ino` = diagnostiikka, ei muuteta.

# Suunnitelmat ensin

Suunnitellaan AINA ennen toteuttamista. Suunnitelma kirjoitetaan tiedostoon
plans/ hakemistossa. Kysy aina lupa ennen kuin siirrytään suunnitelmasta
toteutukseen.
