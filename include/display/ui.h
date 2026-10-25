#pragma once

#include <Arduino.h>

namespace Ui {

enum Page : uint8_t { Home = 0, Player = 1, List = 2, SettingsPage = 3, Files = 4 };

void begin();

void loop();

void show(Page page);

uint8_t page();

void setBrightness(uint8_t dim);

void browseTo(const String& dir);

void powerOff();

}
