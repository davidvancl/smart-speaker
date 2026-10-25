#pragma once

#include <Arduino.h>

namespace StreamPlayer {

void begin();

void playUrl(const String& url);

void playFile(const String& path, uint32_t offset = 0);

void pause();

void resume();

void stop();

uint32_t filePosition();

}
