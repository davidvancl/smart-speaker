#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include <vector>

#include "command.h"
#include "settings.h"

struct Status {
  Mode mode = Mode::Bluetooth;
  bool playing = false;
  bool muted = false;
  uint8_t volume = 0;
  StreamPhase phase = StreamPhase::Idle;
  bool btConnected = false;
  String btDevice;
  String title;
  String artist;
  String album;
  String stationName;
  String message;
  int bitrate = 0;
  int fileIndex = -1;
  int fileCount = 0;
};

namespace Controller {

void init();

void begin();

void loop();

const Status& status();

uint32_t revision();

String sourceLabel();

String modeLabel();

String infoLine();

String nowPlaying();

void togglePlay();

void next();

void previous();

void setVolume(int volume);

void changeVolume(int delta);

void toggleMute();

void selectMode(Mode mode);

void playStation(int index);

void playFile(const String& path);

void setLed(bool on);

void setLedBrightness(uint8_t brightness);

void setDim(uint8_t dim);

void toggleBass();


void powerOff(const char* reason);

bool applySettings(JsonObjectConst json);

String statusJson();

}
