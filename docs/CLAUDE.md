# ESP32 Soundbar – firmware brief for Claude

You are helping build the firmware for a DIY battery-powered stereo smart speaker ("soundbar").
The hardware is already built and wired; this document is the single source of truth for pins,
modules and the display layout. **The owner speaks Czech – answer in Czech**, no code comments.

## How to work on this project
- The owner flashes the board and reports back with Serial Monitor output (115200 baud). You cannot see the hardware,
  so add clear `Serial` logs for every new feature and ask for the log when something does not work.
- All milestones are implemented but **not yet verified on the hardware**. Fix issues from the owner's logs one at a time.
- Keep `src/main.cpp` thin; features live in modules (namespaces): `.cpp` files in `src/`, headers in `include/` with the same
  subfolders (`src/audio/dsp.cpp` ↔ `include/audio/dsp.h`, included as `"audio/dsp.h"`). All pins and constants in `include/config.h`,
  display strings in `include/texts.h`. Before every change build both environments: `pio run -e soundbar -e updater`.

| Path | Responsibility |
|---|---|
| `controller.*` | central state (`Status`), modes, playback commands, settings application, status JSON, power off; other tasks talk to it only via `Controller::post()` (`command.h`) |
| `settings.*` | NVS settings (`Preferences`, debounced save) |
| `audio/dsp.*` | AudioTools HighPassFilter + LowShelfFilter + loudness + headroom + soft limiter, volume (AudioTools `AudioOutput`) |
| `audio/output.*` | I2S + DSP + XSMT mute |
| `audio/bt_sink.*` | A2DP sink, AVRCP, metadata, absolute volume |
| `audio/stream_player.*` | FreeRTOS audio task: ICY radio stream with AudioTools RingBuffer / SD file with MetaDataID3, Helix MP3/AAC + WAV decoders |
| `display/nextion.*`, `display/latin2.*`, `display/ui.*` | Nextion protocol + field cache, UTF-8 → ISO-8859-2, pages and touch IDs |
| `input.*` | ESP32Encoder (PCNT), OneButton buttons, touch pad |
| `power.*` | battery curve + smoothing, deep sleep |
| `sensors.*` | Adafruit BME280 + BH1750 library on `Wire` |
| `clock.*`, `leds.*` (NeoPixelBus on I2S1), `stations.*`, `sd_card.*` | NTP/time, WS2812B effects + current cap, station list on LittleFS, SD access |
| `net/network.*` | WiFi in its own task (networks from `secrets.h`, saved by esp-ota-updater), mDNS, update check |
| `net/api.*` | REST + WebSocket API for the app, SD upload (started once WiFi is connected) |
| `updater/main.cpp` | separate `updater` environment for the `factory` partition (see *Platform*) |
- Pin every library to an exact version/tag in `platformio.ini`. Before adding a library, check that it supports the
  Arduino core in use (see *Platform* below) and an ESP32 **without PSRAM**.
- Do not change pin assignments – they are soldered. If something seems impossible with the current pins, say so and ask.
- Prefer non-blocking code (no long `delay()`), FreeRTOS tasks only where needed (audio decoding).

## Platform
- Board: **ESP32 DevKitC (ESP32-WROOM-32D, 4 MB flash, NO PSRAM)**, PlatformIO `board = esp32dev`, `framework = arduino`.
- Current setup: `platform = espressif32 @ 6.9.0` → **Arduino core 2.0.17 (ESP-IDF 4.4)**, `-std=gnu++17`.
  - ESP32-A2DP **v1.8.9 and newer need Arduino core 3** → on core 2 use **v1.8.8** (+ arduino-audio-tools v1.2.0). This combination is the tested baseline.
  - If a feature really needs core 3, propose switching the whole project to the pioarduino platform – do not mix.
