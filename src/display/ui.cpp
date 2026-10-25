#include "display/ui.h"

#include <vector>

#include "clock.h"
#include "config.h"
#include "controller.h"
#include "display/nextion.h"
#include "net/network.h"
#include "net/api.h"
#include "power.h"
#include "sd_card.h"
#include "sensors.h"
#include "settings.h"
#include "stations.h"
#include "texts.h"

namespace Ui {

namespace {

namespace Id {
constexpr uint8_t HOME_RADIO = 10, HOME_SD = 11, HOME_BT = 12, HOME_SETTINGS = 13;
constexpr uint8_t PLAYER_BACK = 1, PLAYER_MUTE = 8, PLAYER_PREV = 9, PLAYER_PLAY = 10, PLAYER_NEXT = 11,
                  PLAYER_LIST = 12, PLAYER_VOLUME = 13;
constexpr uint8_t LIST_ROW0 = 5, LIST_UP = 10, LIST_DOWN = 11;
constexpr uint8_t SET_DIM = 4, SET_LED = 5, SET_LED_LEVEL = 6;
constexpr uint8_t FILES_PARENT = 6, FILES_ROW0 = 8, FILES_UP = 17, FILES_DOWN = 18;
}

enum Tag : uint8_t { TagVolume = 1, TagDim, TagLed, TagLedLevel };

constexpr const char* PAGE_NAMES[] = {"home", "player", "list", "settings", "files"};
constexpr uint8_t ROWS = 5;
constexpr uint8_t ICON_EMPTY = 10, ICON_FOLDER = 12, ICON_FILE = 13;

uint32_t refreshedAt = 0;
int listOffset = 0;
String browseDir = "/";
std::vector<SdEntry> entries;
int filesOffset = 0;
bool filesLoaded = false;

String batteryText() {
  if (!Power::present()) return "--";
  return String(Power::percent()) + "%";
}

void common() {
  Nextion::setText("t_bat", batteryText(), 4);
  Nextion::setValue("j_bat.val", Power::percent());
}

String climateText(float value, int decimals, const char* unit) {
  if (!Sensors::climateOk() || isnan(value)) return "--";
  return String(value, decimals) + unit;
}

void refreshHome() {
  bool blink = Power::low() && (millis() / 2000) % 2;
  Nextion::setText("t_mode", blink ? String(Text::BATTERY_LOW) : Controller::modeLabel(), 20);
  Nextion::setText("t_time", Clock::time(), 5);
  Nextion::setText("t_date", Clock::date(), 24);
  Nextion::setText("t_temp", climateText(Sensors::temperature(), 1, "°C"), 8);
  Nextion::setText("t_hum", climateText(Sensors::humidity(), 0, " %"), 8);
  Nextion::setText("t_pres", climateText(Sensors::pressure(), 0, " hPa"), 10);
  Nextion::setText("g_now", Controller::nowPlaying(), 80);
}

void refreshPlayer() {
  const Status& s = Controller::status();
  String title = s.message.length() ? s.message : s.title;
  if (!title.length() && s.mode == Mode::Bluetooth && !s.btConnected) title = Text::BT_WAITING;
  Nextion::setText("t_src", Controller::sourceLabel(), 24);
  Nextion::setText("g_title", title, 80);
  Nextion::setText("t_artist", s.artist, 40);
  Nextion::setText("t_info", Controller::infoLine(), 40);
  Nextion::setValue("bt_play.val", s.playing ? 1 : 0);
  Nextion::setValue("h_vol.val", s.volume);
  Nextion::setText("t_vol", String(s.volume), 3);
}

void refreshList() {
  int count = Stations::count();
  if (listOffset >= count) listOffset = max(0, (count - 1) / ROWS * ROWS);
  String title = count ? String(Text::STATIONS) + " " + String(listOffset + 1) + "-" +
                             String(min(listOffset + ROWS, count)) + "/" + String(count)
                       : String(Text::NO_STATIONS);
  Nextion::setText("t_ltitle", title, 24);
  int currentStation = Settings::data().station;
  for (uint8_t i = 0; i < ROWS; i++) {
    Station st;
    String text;
    int index = listOffset + i;
    if (Stations::get(index, st)) text = String(index == currentStation ? "> " : "") + String(index + 1) + ". " + st.name;
    Nextion::setText((String("b_row") + i).c_str(), text, 48);
  }
}

void loadFiles() {
  filesLoaded = true;
  SdCard::list(browseDir, entries, true);
  if (filesOffset >= static_cast<int>(entries.size())) filesOffset = 0;
  Serial.printf("[ui] files %s: %u entries\n", browseDir.c_str(), (unsigned)entries.size());
}

void refreshFiles() {
  if (!filesLoaded) loadFiles();
  int count = entries.size();
  String path = SdCard::ready() ? browseDir : String(Text::NO_SD);
  if (path.length() > 40) path = "..." + path.substring(path.length() - 37);
  Nextion::setText("t_path", path, 40);
  String cnt;
  if (count) {
    cnt = String(filesOffset + 1) + "-" + String(min(filesOffset + ROWS, count)) + "/" + String(count);
    if (cnt.length() > 7) cnt = String(filesOffset + 1) + "/" + String(count);
  }
  Nextion::setText("t_cnt", cnt, 7);
  for (uint8_t i = 0; i < ROWS; i++) {
    int index = filesOffset + i;
    bool used = index < count;
    Nextion::setText((String("b_f") + i).c_str(), used ? entries[index].name : String(), 48);
    Nextion::setValue((String("q_ic") + i + ".picc").c_str(),
                      !used ? ICON_EMPTY : entries[index].dir ? ICON_FOLDER : ICON_FILE);
  }
}

String ipText() {
  if (Settings::data().mode == Mode::Bluetooth) return Text::WIFI_OFF_BT;
  switch (Network::state()) {
    case Network::State::Connected:
      return "IP " + Network::ip() + "  " + String(Network::rssi()) + " dBm";
    case Network::State::Portal:
      return String(Text::WIFI_SETUP) + Cfg::SETUP_AP;
    case Network::State::Connecting:
      return Text::WIFI_CONNECTING;
    default:
      return Text::WIFI_FAILED;
  }
}

void refreshSettings() {
  const SettingsData& s = Settings::data();
  const Status& st = Controller::status();
  Nextion::setValue("h_dim.val", s.dim);
  Nextion::setValue("bt_led.val", s.led ? 1 : 0);
  Nextion::setValue("h_led.val", s.ledBrightness);
  Nextion::setText("t_ip", st.message == Text::WIFI_SETUP_RESTART ? String(st.message) : ipText(), 40);
  Nextion::setText("t_ver", String("FW ") + FW_VERSION + "  Battery " + String(Power::voltage(), 2) + " V", 40);
}

void refresh() {
  uint8_t p = Nextion::currentPage();
  if (p > Files) return;
  common();
  switch (p) {
    case Home:
      refreshHome();
      break;
    case Player:
      refreshPlayer();
      break;
    case List:
      refreshList();
      break;
    case SettingsPage:
      refreshSettings();
      break;
    case Files:
      refreshFiles();
      break;
  }
}

void scrollList(int delta) {
  int count = Stations::count();
  int next = listOffset + delta;
  if (next < 0 || next >= count) return;
  listOffset = next;
}

void scrollFiles(int delta) {
  int next = filesOffset + delta;
  if (next < 0 || next >= static_cast<int>(entries.size())) return;
  filesOffset = next;
}

void openEntry(uint8_t row) {
  int index = filesOffset + row;
  if (index >= static_cast<int>(entries.size())) return;
  const SdEntry& e = entries[index];
  String path = SdCard::join(browseDir, e.name);
  if (e.dir) {
    browseTo(path);
    return;
  }
  Controller::playFile(path);
  show(Player);
}

void onPageOpened(uint8_t p) {
  if (p == List) listOffset = Settings::data().station / ROWS * ROWS;
  if (p == Files) loadFiles();
}

void onTouch(uint8_t p, uint8_t id) {
  Api::broadcastTouch(p, id);
  switch (p) {
    case Home:
      if (id == Id::HOME_RADIO) Controller::selectMode(Mode::Radio);
      if (id == Id::HOME_SD) Controller::selectMode(Mode::Sd);
      if (id == Id::HOME_BT) Controller::selectMode(Mode::Bluetooth);
      break;
    case Player:
      if (id == Id::PLAYER_MUTE) Controller::toggleMute();
      if (id == Id::PLAYER_PREV) Controller::previous();
      if (id == Id::PLAYER_NEXT) Controller::next();
      if (id == Id::PLAYER_PLAY) {
        Nextion::clearCache();
        Controller::togglePlay();
      }
      if (id == Id::PLAYER_VOLUME) Nextion::request("h_vol.val", TagVolume);
      if (id == Id::PLAYER_LIST) {
        Mode m = Controller::status().mode;
        if (m == Mode::Radio) show(List);
        if (m == Mode::Sd) show(Files);
      }
      break;
    case List:
      if (id >= Id::LIST_ROW0 && id < Id::LIST_ROW0 + ROWS) {
        int index = listOffset + id - Id::LIST_ROW0;
        if (index < Stations::count()) {
          Controller::playStation(index);
          show(Player);
        }
      }
      if (id == Id::LIST_UP) scrollList(-ROWS);
      if (id == Id::LIST_DOWN) scrollList(ROWS);
      break;
    case SettingsPage:
      if (id == Id::SET_DIM) Nextion::request("h_dim.val", TagDim);
      if (id == Id::SET_LED) Nextion::request("bt_led.val", TagLed);
      if (id == Id::SET_LED_LEVEL) Nextion::request("h_led.val", TagLedLevel);
      break;
    case Files:
      if (id == Id::FILES_PARENT && browseDir != "/") browseTo(SdCard::parent(browseDir));
      if (id >= Id::FILES_ROW0 && id < Id::FILES_ROW0 + 2 * ROWS && (id - Id::FILES_ROW0) % 2 == 0) {
        openEntry((id - Id::FILES_ROW0) / 2);
      }
      if (id == Id::FILES_UP) scrollFiles(-ROWS);
      if (id == Id::FILES_DOWN) scrollFiles(ROWS);
      break;
  }
}

void onNumber(uint8_t tag, int32_t value) {
  Serial.printf("[ui] value tag %u = %d\n", tag, value);
  switch (tag) {
    case TagVolume:
      Controller::setVolume(value);
      break;
    case TagDim:
      Controller::setDim(value);
      break;
    case TagLed:
      Controller::setLed(value != 0);
      break;
    case TagLedLevel:
      Controller::setLedBrightness(constrain(value, 0, 255));
      break;
  }
}

}

void begin() {
  Nextion::begin();
  Nextion::command("sleep=0");
  delay(50);
}

void loop() {
  Nextion::Event e;
  while (Nextion::poll(e)) {
    if (e.type == Nextion::EventType::Touch) onTouch(e.page, e.id);
    if (e.type == Nextion::EventType::Page) onPageOpened(e.page);
    if (e.type == Nextion::EventType::Number) onNumber(e.tag, e.value);
    refreshedAt = 0;
  }
  if (millis() - refreshedAt < Cfg::UI_REFRESH_MS) return;
  refreshedAt = millis();
  refresh();
}

void show(Page p) {
  Serial.printf("[ui] show page %s\n", PAGE_NAMES[p]);
  Nextion::showPage(p, PAGE_NAMES[p]);
  onPageOpened(p);
  refreshedAt = 0;
}

uint8_t page() {
  return Nextion::currentPage();
}

void setBrightness(uint8_t dim) {
  Nextion::command(String("dim=") + dim);
}

void browseTo(const String& dir) {
  browseDir = SdCard::normalize(dir);
  filesOffset = 0;
  filesLoaded = false;
}

void powerOff() {
  Nextion::command("sleep=1");
}

}
