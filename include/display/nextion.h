#pragma once

#include <Arduino.h>

namespace Nextion {

enum class EventType : uint8_t { Touch, Page, Number };

struct Event {
  EventType type;
  uint8_t page;
  uint8_t id;
  uint8_t tag;
  int32_t value;
};

void begin();

void command(const String& cmd);

void setText(const char* obj, const String& utf8, size_t maxLen);

void setValue(const char* objAttr, int32_t value);

void request(const char* objAttr, uint8_t tag);

void showPage(uint8_t page, const char* name);

void pageOpened(uint8_t page);

uint8_t currentPage();

void clearCache();

bool poll(Event& event);

}
