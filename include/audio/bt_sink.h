#pragma once

#include <Arduino.h>

namespace BtSink {

void begin(uint8_t volume);

void play();

void pause();

void next();

void previous();

void setVolume(uint8_t volume);

bool connected();

String deviceName();

void end();

}
