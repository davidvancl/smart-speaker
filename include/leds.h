#pragma once

#include <Arduino.h>

#include "settings.h"

namespace Leds {

void begin();

void loop();

void configure(bool on, uint8_t brightness, uint32_t color, LedEffect effect);

void setAmbient(float factor);

void setLowBattery(bool low);

void setBusy(bool on);

void off();

}
