#pragma once

#include <Arduino.h>

namespace Network {

enum class State : uint8_t { Off, Connecting, Connected, Portal, Failed };

void begin(bool portalIfNeeded, bool forcePortal);

State state();

const char* stateName();

String ip();

int rssi();

String ssid();

bool portalActive();

void requestOta();

}
