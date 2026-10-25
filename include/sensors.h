#pragma once

#include <Arduino.h>

namespace Sensors {

void begin();

void loop();

void setTempOffset(float offset);

bool climateOk();

float temperature();

float humidity();

float pressure();

bool lightOk();

float lux();

}
