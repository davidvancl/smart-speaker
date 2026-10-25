#pragma once

#include <Arduino.h>

namespace Clock {

void begin();

void startNtp();

bool valid();

String time();

String date();

}
