#include "stations.h"

#include <LittleFS.h>

#include <vector>

namespace Stations {

namespace {

constexpr const char* PATH = "/stations.json";
constexpr size_t MAX_STATIONS = 64;

const Station DEFAULTS[] = {
    {"ČRo Radiožurnál", "http://icecast7.play.cz/cro1-128.mp3"},
    {"ČRo Dvojka", "http://icecast5.play.cz/cro2-128.mp3"},
    {"ČRo Vltava", "http://icecast6.play.cz/cro3-128.mp3"},
    {"Evropa 2", "http://ice.actve.net/fm-evropa2-128"},
    {"Frekvence 1", "http://ice.actve.net/fm-frekvence1-128"},
    {"Kiss", "http://icecast3.play.cz/kiss128.mp3"},
    {"Hit Vysočina", "http://ice5.abradio.cz/hitvysocina128.mp3"},
};

std::vector<Station> list;
SemaphoreHandle_t mutex = nullptr;

struct Guard {
  Guard() {
    if (mutex) xSemaphoreTake(mutex, portMAX_DELAY);
  }
  ~Guard() {
    if (mutex) xSemaphoreGive(mutex);
  }
};

bool valid(const Station& s) {
  return s.name.length() > 0 && (s.url.startsWith("http://") || s.url.startsWith("https://"));
}

void save() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const Station& s : list) {
    JsonObject o = arr.add<JsonObject>();
    o["name"] = s.name;
    o["url"] = s.url;
  }
  File f = LittleFS.open(PATH, "w");
  if (!f) {
    Serial.println("[stations] cannot write file");
    return;
  }
  serializeJson(doc, f);
  f.close();
  Serial.printf("[stations] saved %u stations\n", (unsigned)list.size());
}

bool load() {
  File f = LittleFS.open(PATH, "r");
  if (!f) return false;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err || !doc.is<JsonArray>()) {
    Serial.printf("[stations] invalid file: %s\n", err.c_str());
    return false;
  }
  list.clear();
  for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
    Station s{o["name"] | "", o["url"] | ""};
    if (valid(s) && list.size() < MAX_STATIONS) list.push_back(s);
  }
  return true;
}

}

void begin() {
  mutex = xSemaphoreCreateMutex();
  if (!LittleFS.begin(true)) {
    Serial.println("[stations] LittleFS mount failed");
  }
  Guard g;
  if (!load()) {
    list.assign(std::begin(DEFAULTS), std::end(DEFAULTS));
    save();
  }
  Serial.printf("[stations] %u stations\n", (unsigned)list.size());
}

int count() {
  Guard g;
  return list.size();
}

bool get(int index, Station& out) {
  Guard g;
  if (index < 0 || index >= static_cast<int>(list.size())) return false;
  out = list[index];
  return true;
}

bool add(const Station& station) {
  if (!valid(station)) return false;
  Guard g;
  if (list.size() >= MAX_STATIONS) return false;
  list.push_back(station);
  save();
  return true;
}

bool update(int index, const Station& station) {
  if (!valid(station)) return false;
  Guard g;
  if (index < 0 || index >= static_cast<int>(list.size())) return false;
  list[index] = station;
  save();
  return true;
}

bool remove(int index) {
  Guard g;
  if (index < 0 || index >= static_cast<int>(list.size())) return false;
  list.erase(list.begin() + index);
  save();
  return true;
}

bool move(int from, int to) {
  Guard g;
  int n = list.size();
  if (from < 0 || from >= n || to < 0 || to >= n) return false;
  Station s = list[from];
  list.erase(list.begin() + from);
  list.insert(list.begin() + to, s);
  save();
  return true;
}

bool replace(JsonArrayConst items) {
  std::vector<Station> next;
  for (JsonObjectConst o : items) {
    Station s{o["name"] | "", o["url"] | ""};
    if (!valid(s)) return false;
    next.push_back(s);
  }
  if (next.size() > MAX_STATIONS) return false;
  Guard g;
  list.swap(next);
  save();
  return true;
}

void toJson(JsonArray out) {
  Guard g;
  for (const Station& s : list) {
    JsonObject o = out.add<JsonObject>();
    o["name"] = s.name;
    o["url"] = s.url;
  }
}

}
