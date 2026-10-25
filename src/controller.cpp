#include "controller.h"

#include "audio/bt_sink.h"
#include "audio/output.h"
#include "audio/stream_player.h"
#include "clock.h"
#include "config.h"
#include "display/ui.h"
#include "input.h"
#include "leds.h"
#include "net/network.h"
#include "net/api.h"
#include "power.h"
#include "sd_card.h"
#include "sensors.h"
#include "stations.h"
#include "texts.h"

namespace Controller {

namespace {

QueueHandle_t queue = nullptr;
SemaphoreHandle_t jsonMutex = nullptr;
Status state;
uint32_t rev = 1;
uint32_t restartAt = 0;
uint32_t messageUntil = 0;
uint32_t activityAt = 0;
bool radioPending = false;
std::vector<String> playlist;
String json;
uint32_t jsonRev = 0;
uint32_t jsonSettingsRev = 0;
uint32_t jsonAt = 0;
uint8_t appliedDim = 0;
float ambient = 1.0f;

SettingsData& cfg() {
  return Settings::data();
}

void changed() {
  rev++;
}

void activity() {
  activityAt = millis();
}

void message(const String& text, uint32_t durationMs = 0) {
  state.message = text;
  messageUntil = durationMs ? millis() + durationMs : 0;
  Serial.printf("[ctrl] message: %s\n", text.c_str());
  changed();
}

void clearMeta() {
  state.title = "";
  state.artist = "";
  state.album = "";
  state.bitrate = 0;
  changed();
}

bool streaming() {
  return state.phase == StreamPhase::Connecting || state.phase == StreamPhase::Buffering ||
         state.phase == StreamPhase::Playing;
}

void applyAudio() {
  DspParams p;
  p.volume = cfg().volume;
  p.muted = state.muted;
  p.bass = cfg().bass;
  p.bassLevel = cfg().bassLevel;
  p.loudness = cfg().loudness;
  AudioOut::setParams(p);
}

void applyLeds() {
  Leds::configure(cfg().led, cfg().ledBrightness, cfg().ledColor, cfg().ledEffect);
}

void applyDim(bool force = false) {
  uint8_t dim = cfg().dim;
  if (cfg().autoDim) dim = max<uint8_t>(10, static_cast<uint8_t>(dim * ambient + 0.5f));
  if (!force && abs(static_cast<int>(dim) - appliedDim) < 3) return;
  appliedDim = dim;
  Ui::setBrightness(dim);
}

void applyAll() {
  state.volume = cfg().volume;
  applyAudio();
  applyLeds();
  Sensors::setTempOffset(cfg().tempOffset);
  applyDim(true);
}

void updateAmbient() {
  if (!Sensors::lightOk()) return;
  float lux = Sensors::lux();
  float t = (log10f(max(lux, 0.01f)) - log10f(Cfg::LUX_DARK)) / (log10f(Cfg::LUX_BRIGHT) - log10f(Cfg::LUX_DARK));
  float target = Cfg::AMBIENT_MIN_FACTOR + (1.0f - Cfg::AMBIENT_MIN_FACTOR) * constrain(t, 0.0f, 1.0f);
  ambient += (target - ambient) * 0.1f;
  if (cfg().autoDim) {
    Leds::setAmbient(ambient);
    applyDim();
  } else {
    Leds::setAmbient(1.0f);
  }
}

void savePosition() {
  if (state.mode != Mode::Sd || cfg().file.isEmpty()) return;
  uint32_t pos = StreamPlayer::filePosition();
  if (pos == cfg().filePos) return;
  cfg().filePos = pos;
  Settings::changed();
}

void scheduleRestart(const String& text) {
  message(text);
  AudioOut::enableDac(false);
  restartAt = millis() + Cfg::RESTART_DELAY_MS;
}

void loadPlaylist(const String& path) {
  playlist.clear();
  std::vector<SdEntry> entries;
  String dir = SdCard::parent(path);
  SdCard::list(dir, entries, true);
  for (const SdEntry& e : entries) {
    if (!e.dir) playlist.push_back(SdCard::join(dir, e.name));
  }
  state.fileIndex = -1;
  for (size_t i = 0; i < playlist.size(); i++) {
    if (playlist[i] == path) state.fileIndex = i;
  }
  state.fileCount = playlist.size();
}

void startFile(const String& path, uint32_t offset) {
  if (!SdCard::ready()) {
    message(Text::NO_SD, 3000);
    return;
  }
  String p = SdCard::normalize(path);
  if (!SdCard::exists(p)) {
    message(String("Missing ") + SdCard::fileName(p), 3000);
    return;
  }
  cfg().file = p;
  cfg().filePos = offset;
  Settings::changed();
  loadPlaylist(p);
  clearMeta();
  String name = SdCard::fileName(p);
  int dot = name.lastIndexOf('.');
  state.title = dot > 0 ? name.substring(0, dot) : name;
  String folder = SdCard::fileName(SdCard::parent(p));
  state.artist = folder;
  state.message = "";
  state.phase = StreamPhase::Connecting;
  state.playing = true;
  Serial.printf("[ctrl] play file %s (%d/%d) from %u\n", p.c_str(), state.fileIndex + 1, state.fileCount, (unsigned)offset);
  StreamPlayer::playFile(p, offset);
  changed();
}

void startStation(int index) {
  int count = Stations::count();
  if (count == 0) {
    message(Text::NO_STATIONS, 3000);
    return;
  }
  index = (index % count + count) % count;
  Station s;
  if (!Stations::get(index, s)) return;
  if (cfg().station != index) {
    cfg().station = index;
    Settings::changed();
  }
  clearMeta();
  state.stationName = s.name;
  state.title = s.name;
  if (Network::state() != Network::State::Connected) {
    radioPending = true;
    state.phase = StreamPhase::Idle;
    state.playing = false;
    message(Network::portalActive() ? String(Text::WIFI_SETUP) + Cfg::SETUP_AP : String(Text::WIFI_CONNECTING));
    return;
  }
  radioPending = false;
  state.message = "";
  state.phase = StreamPhase::Connecting;
  state.playing = true;
  Serial.printf("[ctrl] play station %d %s\n", index + 1, s.url.c_str());
  StreamPlayer::playUrl(s.url);
  changed();
}

void splitIcyTitle(const String& text) {
  int sep = text.indexOf(" - ");
  if (sep > 0) {
    state.artist = text.substring(0, sep);
    state.title = text.substring(sep + 3);
  } else {
    state.artist = state.stationName;
    state.title = text.length() ? text : state.stationName;
  }
}

void onStreamMeta(MetaField field, const String& text) {
  if (state.mode == Mode::Radio) {
    if (field == MetaField::Title) splitIcyTitle(text);
    if (field == MetaField::Name && text.length() && state.title == state.stationName) state.album = text;
  } else if (state.mode == Mode::Sd && text.length()) {
    if (field == MetaField::Title) state.title = text;
    if (field == MetaField::Artist) state.artist = text;
    if (field == MetaField::Album) state.album = text;
  }
  changed();
}

void onStreamState(StreamPhase phase) {
  state.phase = phase;
  state.playing = streaming();
  if (phase == StreamPhase::Failed) message(Text::STREAM_FAILED, 5000);
  if (phase == StreamPhase::Buffering && state.mode == Mode::Radio) message(Text::BUFFERING, 1500);
  if (phase == StreamPhase::Playing && state.message == Text::BUFFERING) state.message = "";
  if (phase == StreamPhase::Paused) savePosition();
  changed();
}

void onStreamEnded() {
  cfg().filePos = 0;
  Settings::changed();
  if (state.fileIndex >= 0 && state.fileIndex + 1 < static_cast<int>(playlist.size())) {
    startFile(playlist[state.fileIndex + 1], 0);
  } else {
    state.phase = StreamPhase::Idle;
    state.playing = false;
    message(Text::STOPPED, 3000);
  }
}

void onBtConnection(bool connected) {
  state.btConnected = connected;
  if (connected) {
    BtSink::setVolume(cfg().volume);
    Ui::show(Ui::Player);
  } else {
    state.playing = false;
    state.btDevice = "";
    clearMeta();
    Ui::show(Ui::Home);
  }
  changed();
}

void onBtMeta(MetaField field, const String& text) {
  if (field == MetaField::Title) state.title = text;
  if (field == MetaField::Artist) state.artist = text;
  if (field == MetaField::Album) state.album = text;
  changed();
}

void handle(const Command& c) {
  activity();
  String text(c.text);
  switch (c.cmd) {
    case Cmd::TogglePlay:
      togglePlay();
      break;
    case Cmd::Play:
      if (!state.playing) togglePlay();
      break;
    case Cmd::Pause:
      if (state.playing) togglePlay();
      break;
    case Cmd::Next:
      next();
      break;
    case Cmd::Previous:
      previous();
      break;
    case Cmd::ToggleMute:
      toggleMute();
      break;
    case Cmd::Volume:
      setVolume(c.a);
      break;
    case Cmd::VolumeDelta:
      changeVolume(c.a);
      break;
    case Cmd::Source: {
      Mode m;
      if (Settings::parseMode(c.text, m)) selectMode(m);
      break;
    }
    case Cmd::Station:
      playStation(c.a);
      break;
    case Cmd::File:
      playFile(text);
      break;
    case Cmd::Settings: {
      JsonDocument doc;
      if (!deserializeJson(doc, text)) applySettings(doc.as<JsonObjectConst>());
      break;
    }
    case Cmd::Restart:
      scheduleRestart(text.length() ? text : String("Restarting..."));
      break;
    case Cmd::WifiSetup:
      startWifiSetup();
      break;
    case Cmd::Ota:
      Network::requestOta();
      break;
    case Cmd::StationsChanged:
      changed();
      break;
    case Cmd::BtConnection:
      onBtConnection(c.a != 0);
      break;
    case Cmd::BtPlaying:
      state.playing = c.a != 0;
      changed();
      break;
    case Cmd::BtMeta:
      onBtMeta(static_cast<MetaField>(c.a), text);
      break;
    case Cmd::BtVolume:
      state.muted = false;
      cfg().volume = constrain(c.a, 0, Cfg::VOLUME_MAX);
      state.volume = cfg().volume;
      Settings::changed();
      applyAudio();
      changed();
      break;
    case Cmd::StreamState:
      onStreamState(static_cast<StreamPhase>(c.a));
      break;
    case Cmd::StreamMeta:
      onStreamMeta(static_cast<MetaField>(c.a), text);
      break;
    case Cmd::StreamInfo:
      state.bitrate = c.a;
      changed();
      break;
    case Cmd::StreamEnded:
      onStreamEnded();
      break;
  }
}

void handleInput() {
  int steps = Input::takeEncoderSteps();
  if (steps) changeVolume(steps);

  switch (Input::poll()) {
    case Input::Event::EncoderClick:
    case Input::Event::Button2:
      Serial.println("[input] play/pause");
      togglePlay();
      break;
    case Input::Event::EncoderLong:
      powerOff("encoder long press");
      break;
    case Input::Event::Button1:
      Serial.println("[input] previous");
      previous();
      break;
    case Input::Event::Button3:
      Serial.println("[input] next");
      next();
      break;
    case Input::Event::Button1Hold:
      Serial.println("[input] button 1 held");
      if (Ui::page() == Ui::SettingsPage) startWifiSetup();
      break;
    case Input::Event::Touch:
      Serial.printf("[input] touch pad -> %s\n", Settings::touchName(cfg().touchAction));
      if (cfg().touchAction == TouchAction::Leds) setLed(!cfg().led);
      if (cfg().touchAction == TouchAction::Bass) toggleBass();
      if (cfg().touchAction == TouchAction::PlayPause) togglePlay();
      break;
    default:
      break;
  }
}

void updateNetwork() {
  static Network::State last = Network::State::Off;
  Network::State now = Network::state();
  if (now == last) return;
  last = now;
  Serial.printf("[ctrl] network %s\n", Network::stateName());
  if (now == Network::State::Portal) message(String(Text::WIFI_SETUP) + Cfg::SETUP_AP);
  if (now == Network::State::Failed && state.mode == Mode::Radio) message(Text::WIFI_FAILED);
  if (now == Network::State::Connected) {
    Api::begin();
    if (state.message.startsWith(Text::WIFI_SETUP) || state.message == Text::WIFI_CONNECTING ||
        state.message == Text::WIFI_FAILED) {
      state.message = "";
    }
    if (radioPending && state.mode == Mode::Radio) startStation(cfg().station);
  }
  changed();
}

void updatePower() {
  if (Power::critical()) powerOff("battery critical");
  static bool wasLow = false;
  bool low = Power::low();
  Leds::setLowBattery(Power::percent() < Cfg::BATTERY_LED_LIMIT_PERCENT && Power::present());
  if (low && !wasLow) message(Text::BATTERY_LOW, 10000);
  wasLow = low;

  if (state.playing) activity();
  uint32_t last = max(activityAt, Input::lastActivity());
  if (cfg().autoOff && !state.playing && millis() - last > cfg().autoOff * 60000UL) powerOff("inactivity");
}

void buildJson() {
  JsonDocument doc;
  const SettingsData& s = cfg();
  doc["fw"] = FW_VERSION;
  doc["mode"] = Settings::modeName(state.mode);
  doc["source"] = sourceLabel();
  doc["playing"] = state.playing;
  doc["muted"] = state.muted;
  doc["volume"] = state.volume;
  doc["volumeMax"] = Cfg::VOLUME_MAX;
  doc["title"] = state.title;
  doc["artist"] = state.artist;
  doc["album"] = state.album;
  doc["info"] = infoLine();
  doc["message"] = state.message;

  JsonObject bt = doc["bluetooth"].to<JsonObject>();
  bt["connected"] = state.btConnected;
  bt["device"] = state.btDevice;

  JsonObject radio = doc["radio"].to<JsonObject>();
  radio["station"] = s.station;
  radio["name"] = state.stationName;
  radio["count"] = state.mode == Mode::Bluetooth ? 0 : Stations::count();
  radio["bitrate"] = state.bitrate;

  JsonObject sd = doc["sd"].to<JsonObject>();
  sd["ready"] = SdCard::ready();
  sd["file"] = s.file;
  sd["index"] = state.fileIndex;
  sd["count"] = state.fileCount;

  JsonObject bat = doc["battery"].to<JsonObject>();
  bat["voltage"] = roundf(Power::voltage() * 100) / 100;
  bat["percent"] = Power::percent();
  bat["low"] = Power::low();

  if (Sensors::climateOk()) {
    JsonObject env = doc["climate"].to<JsonObject>();
    env["temperature"] = roundf(Sensors::temperature() * 10) / 10;
    env["humidity"] = roundf(Sensors::humidity());
    env["pressure"] = roundf(Sensors::pressure());
  }
  if (Sensors::lightOk()) doc["lux"] = roundf(Sensors::lux());

  JsonObject net = doc["wifi"].to<JsonObject>();
  net["state"] = Network::stateName();
  net["ssid"] = Network::ssid();
  net["ip"] = Network::ip();
  net["rssi"] = Network::rssi();

  doc["time"] = Clock::time();
  doc["date"] = Clock::date();
  doc["page"] = Ui::page();
  doc["heap"] = ESP.getFreeHeap();

  JsonObject set = doc["settings"].to<JsonObject>();
  set["bass"] = s.bass;
  set["bassLevel"] = s.bassLevel;
  set["loudness"] = s.loudness;
  set["dim"] = s.dim;
  set["autoDim"] = s.autoDim;
  set["led"] = s.led;
  set["ledBrightness"] = s.ledBrightness;
  char color[8];
  snprintf(color, sizeof(color), "#%06X", static_cast<unsigned>(s.ledColor & 0xFFFFFF));
  set["ledColor"] = color;
  set["ledEffect"] = Settings::effectName(s.ledEffect);
  set["touchAction"] = Settings::touchName(s.touchAction);
  set["tempOffset"] = s.tempOffset;
  set["autoOff"] = s.autoOff;

  String out;
  serializeJson(doc, out);
  bool differs;
  {
    xSemaphoreTake(jsonMutex, portMAX_DELAY);
    differs = out != json;
    if (differs) json = out;
    xSemaphoreGive(jsonMutex);
  }
  if (differs) Api::broadcastStatus(out);
}

void updateJson() {
  uint32_t now = millis();
  if (now - jsonAt < Cfg::STATUS_MIN_INTERVAL_MS) return;
  bool dirty = jsonRev != rev || jsonSettingsRev != Settings::revision() || now - jsonAt > Cfg::STATUS_REFRESH_MS;
  if (!dirty) return;
  jsonRev = rev;
  jsonSettingsRev = Settings::revision();
  jsonAt = now;
  buildJson();
}

uint32_t parseColor(JsonVariantConst v, uint32_t fallback) {
  if (v.is<uint32_t>()) return v.as<uint32_t>() & 0xFFFFFF;
  const char* text = v.as<const char*>();
  if (!text) return fallback;
  if (*text == '#') text++;
  char* end;
  uint32_t value = strtoul(text, &end, 16);
  return (*end == 0 && strlen(text) == 6) ? value : fallback;
}

}

void init() {
  queue = xQueueCreate(Cfg::COMMAND_QUEUE, sizeof(Command));
  jsonMutex = xSemaphoreCreateMutex();
}

bool post(Cmd cmd, int32_t a, int32_t b, const char* text) {
  if (!queue) return false;
  Command c;
  c.cmd = cmd;
  c.a = a;
  c.b = b;
  strlcpy(c.text, text ? text : "", sizeof(c.text));
  if (xQueueSend(queue, &c, 0) != pdTRUE) {
    Serial.printf("[ctrl] queue full, dropped command %d\n", static_cast<int>(cmd));
    return false;
  }
  return true;
}

void begin() {
  state.mode = cfg().mode;
  applyAll();
  activity();
  Serial.printf("[ctrl] mode %s, free heap %u\n", Settings::modeName(state.mode), (unsigned)ESP.getFreeHeap());

  switch (state.mode) {
    case Mode::Bluetooth:
      Ui::show(Ui::Home);
      break;
    case Mode::Radio:
      Ui::show(Ui::Player);
      startStation(cfg().station);
      break;
    case Mode::Sd:
      if (cfg().file.length() && SdCard::exists(cfg().file)) {
        Ui::browseTo(SdCard::parent(cfg().file));
        Ui::show(Ui::Player);
        startFile(cfg().file, cfg().filePos);
      } else {
        Ui::show(Ui::Files);
      }
      break;
  }
  changed();
}

void loop() {
  Command c;
  while (xQueueReceive(queue, &c, 0) == pdTRUE) handle(c);

  if (restartAt && static_cast<int32_t>(millis() - restartAt) > 0) {
    Settings::saveNow();
    Serial.println("[ctrl] restart");
    Serial.flush();
    ESP.restart();
  }
  if (messageUntil && static_cast<int32_t>(millis() - messageUntil) > 0) {
    messageUntil = 0;
    state.message = "";
    changed();
  }

  handleInput();
  updateNetwork();
  updatePower();
  updateAmbient();

  if (state.mode == Mode::Bluetooth && state.btConnected) {
    String name = BtSink::deviceName();
    if (name != state.btDevice) {
      state.btDevice = name;
      changed();
    }
  }
  updateJson();
}

const Status& status() {
  return state;
}

uint32_t revision() {
  return rev + Settings::revision();
}

String sourceLabel() {
  switch (state.mode) {
    case Mode::Bluetooth:
      return state.btDevice.length() ? String(Text::BLUETOOTH) + ": " + state.btDevice : String(Text::BLUETOOTH);
    case Mode::Radio:
      return Text::RADIO;
    default:
      return Text::SD;
  }
}

String modeLabel() {
  switch (state.mode) {
    case Mode::Bluetooth:
      return state.btConnected ? sourceLabel() : String(Text::BT_WAITING);
    case Mode::Radio:
      return Network::state() == Network::State::Connected ? String("WiFi - ") + Text::RADIO : String(Text::RADIO);
    default:
      return Network::state() == Network::State::Connected ? String("WiFi - ") + Text::SD : String(Text::SD);
  }
}

String infoLine() {
  String info;
  if (state.mode == Mode::Radio) {
    int count = Stations::count();
    info = state.stationName;
    if (count) info += " (" + String(cfg().station + 1) + "/" + String(count) + ")";
    if (state.bitrate) info += " " + String(state.bitrate) + "k";
  } else if (state.mode == Mode::Sd) {
    if (state.fileIndex >= 0) info = String(state.fileIndex + 1) + "/" + String(state.fileCount);
    if (state.album.length()) info += (info.length() ? " " : "") + state.album;
  } else {
    info = state.album;
  }
  if (state.muted) info = String(Text::MUTED) + (info.length() ? " | " + info : "");
  return info;
}

String nowPlaying() {
  if (state.message.length()) return state.message;
  if (state.mode == Mode::Bluetooth && !state.btConnected) return Text::BT_WAITING;
  if (!state.title.length()) return state.mode == Mode::Radio ? state.stationName : String();
  if (!state.artist.length() || state.artist == state.title) return state.title;
  return state.title + " - " + state.artist;
}

void togglePlay() {
  activity();
  switch (state.mode) {
    case Mode::Bluetooth:
      if (!state.btConnected) return;
      if (state.playing) BtSink::pause();
      else BtSink::play();
      break;
    case Mode::Radio:
      if (streaming()) StreamPlayer::pause();
      else if (state.phase == StreamPhase::Paused) StreamPlayer::resume();
      else startStation(cfg().station);
      break;
    case Mode::Sd:
      if (streaming()) StreamPlayer::pause();
      else if (state.phase == StreamPhase::Paused) StreamPlayer::resume();
      else if (cfg().file.length()) startFile(cfg().file, cfg().filePos);
      break;
  }
}

void next() {
  activity();
  switch (state.mode) {
    case Mode::Bluetooth:
      BtSink::next();
      break;
    case Mode::Radio:
      startStation(cfg().station + 1);
      break;
    case Mode::Sd:
      if (!playlist.empty()) startFile(playlist[(state.fileIndex + 1) % playlist.size()], 0);
      break;
  }
}

void previous() {
  activity();
  switch (state.mode) {
    case Mode::Bluetooth:
      BtSink::previous();
      break;
    case Mode::Radio:
      startStation(cfg().station - 1);
      break;
    case Mode::Sd:
      if (!playlist.empty()) startFile(playlist[(state.fileIndex - 1 + playlist.size()) % playlist.size()], 0);
      break;
  }
}

void setVolume(int volume) {
  activity();
  volume = constrain(volume, 0, Cfg::VOLUME_MAX);
  state.muted = false;
  if (cfg().volume != volume) {
    cfg().volume = volume;
    Settings::changed();
  }
  state.volume = volume;
  applyAudio();
  if (state.mode == Mode::Bluetooth) BtSink::setVolume(volume);
  changed();
}

void changeVolume(int delta) {
  setVolume(static_cast<int>(cfg().volume) + delta);
}

void toggleMute() {
  activity();
  state.muted = !state.muted;
  applyAudio();
  changed();
}

void selectMode(Mode mode) {
  activity();
  if (mode == state.mode) {
    if (mode == Mode::Radio && !streaming()) startStation(cfg().station);
    return;
  }
  Serial.printf("[ctrl] switch %s -> %s\n", Settings::modeName(state.mode), Settings::modeName(mode));
  savePosition();
  if ((state.mode == Mode::Bluetooth) != (mode == Mode::Bluetooth)) {
    cfg().mode = mode;
    Settings::changed();
    Settings::saveNow();
    String target = mode == Mode::Bluetooth ? Text::BLUETOOTH : mode == Mode::Radio ? Text::RADIO : Text::SD;
    scheduleRestart(String(Text::SWITCHING_TO) + target + "...");
    return;
  }
  StreamPlayer::stop();
  state.mode = mode;
  state.phase = StreamPhase::Idle;
  state.playing = false;
  state.fileIndex = -1;
  state.fileCount = 0;
  state.message = "";
  cfg().mode = mode;
  Settings::changed();
  clearMeta();
  if (mode == Mode::Radio) {
    startStation(cfg().station);
  } else if (cfg().file.length()) {
    Ui::browseTo(SdCard::parent(cfg().file));
  }
}

void playStation(int index) {
  activity();
  if (state.mode != Mode::Radio) {
    cfg().station = index;
    selectMode(Mode::Radio);
    return;
  }
  startStation(index);
}

void playFile(const String& path) {
  activity();
  if (state.mode == Mode::Bluetooth) {
    cfg().file = path;
    cfg().filePos = 0;
    selectMode(Mode::Sd);
    return;
  }
  if (state.mode != Mode::Sd) selectMode(Mode::Sd);
  startFile(path, 0);
}

void setLed(bool on) {
  cfg().led = on;
  Settings::changed();
  applyLeds();
  changed();
}

void setLedBrightness(uint8_t brightness) {
  cfg().ledBrightness = brightness;
  Settings::changed();
  applyLeds();
  changed();
}

void setDim(uint8_t dim) {
  cfg().dim = constrain(dim, 10, 100);
  Settings::changed();
  applyDim(true);
  changed();
}

void toggleBass() {
  cfg().bass = !cfg().bass;
  Settings::changed();
  applyAudio();
  message(cfg().bass ? "Bass boost on" : "Bass boost off", 2000);
}

void startWifiSetup() {
  Serial.println("[ctrl] WiFi setup requested");
  Settings::setFlag("wifiSetup");
  if (cfg().mode == Mode::Bluetooth) cfg().mode = Mode::Radio;
  Settings::changed();
  Settings::saveNow();
  scheduleRestart(Text::WIFI_SETUP_RESTART);
}

void powerOff(const char* reason) {
  Serial.printf("[ctrl] power off: %s\n", reason);
  savePosition();
  Settings::saveNow();
  AudioOut::enableDac(false);
  if (state.mode == Mode::Bluetooth) BtSink::end();
  else StreamPlayer::stop();
  Leds::off();
  Ui::powerOff();
  while (Input::encoderHeld()) delay(10);
  delay(200);
  Power::deepSleep();
}

bool applySettings(JsonObjectConst in) {
  SettingsData& s = cfg();
  bool audio = false, leds = false, dim = false;
  for (JsonPairConst kv : in) {
    const char* key = kv.key().c_str();
    JsonVariantConst v = kv.value();
    if (!strcmp(key, "volume")) {
      setVolume(v.as<int>());
    } else if (!strcmp(key, "muted")) {
      if (v.as<bool>() != state.muted) toggleMute();
    } else if (!strcmp(key, "bass")) {
      s.bass = v.as<bool>();
      audio = true;
    } else if (!strcmp(key, "bassLevel")) {
      s.bassLevel = constrain(v.as<int>(), 0, Cfg::DSP_BASS_LEVEL_MAX);
      audio = true;
    } else if (!strcmp(key, "loudness")) {
      s.loudness = v.as<bool>();
      audio = true;
    } else if (!strcmp(key, "dim")) {
      s.dim = constrain(v.as<int>(), 10, 100);
      dim = true;
    } else if (!strcmp(key, "autoDim")) {
      s.autoDim = v.as<bool>();
      dim = true;
    } else if (!strcmp(key, "led")) {
      s.led = v.as<bool>();
      leds = true;
    } else if (!strcmp(key, "ledBrightness")) {
      s.ledBrightness = constrain(v.as<int>(), 0, 255);
      leds = true;
    } else if (!strcmp(key, "ledColor")) {
      s.ledColor = parseColor(v, s.ledColor);
      leds = true;
    } else if (!strcmp(key, "ledEffect")) {
      Settings::parseEffect(v.as<const char*>(), s.ledEffect);
      leds = true;
    } else if (!strcmp(key, "touchAction")) {
      Settings::parseTouch(v.as<const char*>(), s.touchAction);
    } else if (!strcmp(key, "tempOffset")) {
      s.tempOffset = constrain(v.as<float>(), -10.0f, 10.0f);
      Sensors::setTempOffset(s.tempOffset);
    } else if (!strcmp(key, "autoOff")) {
      s.autoOff = constrain(v.as<int>(), 0, 240);
    } else {
      Serial.printf("[ctrl] unknown setting %s\n", key);
    }
  }
  Settings::changed();
  if (audio) applyAudio();
  if (leds) applyLeds();
  if (dim) {
    if (!s.autoDim) Leds::setAmbient(1.0f);
    applyDim(true);
  }
  changed();
  return true;
}

String statusJson() {
  if (!jsonMutex) return "{}";
  xSemaphoreTake(jsonMutex, portMAX_DELAY);
  String copy = json;
  xSemaphoreGive(jsonMutex);
  return copy;
}

}
