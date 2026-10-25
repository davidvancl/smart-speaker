#pragma once

#include <Arduino.h>

namespace Latin2 {

String fromUtf8(const String& text, size_t maxLen);

}
