#pragma once

#include <Arduino.h>

#include "config.h"

enum class Mode : uint8_t { Bluetooth, Radio, Sd };
enum class LedEffect : uint8_t { Solid, Breathe, Rainbow, Vu, Wave, Count };
enum class TouchAction : uint8_t { Leds, Bass, PlayPause, None, Count };

struct SettingsData {
  Mode mode = Mode::Bluetooth;
  uint8_t volume = Cfg::VOLUME_DEFAULT;
  bool bass = true;
  uint8_t bassLevel = 6;
  bool loudness = true;
  uint8_t dim = 80;
  bool autoDim = true;
  bool led = true;
  uint8_t ledBrightness = 96;
  uint32_t ledColor = 0xFF0000;
  LedEffect ledEffect = LedEffect::Solid;
  TouchAction touchAction = TouchAction::Leds;
  int16_t station = 0;
  String file;
  uint32_t filePos = 0;
  float tempOffset = -2.0f;
  uint16_t autoOff = 30;
};

namespace Settings {

void begin();

SettingsData& data();

void changed();

void saveNow();

void loop();

uint32_t revision();

const char* modeName(Mode mode);
bool parseMode(const char* text, Mode& out);

const char* effectName(LedEffect effect);
bool parseEffect(const char* text, LedEffect& out);

const char* touchName(TouchAction action);
bool parseTouch(const char* text, TouchAction& out);

}
