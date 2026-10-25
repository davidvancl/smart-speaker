# smart-speaker

Firmware for a DIY battery-powered stereo speaker ("ESP32 Soundbar"), running on an ESP32. It works as a Bluetooth speaker, an internet radio and a player for a microSD card. It also has a Nextion touch display with a clock and weather, WS2812B underglow LEDs and a REST/WebSocket API for an Android app. The firmware updates itself from GitHub Releases.

A printable user manual in Czech is in [docs/manual.pdf](docs/manual.pdf). Its source is [docs/manual.html](docs/manual.html).

Pins, modules and the display layout in detail are in [docs/CLAUDE.md](docs/CLAUDE.md). The brief for the Android app is in [docs/ANDROID_APP.md](docs/ANDROID_APP.md), the Nextion Editor project is in [display-preset](display-preset).

## Hardware

- ESP32 DevKitC (ESP32-WROOM-32D, 4 MB flash, no PSRAM, `esp32dev`)
- PCM5102 I2S DAC → passive divider 22k/4.7k → PAM8403 amplifier (2× 3 W) → 2× Roadstar VO-401 4" speakers in closed boxes (~0.9 l each)
- Nextion NX4024K032 3.2" touch display (400×240)
- KY-040 rotary encoder with a push button, 3 push buttons, a capacitive touch pad (M3 screw on the top)
- ≈40 WS2812B LEDs as underglow below the case
- BME280 (temperature, humidity, pressure) and BH1750 (ambient light) on I2C
- LaskaKit μŠup microSD module
- 1S2P Li-ion 6400 mAh, IP5306 charger/boost module (5 V 2.1 A, USB-C), rocker switch

Wiring:

| Signal | GPIO |
|---|---|
| Nextion RX / TX | 16 / 17 |
| I2S BCK / LRCK / DATA (PCM5102) | 26 / 25 / 27 |
| PCM5102 XSMT (soft mute) | 2 |
| I2C SDA / SCL | 21 / 22 |
| microSD SCK / MISO / MOSI / CS | 18 / 19 / 23 / 5 |
| Encoder CLK / DT / SW | 32 / 33 / 13 |
| Button 1 / 2 / 3 | 39 / 34 / 35 |
| Touch pad | 4 |
| LED strip DIN | 14 |
| Battery voltage (divider 100k/100k) | 36 |

All pins and constants are in [include/config.h](include/config.h).

## Modes

| Mode | What runs | Switching |
|---|---|---|
| **Bluetooth** | A2DP sink "ESP32 Soundbar", AVRCP, metadata, volume synced with the phone | home → *Bluetooth* |
| **Radio** | WiFi, stations from `/stations.json` (LittleFS), MP3/AAC streams, ICY metadata | home → *Radio* |
| **SD card** | MP3 / AAC (ADTS) / WAV from the card, ID3 tags, folder browser | home → *SD card* |

Bluetooth and WiFi do not fit into the RAM together (ESP32 without PSRAM), so switching between Bluetooth and Radio/SD saves the mode to NVS and restarts. Radio ↔ SD switches without a restart. After power-on the last mode, station, file (including the position in it) and volume are restored.

## Controls

| Input | Action |
|---|---|
| Encoder – turn | volume 0–21 |
| Encoder – press | play / pause |
| Encoder – hold 2 s | power off (deep sleep), wake up by pressing the encoder |
| Button 1 / 2 / 3 | previous / play-pause / next (radio = station, SD = file) |
| Touch pad | as set: LEDs on/off (default) / bass boost / play-pause / nothing |
| Display | pages home, player, list (stations), files (SD), settings |

The speaker turns itself off after 30 min without playback and without any input (configurable, 0 = never). Below ~3.3 V of the battery it goes to deep sleep, below 10 % a warning is shown on the display.

## LEDs

| State | LEDs |
|---|---|
| Startup | red → green → blue test, then off |
| WiFi connection and update check (also in the updater) | blinking orange |
| Normal operation | solid red (default), colour, effect and brightness can be changed in the app |
| Battery below 20 % | brightness limited |
| Powered off | off |

Effects: `solid`, `breathe`, `rainbow`, `vu` (reacts to the music), `wave`. The brightness follows the ambient light sensor when auto-dim is on, and the total current is capped (`LED_BUDGET_MA`).

## Sound (DSP)

Every source goes through the same chain in [src/audio/dsp.cpp](src/audio/dsp.cpp):