- No PSRAM ⇒ **Bluetooth A2DP and WiFi streaming together do not fit in RAM in practice** – run one at a time. Mode switching = store the mode in NVS (`Preferences`) and `ESP.restart()`.
- Flash: the full firmware (BT + WiFi + TLS + decoders) is ~2.45 MB, so two ~1.9 MB OTA slots are impossible.
  Decided layout (`partitions.csv`): `factory` 1.1 MB = **updater** env (WiFi + esp-ota-updater only, ~0.95 MB),
  `app0` (ota_0) 2.75 MB = **soundbar** env, 64 kB LittleFS. The soundbar checks `version.txt` of the latest GitHub
  release; when newer it sets the boot partition to `factory` and restarts, the updater writes `app0` and boots it.
  If `app0` is invalid the bootloader falls back to the updater.
- IRAM is nearly full with BT + WiFi linked together (~1.2 kB free in 0.1.0). LEDs use NeoPixelBus on I2S1 instead of the RMT driver to make room
  for `Wire`. Check `.iram0.text_end` (objdump) after adding a library that brings IDF drivers with IRAM ISRs.
- Prefer existing libraries; do not write own drivers, parsers or buffers unless no library fits.

## Hardware

### Power
1S2P Li-ion 6400 mAh → charger/boost module (IP5306 type, 5 V 2.1 A, USB-C charging) → rocker switch → **5 V rail**.
ESP32 is fed from 5 V (V5 pin) and supplies the **3V3 rail** for low-power modules.
Ground rails: GND-P (power: charger, display, DAC, amp, LEDs, microSD) and GND-S (signals), joined through the ESP32 GND pins.

### Pin map (ESP32 board labels in brackets)
| GPIO | Function | Notes |
|---|---|---|
| 16 [G16] | Nextion RX2 ← display TX | UART2, 115200 |
| 17 [G17] | Nextion TX2 → display RX | |
| 26 [G26] | I2S BCK → PCM5102 BCK | |
| 25 [G25] | I2S LRCK/WS → PCM5102 LRCK | |
| 27 [G27] | I2S DATA → PCM5102 DIN | PCM5102 SCK tied to GND (3-wire I2S) |
| 2 [G2] | PCM5102 XSMT (soft mute) | 10k pull-down → muted at boot; drive HIGH to play. Strapping pin. |
| 21 [G21] | I2C SDA | BME280 + BH1750 |
| 22 [G22] | I2C SCL | |
| 18 [G18] | microSD SCK | VSPI; LaskaKit μŠup module |
| 19 [G19] | microSD MISO (module pin **SDO**) | |
| 23 [G23] | microSD MOSI (module pin **SDI**) | |
| 5 [G5] | microSD CS | strapping pin; module pins CARD (card detect) and EN (power enable) are NOT connected |
| 32 [G32] | KY-040 CLK | module has pull-ups, powered from 3V3 |
| 33 [G33] | KY-040 DT | |
| 13 [G13] | KY-040 SW (push) | needs `INPUT_PULLUP`; RTC GPIO → usable as deep-sleep wake (ext0) |
| 39 [SN] | Button 1 (left, seen from the front) | input-only, external 10k pull-up + 100 nF, active LOW |
| 34 [G34] | Button 2 (middle) | input-only, same |
| 35 [G35] | Button 3 (right) | input-only, same |
| 4 [G4] | Capacitive touch pad (stainless M3 screw on the top, wire under the inner nut) | `touchRead(T0)`; calibrate a baseline at boot, debounce, ignore while the hand is on the encoder |
| 14 [G14] | WS2812B LED strip DIN (≈40 LEDs, underglow below the case, LEDs face down) | 330 Ω series, strip on 5 V, 470 µF on the 5 V entry inside the case; usual colour order GRB – verify |
| 36 [SP] | Battery voltage | divider 100k/100k + 100 nF → Vbat = Vpin × 2; use `analogReadMilliVolts`, average, 11 dB |
| 15 | (optional) USB-C VBUS detect | not wired yet – charging state is unknown to the firmware |

I2C addresses: **BH1750 0x23**, **BME280 0x76 or 0x77** (detect at boot). BME280 sits inside the case → temperature reads a bit high, apply a configurable offset.

