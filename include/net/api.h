#pragma once

#include <Arduino.h>

namespace Api {

void begin();

void loop();

void broadcastStatus(const String& json);

void broadcastTouch(uint8_t page, uint8_t id);

}
