# ESP32 Soundbar – firmware

Firmware pro DIY bateriový stereo reproduktor: Bluetooth reproduktor, internetové rádio, přehrávač z microSD,
Nextion displej, hodiny + počasí, podsvícení WS2812B a REST/WebSocket API pro aplikaci.

Piny, moduly a rozložení displeje jsou v [docs/CLAUDE.md](docs/CLAUDE.md), zadání Android aplikace v [docs/ANDROID_APP.md](docs/ANDROID_APP.md). Projekt displeje je v [display-preset](display-preset).

## Režimy

| Režim | Co běží | Přepnutí |
|---|---|---|
| **Bluetooth** | A2DP sink „ESP32 Soundbar“, AVRCP, metadata, synchronizace hlasitosti s telefonem | domů → *Bluetooth* |
| **Rádio** | WiFi, stanice z `/stations.json` (LittleFS), MP3/AAC streamy, ICY metadata | domů → *Radio* |
| **SD karta** | MP3 / AAC (ADTS) / WAV z karty, ID3 tagy, procházení složek | domů → *SD card* |

Bluetooth a WiFi se do RAM nevejdou zároveň (ESP32 bez PSRAM), proto přepnutí mezi Bluetooth a Rádiem/SD uloží
režim do NVS a restartuje. Rádio ↔ SD se přepíná bez restartu. Po zapnutí se obnoví poslední režim, stanice,
soubor (i pozice v něm) a hlasitost.

## Nahrání

Flash je rozdělený takto (`partitions.csv`):

| Oddíl | Velikost | Obsah |
|---|---|---|
| `factory` | 1,1 MB | **updater** – malý firmware, který jen stáhne novou verzi z GitHubu |
| `app0` | 2,75 MB | **soundbar** – hlavní firmware |
| `spiffs` | 64 kB | LittleFS se seznamem stanic |

Hlavní firmware má ~2,4 MB (Bluetooth + WiFi + TLS + dekodéry), takže se nevejde do dvou OTA slotů po 1,9 MB.
Proto je OTA řešené updaterem v oddílu `factory`.

Poprvé přes USB (pořadí je důležité):

```
pio run -e updater -t upload
pio run -e soundbar -t upload
pio device monitor
```

Ve VS Code: PlatformIO → *Project Tasks* → `updater` → *Upload*, potom `soundbar` → *Upload* a *Monitor*.
Updater stačí nahrát jednou, potom už jen `soundbar` (výchozí prostředí).

## První spuštění

1. Po prvním nahrání startuje v režimu **Bluetooth** – v telefonu se připoj k **ESP32 Soundbar**.
2. Na displeji domů → **Radio**: reproduktor se restartuje do WiFi režimu. Když nezná žádnou síť, spustí
   přístupový bod **ESP32-Soundbar-Setup** (zobrazí se na displeji). Připoj se k němu telefonem, otevře se
   stránka WiFiManageru (jinak `http://192.168.4.1`), vyber síť a zadej heslo. Po připojení se spustí rádio.
3. Znovu spustit nastavení WiFi: na stránce *settings* drž **tlačítko 1** 5 s, nebo příkaz `{"cmd":"wifiSetup"}`.

Údaje k WiFi ukládá WiFiManager do NVS. Updater (esp-ota-updater) je použije přes `WiFi.begin()`, protože nemá vlastní uložené sítě.

## Ovládání

| Vstup | Akce |
|---|---|
| enkodér – otáčení | hlasitost 0–21 |
| enkodér – stisk | play / pauza |
| enkodér – držet 2 s | vypnutí (deep sleep), probuzení stiskem enkodéru |
| tlačítko 1 / 2 / 3 | předchozí / play-pauza / další (rádio = stanice, SD = soubor) |
| tlačítko 1 – držet 5 s na stránce *settings* | nastavení WiFi |
| dotykový šroub | podle nastavení: podsvícení (výchozí) / bass boost / play-pauza / nic |
| displej | stránky home, player, list (stanice), files (SD), settings |

Automatické vypnutí po 30 min bez přehrávání a bez ovládání (nastavitelné, 0 = nikdy). Pod ~3,3 V baterie
se reproduktor uspí, pod 10 % bliká na displeji varování a podsvícení se ztlumí.

## Zvuk (DSP)

Každý zdroj jde přes stejný řetězec: high-pass 65 Hz → low-shelf 120 Hz (bass 0–10 × 0,9 dB + loudness až
+6 dB při nízké hlasitosti, max +12 dB) → hlasitost snížená o zesílení basů (headroom) → měkký limiter.
Nastavení (bass zap/vyp, úroveň, loudness) je v API. Konstanty jsou v `include/config.h` (`DSP_*`, `VOLUME_*`).

## Aktualizace firmwaru (OTA)

1. Zvyš `custom_version` v `platformio.ini`, commit a push do `main`.
2. GitHub Actions (`.github/workflows/release.yml`) sestaví `soundbar` a vydá release s `firmware.bin` a `version.txt`.
3. Reproduktor v režimu Rádio/SD po připojení k WiFi zkontroluje verzi. Když je novější, restartuje do updateru,
   ten firmware stáhne, zapíše do `app0` a nabootuje ho. Ručně příkazem `{"cmd":"ota"}`.

