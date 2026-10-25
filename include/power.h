#pragma once

#include <Arduino.h>

namespace Power {

void begin();

void loop();

bool present();

float voltage();

int percent();

bool low();

bool critical();

const char* wakeReason();

[[noreturn]] void deepSleep();

}
