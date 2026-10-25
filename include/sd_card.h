#pragma once

#include <Arduino.h>

#include <vector>

struct SdEntry {
  String name;
  bool dir;
  uint32_t size;
};

namespace SdCard {

bool begin();

bool ready();

bool list(const String& dir, std::vector<SdEntry>& out, bool audioOnly);

bool isAudio(const String& name);

bool exists(const String& path);

bool remove(const String& path);

bool makeDir(const String& path);

String parent(const String& path);

String join(const String& dir, const String& name);

String fileName(const String& path);

String normalize(const String& path);

uint64_t totalBytes();

uint64_t usedBytes();

}