V režimu Bluetooth je WiFi vypnutá, aktualizace se tam nekontroluje. Repozitář `davidvancl/smart-speaker`
(`custom_ota_repo`) musí existovat a mít povolené Actions.

## API

Aplikace používá API na portu 80 (spouští se po připojení k WiFi). mDNS: `soundbar.local`, služba `_http._tcp`.

### REST

| Metoda | Cesta | Tělo / parametry | Popis |
|---|---|---|---|
| GET | `/api/status` | – | kompletní stav (viz níže) |
| POST | `/api/cmd` | `{"cmd":"…","value":…}` | příkaz (tabulka níže) |
| GET | `/api/stations` | – | `[{"name","url"}]` |
| POST | `/api/stations` | `{"name","url"}` | přidat stanici |
| PUT | `/api/stations` | `[{"name","url"}]` | nahradit celý seznam |
| POST | `/api/stations/edit` | `{"index","name","url"}` | upravit stanici |
| POST | `/api/stations/move` | `{"from","to"}` | přesunout stanici |
| DELETE | `/api/stations?index=n` | – | smazat stanici |
| GET | `/api/files?path=/` | – | obsah složky na SD `{path,total,used,entries:[{name,dir,size,audio}]}` |
| DELETE | `/api/files?path=…` | – | smazat soubor / složku |
| POST | `/api/mkdir?path=…` | – | vytvořit složku |
| POST | `/api/upload?path=/slozka` | multipart, pole `file` | nahrát soubor na SD |

### Příkazy (`/api/cmd` i WebSocket)

| `cmd` | `value` | |
|---|---|---|
| `toggle`, `play`, `pause`, `next`, `prev`, `mute` | – | přehrávání |
| `volume` | 0–21 | hlasitost |
| `volumeUp`, `volumeDown` | – | hlasitost ±1 |
| `source` | `bluetooth` / `radio` / `sd` | zdroj (BT ↔ WiFi = restart) |
| `station` | index | přehrát stanici |
| `file` | cesta | přehrát soubor ze SD |
| `settings` | objekt, viz níže | změnit nastavení (ukládá se do NVS) |
| `restart`, `wifiSetup`, `ota` | – | systém |

Nastavení: `bass` (bool), `bassLevel` (0–10), `loudness` (bool), `dim` (10–100), `autoDim` (bool), `led` (bool),
`ledBrightness` (0–255), `ledColor` (`"#RRGGBB"`), `ledEffect` (`solid`/`breathe`/`rainbow`/`vu`/`wave`),
`touchAction` (`leds`/`bass`/`play`/`none`), `tempOffset` (°C, −10…10), `autoOff` (minuty, 0 = nikdy),
`volume`, `muted`.

### WebSocket `ws://soundbar.local/ws`

- Po připojení a při každé změně: `{"t":"status","status":{…}}` – stejný objekt jako `/api/status`.
- Každý dotyk na displeji: `{"t":"touch","page":1,"id":10}`.
- Klient posílá stejné příkazy jako `/api/cmd`, odpověď `{"t":"ack"}` nebo `{"t":"err","error":"…"}`.

Stav obsahuje: `fw`, `mode`, `source`, `playing`, `muted`, `volume`, `volumeMax`, `title`, `artist`, `album`,
`info`, `message`, `bluetooth{connected,device}`, `radio{station,name,count,bitrate}`, `sd{ready,file,index,count}`,
`battery{voltage,percent,low}`, `climate{temperature,humidity,pressure}`, `lux`, `wifi{state,ssid,ip,rssi}`,
`time`, `date`, `page` (aktuální stránka displeje), `heap`, `settings{…}`.

## Ladění

Každý modul loguje do Serial Monitoru s prefixem (`[bt]`, `[stream]`, `[nx]`, `[sd]`, `[sensors]`, `[net]`, …).

- **Ticho:** SCK na PCM → GND, propojky H1/H2/H4 na L, XSMT na G2. Log `[audio]` ukazuje hlasitost a zesílení v dB.
- **Dotyk na displeji dělá něco jiného:** log `[nx] touch page X id Y` – čísla jsou v `src/display/ui.cpp` (`namespace Id`).
- **Enkodér jde obráceně / po dvou krocích:** `ENC_STEPS_PER_DETENT` v `include/config.h` nebo prohoď piny v `encoder.attachFullQuad()` v `src/input.cpp`.
- **LED mají špatné barvy:** při startu blikne červená → zelená → modrá; jinak změň `NeoGrbFeature` v `src/leds.cpp`.
- **Dotykový šroub nereaguje / spíná sám:** log `[input] touch … baseline …`, prahy `TOUCH_*` v `include/config.h`.
- **SD karta:** FAT32; log `[sd]` vypíše kořen karty. Když se nikdy neinicializuje: EN modulu → 3V3.
- **Rádio se zasekává:** `[stream] buffer underrun` – zvětši `STREAM_BUFFER_BYTES`; volná paměť je v logu `[main]`/`[power]`.
