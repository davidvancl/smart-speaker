#include "display/nextion.h"

#include <vector>

#include "config.h"
#include "display/latin2.h"

namespace Nextion {

namespace {

struct Cached {
  String key;
  String value;
};

struct Pending {
  uint8_t tag;
  uint32_t at;
};

HardwareSerial serial(2);
std::vector<Cached> cache;
std::vector<Pending> pending;
uint8_t page = 0xFF;
uint8_t frame[8];
uint8_t length = 0;

bool cached(const String& key, const String& value) {
  for (Cached& c : cache) {
    if (c.key == key) {
      if (c.value == value) return true;
      c.value = value;
      return false;
    }
  }
  cache.push_back({key, value});
  return false;
}

size_t frameLength(uint8_t header) {
  switch (header) {
    case 0x65:
      return 7;
    case 0x66:
      return 5;
    case 0x71:
      return 8;
    default:
      return 0;
  }
}

void expirePending() {
  uint32_t now = millis();
  while (!pending.empty() && now - pending.front().at > Cfg::NEXTION_GET_TIMEOUT_MS) {
    Serial.printf("[nx] get tag %u timed out\n", pending.front().tag);
    pending.erase(pending.begin());
  }
}

}

void begin() {
  serial.begin(Cfg::NEXTION_BAUD, SERIAL_8N1, Pins::NX_RX, Pins::NX_TX);
  command("");
  command("bkcmd=0");
}

void command(const String& cmd) {
  serial.print(cmd);
  serial.write(0xFF);
  serial.write(0xFF);
  serial.write(0xFF);
}

void setText(const char* obj, const String& utf8, size_t maxLen) {
  String value = Latin2::fromUtf8(utf8, maxLen);
  String key = String(obj) + ".txt";
  if (cached(key, value)) return;
  command(key + "=\"" + value + "\"");
}

void setValue(const char* objAttr, int32_t value) {
  String text(value);
  if (cached(objAttr, text)) return;
  command(String(objAttr) + "=" + text);
}

void request(const char* objAttr, uint8_t tag) {
  pending.push_back({tag, millis()});
  command(String("get ") + objAttr);
}

void showPage(uint8_t id, const char* name) {
  command(String("page ") + name);
  pageOpened(id);
}

void pageOpened(uint8_t id) {
  if (id != page) clearCache();
  page = id;
}

uint8_t currentPage() {
  return page;
}

void clearCache() {
  cache.clear();
}

bool poll(Event& event) {
  expirePending();
  while (serial.available()) {
    uint8_t b = serial.read();
    if (length == 0 && frameLength(b) == 0) continue;
    frame[length++] = b;
    size_t need = frameLength(frame[0]);
    if (length < need) continue;
    length = 0;
    if (frame[need - 1] != 0xFF || frame[need - 2] != 0xFF || frame[need - 3] != 0xFF) continue;

    switch (frame[0]) {
      case 0x65:
        event = {EventType::Touch, frame[1], frame[2], 0, frame[3]};
        Serial.printf("[nx] touch page %u id %u\n", frame[1], frame[2]);
        return true;
      case 0x66:
        event = {EventType::Page, frame[1], 0, 0, 0};
        Serial.printf("[nx] page %u\n", frame[1]);
        pageOpened(frame[1]);
        return true;
      case 0x71: {
        int32_t value = static_cast<int32_t>(frame[1] | (frame[2] << 8) | (frame[3] << 16) | (uint32_t(frame[4]) << 24));
        if (pending.empty()) continue;
        uint8_t tag = pending.front().tag;
        pending.erase(pending.begin());
        event = {EventType::Number, page, 0, tag, value};
        return true;
      }
    }
  }
  return false;
}

}
