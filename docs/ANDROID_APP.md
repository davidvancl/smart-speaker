# ESP32 Soundbar – Android app brief for Claude

Copy this file into the root of the Android Studio project as `CLAUDE.md`.

You are building the Android app that controls a DIY battery-powered stereo speaker ("ESP32 Soundbar") over the
local WiFi network. The speaker firmware is finished and lives in
`C:\Users\vancl\Documents\workspace\smart-speaker` (PlatformIO, ESP32). Its `README.md` (section *API*) is the source of
truth for the API; this document summarizes it. **The owner speaks Czech – answer in Czech**, no code comments.

## How to work on this project
- The owner builds and runs the app from Android Studio on a real phone against the real speaker. You cannot see
  either, so log network traffic (`Log.d`) in debug builds and ask for Logcat output when something does not work.
- Work in small steps, each one buildable and testable on its own (milestones below).
- Do not change the API on the app side only. If the app needs something the firmware does not offer, say so –
  API changes are made in the firmware first (`smart-speaker/src/net/api.cpp`, documented in its `README.md`).
- Pin exact dependency versions (version catalog `gradle/libs.versions.toml`).
- Unknown JSON fields must be ignored, missing optional fields must not crash the app (newer/older firmware).

## Stack
- Kotlin, Jetpack Compose (Material 3), single activity, ViewModel + StateFlow, Coroutines.
- OkHttp for REST and WebSocket (`pingInterval(15, SECONDS)`), kotlinx.serialization (`ignoreUnknownKeys = true`).
- `NsdManager` for discovery, DataStore for the remembered speaker (name, last IP).
- Storage Access Framework (`OpenMultipleDocuments`) to pick audio files for upload.
- `minSdk` 26.

## Things that matter about the speaker
- **Three sources:** `bluetooth` (A2DP speaker for the phone), `radio` (internet radio over WiFi), `sd` (player for the
  microSD card inside the case).
- **The API exists only in `radio` and `sd` mode.** In Bluetooth mode the speaker's WiFi is off (Bluetooth and WiFi do
  not fit into the ESP32's RAM together), so the app cannot reach it. Show a clear state for that case: "The speaker
  is in Bluetooth mode – switch to Radio or SD on the speaker's display". It is still useful to show the last known
  status, greyed out.
- Switching **to Bluetooth** from the app (`source` = `bluetooth`) restarts the speaker into Bluetooth mode, and it
  disappears from the network. Ask for confirmation first. Switching between `radio` and `sd` is instant.
- `restart` and a firmware update also drop the connection. Reconnect with backoff (1, 2, 5, 10 s…).
- **No authentication**, plain `http://` and `ws://` in the LAN. Add a `network_security_config` that allows cleartext
  traffic (Android 9+ blocks it by default).
- Volume is 0–21 (`volumeMax`). The display, the knob on the speaker and the phone (in Bluetooth mode) change it too,
  so always take it from the latest status.

## Discovery
- mDNS host `soundbar.local`, service `_http._tcp` on port 80, instance name `soundbar`.
- Use `NsdManager` to find `_http._tcp` services named `soundbar` and resolve the IP. Android resolves `.local` host
  names unreliably, so connect by IP, never by host name.
- Remember the last IP and try it first. Allow entering an IP manually (the speaker shows its IP on the *settings*
  page of its display).
- One speaker is enough; no multi-device support needed.

## WiFi
The speaker's WiFi network is set only in the firmware (`secrets.h`), not from the app. There is no WiFi setup in the
app and no web page on the speaker. `wifi.state` values: `connecting`, `connected`, `failed`, `off` (Bluetooth mode).

## API

Base URL `http://<ip>`. JSON everywhere. Errors: HTTP 4xx/5xx with `{"ok":false,"error":"…"}`. Success of
modifying calls: `{"ok":true}`. Timeout 3 s for normal calls (uploads: no total timeout, see below).

### WebSocket `ws://<ip>/ws` – the main channel
- Right after connecting and on every change the speaker sends `{"t":"status","status":{…}}`
  (at most every 200 ms; at least every ~2 s while something changes, e.g. the clock).
