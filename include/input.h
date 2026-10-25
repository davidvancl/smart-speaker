#pragma once

#include <Arduino.h>

namespace Input {

enum class Event : uint8_t {
  None,
  EncoderClick,
  EncoderLong,
  Button1,
  Button2,
  Button3,
  Touch,
};

void begin();

void loop();

int takeEncoderSteps();

Event poll();

bool encoderHeld();

uint32_t lastActivity();

void markActivity();

}