Not connected / not usable: charger pad K (key), PCM5102 analog outputs go only to the PAM8403 (no jack used), no I2S MCLK.
The rocker switch cuts the 5 V output → with the rocker off everything is unpowered (no software wake possible).
microSD: card stays inside the case (no slot outside) – files arrive over WiFi (upload from the app). Card is FAT32
(the owner's card may be a large card with a ≤32 GB FAT32 partition). If the card never initialises, the hardware fix is EN → 3V3 on the module.

### Audio chain
PCM5102 (GY board, FLT/DEMP/FMT jumpers to L = normal filter, no de-emphasis, I2S format) → passive divider 22k/4.7k per channel →
PAM8403 (2×3 W, bridged outputs) → 2× Roadstar VO-401 speakers in separate closed chambers (~0.9 l each).
Volume is software (I2S sample scaling). Start at a moderate default; PAM8403 distorts if driven too hard.

**Bass is weak** (owner tested it already mounted and sealed in the case: 4" full-range drivers in ~0.9 l closed boxes, 3 W amp).
The firmware must compensate with DSP – this is a required feature, not optional:
- Bass boost / low-shelf EQ on the I2S output for every source (Bluetooth, radio, SD). The hardware test in this folder already
  does it with arduino-audio-tools `Equalizer3Bands` between the A2DP sink and `I2SStream` (lows ×1.7 below 150 Hz, mids/highs ×0.8) – use it as a starting point,
  a proper biquad low-shelf (~100–150 Hz, +4…+9 dB) is preferred if CPU allows.
- Keep headroom: lower the overall gain by the boost amount and add a simple soft limiter / clip protection so loud passages do not distort the PAM8403.
- Loudness: more bass boost at low volume, less at high volume (volume-dependent shelf gain).
- High-pass below ~60–70 Hz: the drivers cannot reproduce it and it only wastes amplifier power.
- Settings: bass level (e.g. 0–10) + on/off, stored in NVS, adjustable on the settings page / from the app; the touch screw toggles bass boost in the test firmware.

### Battery
Show % from voltage with smoothing (moving average); approximate curve 4.20 V = 100 %, 3.30 V = 0 %
(make the curve a small lookup table, not linear, if possible). Warn below ~10 %, go to deep sleep below ~3.3 V.

## Nextion display
NX4024K032 (Enhanced 3.2", 400×240, landscape), project built in Nextion Editor in `display-preset/` (`soundbar.HMI`, compiled `build/soundbar.tft`).
- UART 115200 (set by `baud=115200` in Program.s), every command ends with `0xFF 0xFF 0xFF`.
- Fonts use **iso-8859-2** → convert UTF-8 metadata (Czech letters) to ISO-8859-2 before sending; replace `"` with `'`.
- Display → ESP32: `0x65 page id event FF FF FF` on touch release (components with "Send Component ID"),
  `0x66 page FF FF FF` when a page opens (`sendme` in every page's Postinitialize), `0x71 + 4-byte value` as answer to `get x.val`.
- Some buttons change page on the display itself (listed below); the ESP32 still receives the touch event.
- Component IDs = order in which they were added (page itself = 0). If a touch does not match, log `page/id` and adjust constants.
- Text fields have `txt_maxl` limits – truncate strings to fit.
- `bt_play` (dual-state): 0 = play icon, 1 = pause icon. `bt_led`: 0 off / 1 on. `q_ic0..4` crop icons: set `picc`.
- `h_vol` range 0–21, `h_dim` 10–100 (display applies `dim` itself on drag), `h_led` 0–255.

#### Page 0 `home` (background pic 0, pressed pic 1)

| ID | objname | type | x,y,w,h | notes |
|---|---|---|---|---|
| 1 | `t_bat` | Text | 300,4,40,20 | txt_maxl=4, font=0 |
| 2 | `j_bat` | Progress bar | 349,10,30,9 |  |
| 3 | `t_mode` | Text | 8,4,150,20 | txt_maxl=20, font=0 |
| 4 | `t_time` | Text | 12,34,240,66 | txt_maxl=5, font=2 |
| 5 | `t_date` | Text | 16,102,236,24 | txt_maxl=24, font=0 |
| 6 | `t_temp` | Text | 262,40,128,32 | txt_maxl=8, font=1 |
| 7 | `t_hum` | Text | 262,76,128,22 | txt_maxl=8, font=0 |
| 8 | `t_pres` | Text | 262,100,128,22 | txt_maxl=10, font=0 |
| 9 | `g_now` | Scrolling text | 12,136,376,24 | txt_maxl=80, font=0 |
| 10 | `b_radio` | Button | 8,168,92,64 | sends touch event, display runs `page player` |
| 11 | `b_sd` | Button | 104,168,92,64 | sends touch event, display runs `page files` |
| 12 | `b_bt` | Button | 200,168,92,64 | sends touch event, display runs `page player` |
| 13 | `b_set` | Button | 296,168,92,64 | sends touch event, display runs `page settings` |

#### Page 1 `player` (background pic 2, pressed pic 3)

| ID | objname | type | x,y,w,h | notes |
|---|---|---|---|---|
| 1 | `b_back` | Button | 0,0,44,28 | sends touch event, display runs `page home` |
| 2 | `t_bat` | Text | 300,4,40,20 | txt_maxl=4, font=0 |
| 3 | `j_bat` | Progress bar | 349,10,30,9 |  |
| 4 | `t_src` | Text | 50,4,200,20 | txt_maxl=24, font=0 |
| 5 | `g_title` | Scrolling text | 124,44,266,32 | txt_maxl=80, font=1 |
| 6 | `t_artist` | Text | 124,80,266,24 | txt_maxl=40, font=0 |
| 7 | `t_info` | Text | 124,106,266,22 | txt_maxl=40, font=0 |
| 8 | `b_mute` | Button | 14,146,60,50 | sends touch event |
| 9 | `b_prev` | Button | 104,146,60,50 | sends touch event |
| 10 | `bt_play` | Dual-state button | 170,146,60,50 | sends touch event, val 0/1 |
| 11 | `b_next` | Button | 236,146,60,50 | sends touch event |
| 12 | `b_list` | Button | 326,146,60,50 | sends touch event |
| 13 | `h_vol` | Slider | 52,208,286,24 | minval=0, maxval=21, sends touch event |
| 14 | `t_vol` | Text | 344,208,46,24 | txt_maxl=3, font=0 |

#### Page 2 `list` (background pic 5, pressed pic 6)

| ID | objname | type | x,y,w,h | notes |
|---|---|---|---|---|
| 1 | `b_back` | Button | 0,0,44,28 | sends touch event, display runs `page home` |
| 2 | `t_bat` | Text | 300,4,40,20 | txt_maxl=4, font=0 |
| 3 | `j_bat` | Progress bar | 349,10,30,9 |  |
| 4 | `t_ltitle` | Text | 50,4,240,20 | txt_maxl=24, font=0 |
| 5 | `b_row0` | Button | 8,34,338,34 | txt_maxl=48, font=0, sends touch event |
| 6 | `b_row1` | Button | 8,72,338,34 | txt_maxl=48, font=0, sends touch event |
| 7 | `b_row2` | Button | 8,110,338,34 | txt_maxl=48, font=0, sends touch event |
| 8 | `b_row3` | Button | 8,148,338,34 | txt_maxl=48, font=0, sends touch event |
| 9 | `b_row4` | Button | 8,186,338,34 | txt_maxl=48, font=0, sends touch event |
| 10 | `b_up` | Button | 352,34,40,91 | sends touch event |
| 11 | `b_down` | Button | 352,129,40,91 | sends touch event |

#### Page 3 `settings` (background pic 7, pressed pic 8)

| ID | objname | type | x,y,w,h | notes |
|---|---|---|---|---|
| 1 | `b_back` | Button | 0,0,44,28 | sends touch event, display runs `page home` |
| 2 | `t_bat` | Text | 300,4,40,20 | txt_maxl=4, font=0 |
| 3 | `j_bat` | Progress bar | 349,10,30,9 |  |
| 4 | `h_dim` | Slider | 160,42,222,28 | minval=10, maxval=100, sends touch event, display sets `dim` itself on move |
| 5 | `bt_led` | Dual-state button | 302,84,80,34 | sends touch event, val 0/1 |
| 6 | `h_led` | Slider | 160,132,222,28 | minval=0, maxval=255, sends touch event |
| 7 | `t_ip` | Text | 14,182,372,22 | txt_maxl=40, font=0 |
| 8 | `t_ver` | Text | 14,206,372,22 | txt_maxl=40, font=0 |

#### Page 4 `files` (background pic 10, pressed pic 11)

| ID | objname | type | x,y,w,h | notes |
|---|---|---|---|---|
| 1 | `b_back` | Button | 0,0,44,28 | sends touch event, display runs `page home` |
| 2 | `t_bat` | Text | 300,4,40,20 | txt_maxl=4, font=0 |
| 3 | `j_bat` | Progress bar | 349,10,30,9 |  |
| 4 | `t_path` | Text | 50,4,150,20 | txt_maxl=40, font=0 |
| 5 | `t_cnt` | Text | 200,4,48,20 | txt_maxl=7, font=0 |
| 6 | `b_parent` | Button | 252,0,44,28 | sends touch event |
| 7 | `q_ic0` | Crop | 8,34,34,34 | picc 10 empty / 12 folder / 13 audio file |
| 8 | `b_f0` | Button | 46,34,300,34 | txt_maxl=48, font=0, sends touch event |
| 9 | `q_ic1` | Crop | 8,72,34,34 | picc 10 empty / 12 folder / 13 audio file |
| 10 | `b_f1` | Button | 46,72,300,34 | txt_maxl=48, font=0, sends touch event |
| 11 | `q_ic2` | Crop | 8,110,34,34 | picc 10 empty / 12 folder / 13 audio file |
| 12 | `b_f2` | Button | 46,110,300,34 | txt_maxl=48, font=0, sends touch event |
| 13 | `q_ic3` | Crop | 8,148,34,34 | picc 10 empty / 12 folder / 13 audio file |
| 14 | `b_f3` | Button | 46,148,300,34 | txt_maxl=48, font=0, sends touch event |
| 15 | `q_ic4` | Crop | 8,186,34,34 | picc 10 empty / 12 folder / 13 audio file |
| 16 | `b_f4` | Button | 46,186,300,34 | txt_maxl=48, font=0, sends touch event |
| 17 | `b_fup` | Button | 352,34,40,91 | sends touch event |
| 18 | `b_fdown` | Button | 352,129,40,91 | sends touch event |


### What the ESP32 fills in
- home: `t_time` (HH:MM, NTP when WiFi, else last known / RTC), `t_date`, `t_temp` / `t_hum` / `t_pres` (BME280), `t_mode` (WiFi · Radio / Bluetooth / SD), `g_now` (now playing), `t_bat` + `j_bat` on every page.
- player: `t_src`, `g_title`, `t_artist`, `t_info` (bitrate, station n/N, album…), `bt_play.val`, `h_vol.val` + `t_vol`.
- list (radio stations): `t_ltitle`, `b_row0..4.txt`, scrolling with `b_up` / `b_down`.
- files (SD browser): `t_path`, `t_cnt` ("1–5/23"), `b_f0..4.txt` + `q_ic0..4.picc`, `b_parent` = folder up, tap folder = open, tap file = play + `page player`.
- settings: `h_dim`, `bt_led`, `h_led` (restore from NVS when the page opens), `t_ip`, `t_ver`.
- At boot send the stored display brightness (`dim=…`); on sleep send `sleep=1`, on wake `sleep=0`.
- Every touch event and every value change from the display must also be reported over the network API (milestone 7) so the app and the display stay in sync.

### Source / mode switching (UX)
- Home buttons: **Radio** (`b_radio`) = WiFi internet radio, **SD card** (`b_sd`) = SD player (files page), **Bluetooth** (`b_bt`) = A2DP sink.
- Bluetooth and WiFi do not fit together → switching between BT and Radio/SD stores the target mode in NVS, shows
  "Switching to …" (`t_mode` / `g_now`) and restarts; after boot the firmware opens the right page by itself (`page player` / `page files`).
- SD playback does not need WiFi, but if WiFi is up (for upload/app) it must still fit in RAM – measure free heap and log it.
- Player `b_list` opens `list` (stations) in radio mode and `files` in SD mode; in BT mode it does nothing.
- Remember the last source, station/file and volume in NVS and resume on power-up.

### WiFi
- No credentials in the code, only in the git-ignored `include/secrets.h`. No web page, no captive portal and no WiFi setup from the app (owner decision): WiFi is set only in `secrets.h`. The OTA library (esp-ota-updater) is not changed for this.
- Re-run the setup from the settings page: hold Button 1 for 5 s while the settings page is open (show a confirmation on the display).
- Show IP + RSSI in `t_ip`, firmware version + battery voltage in `t_ver`. mDNS `soundbar.local`.

## Controls
| Input | Action |
|---|---|
| Encoder turn | volume (also update `h_vol`/`t_vol`) |
| Encoder short press | play / pause |
| Encoder long press (≥ 2 s) | power off = deep sleep (mute DAC first, LEDs off, display `sleep=1`); wake with encoder press |
| Button 1 / 2 / 3 | previous / play-pause / next |
| Touch pad (G4) | configurable in settings/app – default: toggle underglow LEDs (the hardware test uses it to toggle bass boost) |
| Display | as per pages above |

## Features / milestones
Status: milestones 1–9 implemented in one go (firmware 0.1.0), not yet tested on the hardware.

0. **Hardware test** (former `firmware_test` project, deleted): Bluetooth sink + display + inputs + battery + I2C scan + bass-boost EQ. ✔ plays music; bass judged too weak → DSP required (see *Audio chain*). SD, LEDs and touch were wired after the test – verify them in milestone 1 (SD file list in Serial, LED test pattern, touch readings).
1. **Project structure**: modules, `config.h`, NVS settings (volume, mode, brightness, LED, last station), logging.
2. **Bluetooth mode** polished: name "ESP32 Soundbar", auto-reconnect, metadata on display, AVRCP from buttons/encoder.
   Includes the **bass boost / loudness DSP chain** from *Audio chain* (shared by all sources later).
   Sync volume with the phone (AVRCP absolute volume) both ways; show "Bluetooth – waiting" / device name on the display; pairing always allowed.
3. **WiFi + internet radio**: WiFi from `secrets.h`, station list (JSON on LittleFS, editable later from the app), list page, MP3/AAC streams – choose a decoder library that works **without PSRAM** on the chosen core.
4. **microSD player**: card stays inside the case, FAT32; files page browser, play MP3 (FLAC optional if RAM allows).
5. **Home screen data**: NTP clock + date (Europe/Prague, DST), BME280 every 30 s, BH1750 → automatic display `dim` and LED brightness.
6. **Underglow LEDs** (WS2812B, GPIO14): on/off (`bt_led`), brightness (`h_led`), colour + effects from the app, optional music-reactive (VU), auto-dim with BH1750, limit brightness on low battery, all off in sleep. Keep total current modest (40 LEDs × 60 mA full white = 2.4 A – cap brightness so LEDs + amp stay within the 2.1 A boost).
7. **Network API for the Android app**: REST + WebSocket (status, play/pause, volume, source, stations CRUD, settings), web upload of files to the SD card, simple web UI for testing, mDNS `soundbar.local`.
8. **OTA updates** (needs the partition change above).
9. **Power management**: deep sleep, low-battery handling, auto-off after inactivity, quick resume of last source.
10. Android app – separate project later; the API from step 7 is its contract.

## Not in scope / decided
- No IR remote, no external audio jack, no external microSD slot.
- Firmware must not rely on a 5 V signal anywhere – all ESP32 inputs are 3.3 V.
- Network API is the project's own REST + WebSocket (documented in README.md), not esp-device-control: that library
  owns the HTTP server on port 80, so the SD upload could not share it.
- No web interface and no setup portal (owner decision). WiFi only from `secrets.h`.
- Display texts are English (as in this brief); the fonts are ISO-8859-2, so Czech metadata is shown correctly.
- OTA is checked only in Radio/SD mode (WiFi is off in Bluetooth mode). No firmware upload over the network (no free slot).
