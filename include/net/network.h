#pragma once

#include <Arduino.h>

namespace Network {

enum class State : uint8_t { Off, Connecting, Connected, Failed };

void begin();

State state();

const char* stateName();

String ip();

int rssi();

String ssid();

bool busy();

void requestOta();

}