- `{"t":"touch","page":1,"id":10}` – someone touched the speaker's display. Informational, the status follows.
- The app sends commands as `{"cmd":"…","value":…}` (table below). Reply: `{"t":"ack"}` or
  `{"t":"err","error":"bad json"|"rejected"}`. Replies have no id, they come in order. `ack` means "accepted"; the
  actual result arrives in the next `status`. Do not update the UI optimistically except for the control being dragged.
- Use the WebSocket for everything except stations, files and uploads. Close it when the app goes to background,
  reconnect when it returns.

### Commands (WebSocket or `POST /api/cmd` with the same JSON)

| `cmd` | `value` | Meaning |
|---|---|---|
| `toggle` / `play` / `pause` | – | playback |
| `next` / `prev` | – | radio: next/previous station, SD: next/previous file in the folder, BT: track |
| `mute` | – | toggle mute |
| `volume` | int 0–21 | set volume (debounce a slider to ~100 ms) |
| `volumeUp` / `volumeDown` | – | ±1 |
| `source` | `"bluetooth"` / `"radio"` / `"sd"` | switch source (to Bluetooth = restart, see above) |
| `station` | int index | play station |
| `file` | string path, e.g. `"/Music/song.mp3"` | play file from the SD card |
| `settings` | object, keys below | change settings (stored on the speaker); send only changed keys, serialized object max ~230 bytes |
| `restart` | – | restart the speaker |
| `ota` | – | check GitHub for a newer firmware; if there is one the speaker installs it and restarts (~1 min) |

### Settings keys (`settings` object, also returned in `status.settings`)

| Key | Type | Meaning |
|---|---|---|
| `bass` | bool | bass boost on/off |
| `bassLevel` | int 0–10 | bass boost strength |
| `loudness` | bool | extra bass at low volume |
| `dim` | int 10–100 | display brightness (%) |
| `autoDim` | bool | display and LED brightness follow the ambient light sensor |
| `led` | bool | underglow LEDs on/off |
| `ledBrightness` | int 0–255 | LED brightness |
| `ledColor` | string `"#RRGGBB"` | LED colour |
| `ledEffect` | `"solid"` / `"breathe"` / `"rainbow"` / `"vu"` / `"wave"` | LED effect (`vu` = reacts to music) |
| `touchAction` | `"leds"` / `"bass"` / `"play"` / `"none"` | what the touch screw on top does |
| `tempOffset` | float −10…10 | °C added to the temperature sensor (it sits inside the warm case) |
| `autoOff` | int 0–240 | minutes without playback before the speaker turns off (0 = never) |
| `volume`, `muted` | int / bool | also accepted here |

### Status object

```json
{
  "fw": "0.1.0", "mode": "radio", "source": "Radio", "playing": true, "muted": false,
  "volume": 8, "volumeMax": 21,
  "title": "Song", "artist": "Artist", "album": "", "info": "ČRo Radiožurnál (1/7) 128k", "message": "",
  "bluetooth": {"connected": false, "device": ""},
  "radio": {"station": 0, "name": "ČRo Radiožurnál", "count": 7, "bitrate": 128},
  "sd": {"ready": true, "file": "/Music/song.mp3", "index": 2, "count": 14},
  "battery": {"voltage": 3.92, "percent": 70, "low": false},
  "climate": {"temperature": 22.4, "humidity": 41, "pressure": 1013},
  "lux": 120,
  "wifi": {"state": "connected", "ssid": "Home", "ip": "192.168.1.23", "rssi": -61},
  "time": "14:05", "date": "Saturday 25.10.2026", "page": 1, "heap": 81234,
  "settings": {"bass": true, "bassLevel": 6, "loudness": true, "dim": 80, "autoDim": true, "led": true,
               "ledBrightness": 96, "ledColor": "#FF0000", "ledEffect": "solid", "touchAction": "leds",
               "tempOffset": -2.0, "autoOff": 30}
}
```

