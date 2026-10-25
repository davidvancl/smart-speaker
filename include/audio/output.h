#pragma once

#include "audio/dsp.h"

namespace AudioOut {

void begin();

Dsp& sink();

void setParams(const DspParams& params);

void enableDac(bool on);

float level();

}