1. 4th-order high-pass at 70 Hz – the closed boxes cannot play lower, it only wastes amplifier power.
2. Low-shelf at 150 Hz – bass level 0–10 × 0.9 dB, plus loudness up to +6 dB at low volume, max +12 dB.
3. Volume, lowered by the bass boost (headroom).
4. Peak limiter at `DSP_LIMITER_THRESHOLD` = the level where the PAM8403 starts clipping.

Volume is linear in dB from `VOLUME_MIN_DB` (−51 dB, step 1) to 0 dB (step 21), step 0 is mute. Bass on/off, bass level and loudness are in the settings (display, API). The constants are `DSP_*` and `VOLUME_*` in [include/config.h](include/config.h).

## WiFi

Copy [include/secrets.example.h](include/secrets.example.h) to `include/secrets.h` (it is in `.gitignore`) and fill it in. Up to 4 networks are supported, remove the ones you don't need:

```cpp
#define SECRET_SSID "wifi-name"
#define SECRET_PASS "wifi-password"

#define SECRET_SSID_2 "second-wifi-name"
#define SECRET_PASS_2 "second-wifi-password"
```

There is no web page and no setup portal, WiFi is set only here. The networks visible in a scan are tried first, in this order. On the first successful connection the networks are saved to EEPROM. The updater and firmware built by GitHub Actions have no `secrets.h` and use the saved networks. To change them, edit `secrets.h` and upload the firmware over USB again.

## Build and upload