- `mode` is the source id (`bluetooth`/`radio`/`sd`), `source` a human label.
- `message` is a short-lived notice from the speaker ("Buffering...", "Stream not available", "Battery low!"…) –
  show it as a snackbar / subtitle while it is non-empty.
- `info` is a ready-made second line (station n/N and bitrate, file n/N and album, "Muted").
- `climate` and `lux` are missing when the sensor is not found. `sd.index` is −1 when no file is playing.
- `page` is the page open on the speaker's display (0 home, 1 player, 2 stations, 3 settings, 4 files) – not needed in
  the UI.

### Stations (REST)

| Method | Path | Body | |
|---|---|---|---|
| GET | `/api/stations` | – | `[{"name","url"}]`, index = position |
| POST | `/api/stations` | `{"name","url"}` | add at the end |
| PUT | `/api/stations` | `[{"name","url"}]` | replace the whole list |
| POST | `/api/stations/edit` | `{"index","name","url"}` | edit |
| POST | `/api/stations/move` | `{"from","to"}` | move (for drag & drop reordering) |
| DELETE | `/api/stations?index=n` | – | delete |

Name must not be empty, URL must start with `http://` or `https://` (MP3 or AAC stream; `http` is preferred, HTTPS
costs the speaker a lot of RAM). Max 64 stations. Invalid input → 400 `invalid station`. Reload the list after every
change. The station currently playing is `status.radio.station`.

### SD card (REST)

| Method | Path | | |
|---|---|---|---|
| GET | `/api/files?path=/Music` | → `{"path","total","used","entries":[{"name","dir","size","audio"}]}` | folders first, sorted; 503 = no SD card, 404 = not a folder |
| DELETE | `/api/files?path=/Music/a.mp3` | | file or whole folder |
| POST | `/api/mkdir?path=/Music/New` | | |
| POST | `/api/upload?path=/Music` | multipart/form-data, one file part (field name `file`, the part's filename is used) | upload into the folder; an existing file is overwritten |

- URL-encode `path`. Playable files: `.mp3`, `.aac` (ADTS), `.wav` (16 bit) – `audio` is true for them.
- Upload one file per request, sequentially, with a progress bar (OkHttp `RequestBody` that counts written bytes).
  Upload speed is not measured yet (estimate a few hundred kB/s, i.e. tens of seconds per song). No total timeout,
  but a 30 s write/read timeout.
  `{"ok":true}` arrives after the whole file is written.
- Play a file with the `file` command, then the speaker continues with the next file in the same folder.

## Screens
1. **Connection** – searching / found / manual IP / "speaker is in Bluetooth mode" / reconnecting.
2. **Player** (main screen) – source selector (Bluetooth / Radio / SD), title, artist, `info`, `message`,
   play-pause, previous, next, mute, volume slider 0–21, battery %, temperature.
3. **Radio stations** – list with the playing one highlighted, tap = play, add / edit / delete, drag to reorder.
4. **SD card** – folder browser, tap folder = open, tap file = play, delete (with confirmation), new folder,
   upload files from the phone into the current folder (multiple, with progress, cancel), used/total space.
5. **Settings** – Sound (bass, bass level, loudness), Display (brightness, auto brightness), Lights (on/off,
   brightness, colour picker, effect), Touch button action, Temperature offset, Auto off, System (firmware version,
   WiFi SSID/RSSI/IP read-only, check for update, restart – the last two with confirmation).

Sliders: debounce sends (~100 ms), apply incoming status to every control except the one being dragged.

## Milestones
1. Project skeleton, discovery + manual IP, WebSocket connection, Player screen with live status and commands.
2. Settings screen.
3. Radio stations screen.
4. SD card browser + upload.
5. Polish: reconnect/background handling, Bluetooth-mode state, error messages, dark theme.

## Testing without the speaker
Make the base URL configurable in debug builds, so a small mock server (e.g. a Ktor or Python script returning the
status above) can stand in for the speaker. Unit-test the JSON models against the example above.