Requires [PlatformIO](https://platformio.org/) (CLI or the VS Code extension).

The flash is split like this ([partitions.csv](partitions.csv)):

| Partition | Size | Content |
|---|---|---|
| `factory` | 1.1 MB | **updater** – a small firmware that only downloads the new version from GitHub |
| `app0` | 2.75 MB | **soundbar** – the main firmware |
| `spiffs` | 64 kB | LittleFS with the station list |

The main firmware is ~2.4 MB (Bluetooth + WiFi + TLS + decoders), so it does not fit into two OTA slots. The OTA update is therefore done by the updater in the `factory` partition.

First upload over USB (the order matters):

```
pio run -e updater -t upload
pio run -e soundbar -t upload
pio device monitor
```

The updater is uploaded only once, after that only `soundbar` (the default environment). On the first start the speaker is in Bluetooth mode – connect to **ESP32 Soundbar** from the phone.

## Source layout

- `src/main.cpp` – `setup()` and `loop()` only
- `src/controller.cpp` – central state, modes, playback commands, settings, status JSON, power off
- `src/settings.cpp` – settings in NVS
- `src/audio/` – DSP (`dsp.cpp`), I2S output and mute (`output.cpp`), Bluetooth sink (`bt_sink.cpp`), radio and SD player task (`stream_player.cpp`)
- `src/display/` – Nextion protocol (`nextion.cpp`), UTF-8 → ISO-8859-2 (`latin2.cpp`), pages and touch IDs (`ui.cpp`)
- `src/input.cpp` – encoder, buttons, touch pad
- `src/leds.cpp` – WS2812B effects, status blinking, current cap
- `src/power.cpp`, `src/sensors.cpp`, `src/clock.cpp` – battery and deep sleep, BME280 + BH1750, NTP time
- `src/stations.cpp`, `src/sd_card.cpp` – station list on LittleFS, SD card access
- `src/net/network.cpp` – WiFi task, mDNS, update check
- `src/net/api.cpp` – REST + WebSocket API
- `src/updater/main.cpp` – the updater (`updater` environment)
- `include/config.h` – pins and constants, `include/texts.h` – display texts
- `include/WifiCredentials.h` – uses `secrets.h` if it exists

## Firmware update (OTA)

Uses the [esp-ota-updater](https://github.com/davidvancl/esp-ota-updater) library. In Radio or SD mode the speaker checks the latest release after connecting to WiFi. When it is newer, it restarts into the updater, which downloads the firmware, writes it to `app0` and boots it. The check can also be started with the `ota` command. In Bluetooth mode WiFi is off and no check runs.

Releasing a new version:
1. Increase `custom_version` in `platformio.ini`.
2. Commit and push to `main`.
3. GitHub Actions ([.github/workflows/release.yml](.github/workflows/release.yml)) publish a release with `firmware.bin` and `version.txt`. The speaker updates on its next connection in Radio/SD mode.

Do not push a locally changed version in `platformio.ini` unless you want to publish a release.

## API

The Android app uses the API on port 80, started once WiFi is connected (Radio and SD mode only). mDNS: `soundbar.local`, service `_http._tcp`. No authentication.

### REST

| Method | Path | Body / parameters | |
|---|---|---|---|
| GET | `/api/status` | – | complete state (see below) |
| POST | `/api/cmd` | `{"cmd":"…","value":…}` | command (table below) |
| GET | `/api/stations` | – | `[{"name","url"}]` |
| POST | `/api/stations` | `{"name","url"}` | add a station |
| PUT | `/api/stations` | `[{"name","url"}]` | replace the whole list |
| POST | `/api/stations/edit` | `{"index","name","url"}` | edit a station |
| POST | `/api/stations/move` | `{"from","to"}` | move a station |
| DELETE | `/api/stations?index=n` | – | delete a station |
| GET | `/api/files?path=/` | – | SD folder `{path,total,used,entries:[{name,dir,size,audio}]}` |
| DELETE | `/api/files?path=…` | – | delete a file / folder |
| POST | `/api/mkdir?path=…` | – | create a folder |
| POST | `/api/upload?path=/folder` | multipart, field `file` | upload a file to the SD card |

### Commands (`/api/cmd` and WebSocket)

| `cmd` | `value` | |
|---|---|---|
| `toggle`, `play`, `pause`, `next`, `prev`, `mute` | – | playback |
| `volume` | 0–21 | volume |
| `volumeUp`, `volumeDown` | – | volume ±1 |
| `source` | `bluetooth` / `radio` / `sd` | source (Bluetooth ↔ WiFi = restart) |
| `station` | index | play a station |
| `file` | path | play a file from the SD card |
| `settings` | object, see below | change settings (saved to NVS) |
| `restart`, `ota` | – | system |

Settings: `bass` (bool), `bassLevel` (0–10), `loudness` (bool), `dim` (10–100), `autoDim` (bool), `led` (bool), `ledBrightness` (0–255), `ledColor` (`"#RRGGBB"`), `ledEffect` (`solid`/`breathe`/`rainbow`/`vu`/`wave`), `touchAction` (`leds`/`bass`/`play`/`none`), `tempOffset` (°C, −10…10), `autoOff` (minutes, 0 = never), `volume`, `muted`.

### WebSocket `ws://soundbar.local/ws`

- After connecting and on every change: `{"t":"status","status":{…}}` – the same object as `/api/status`.
- Every touch on the display: `{"t":"touch","page":1,"id":10}`.
- The client sends the same commands as to `/api/cmd`, the reply is `{"t":"ack"}` or `{"t":"err","error":"…"}`.

The status contains `fw`, `mode`, `source`, `playing`, `muted`, `volume`, `volumeMax`, `title`, `artist`, `album`, `info`, `message`, `bluetooth{connected,device}`, `radio{station,name,count,bitrate}`, `sd{ready,file,index,count}`, `battery{voltage,percent,low}`, `climate{temperature,humidity,pressure}`, `lux`, `wifi{state,ssid,ip,rssi}`, `time`, `date`, `page` (the open display page), `heap` and `settings{…}`.

## Troubleshooting

Every module logs to the serial monitor (115200 baud) with a prefix (`[bt]`, `[stream]`, `[nx]`, `[sd]`, `[sensors]`, `[net]`, …).

| Problem | What to check |
|---|---|
| No sound | SCK of the PCM5102 to GND, jumpers H1/H2/H4 to L, XSMT on G2. The `[audio]` log shows the volume and gain in dB. |
| A touch on the display does something else | log `[nx] touch page X id Y`, the IDs are in `src/display/ui.cpp` (`namespace Id`) |
| Encoder turns the wrong way / two steps per click | `ENC_STEPS_PER_DETENT` in `include/config.h`, or swap the pins in `encoder.attachFullQuad()` in `src/input.cpp` |
| Wrong LED colours | the startup test should be red → green → blue, otherwise change `NeoGrbFeature` in `src/leds.cpp` |
| Touch pad does not react / triggers by itself | log `[input] touch … baseline …`, thresholds `TOUCH_*` in `include/config.h` |
| SD card | FAT32, the `[sd]` log lists the root. If it never initialises: EN of the module → 3V3 |
| Radio stutters | `[stream] buffer underrun` – increase `STREAM_BUFFER_BYTES`, free memory is in the `[main]` / `[power]` log |
| Distortion at high volume | lower `DSP_LIMITER_THRESHOLD` (e.g. 0.5) |
| No WiFi | `[net] connection failed` – check `include/secrets.h` and upload over USB |

## License

[MIT](LICENSE)
