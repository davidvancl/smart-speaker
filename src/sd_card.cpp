#include "sd_card.h"

#include <SD.h>
#include <SPI.h>

#include <algorithm>

#include "config.h"

namespace SdCard {

namespace {

bool mounted = false;

bool removeRecursive(const String& path) {
  File f = SD.open(path);
  if (!f) return false;
  if (!f.isDirectory()) {
    f.close();
    return SD.remove(path);
  }
  std::vector<String> children;
  for (File c = f.openNextFile(); c; c = f.openNextFile()) {
    children.push_back(join(path, fileName(c.name())));
    c.close();
  }
  f.close();
  for (const String& child : children) removeRecursive(child);
  return SD.rmdir(path);
}

}

bool begin() {
  SPI.begin(Pins::SD_SCK, Pins::SD_MISO, Pins::SD_MOSI, Pins::SD_CS);
  mounted = SD.begin(Pins::SD_CS, SPI, Cfg::SD_SPI_HZ);
  if (!mounted) {
    Serial.println("[sd] card not found (check FAT32, wiring; hardware fix: module EN -> 3V3)");
    return false;
  }
  Serial.printf("[sd] card type %d, %llu MB, used %llu MB\n", SD.cardType(), SD.cardSize() >> 20, SD.usedBytes() >> 20);
  std::vector<SdEntry> root;
  list("/", root, false);
  Serial.printf("[sd] / has %u entries:\n", (unsigned)root.size());
  for (size_t i = 0; i < root.size() && i < 20; i++) {
    Serial.printf("[sd]   %s%s\n", root[i].name.c_str(), root[i].dir ? "/" : "");
  }
  return true;
}

bool ready() {
  return mounted;
}

bool isAudio(const String& name) {
  String n = name;
  n.toLowerCase();
  return n.endsWith(".mp3") || n.endsWith(".aac") || n.endsWith(".wav");
}

bool list(const String& dir, std::vector<SdEntry>& out, bool audioOnly) {
  out.clear();
  if (!mounted) return false;
  File d = SD.open(normalize(dir));
  if (!d || !d.isDirectory()) return false;
  for (File f = d.openNextFile(); f && out.size() < Cfg::SD_MAX_ENTRIES; f = d.openNextFile()) {
    String name = fileName(f.name());
    bool isDir = f.isDirectory();
    uint32_t size = isDir ? 0 : f.size();
    f.close();
    if (name.startsWith(".") || name == "System Volume Information") continue;
    if (audioOnly && !isDir && !isAudio(name)) continue;
    out.push_back({name, isDir, size});
  }
  d.close();
  std::sort(out.begin(), out.end(), [](const SdEntry& a, const SdEntry& b) {
    if (a.dir != b.dir) return a.dir;
    return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
  });
  return true;
}

bool exists(const String& path) {
  return mounted && SD.exists(normalize(path));
}

bool remove(const String& path) {
  String p = normalize(path);
  if (!mounted || p == "/") return false;
  return removeRecursive(p);
}

bool makeDir(const String& path) {
  return mounted && SD.mkdir(normalize(path));
}

String normalize(const String& path) {
  String p = path;
  p.trim();
  p.replace("\\", "/");
  while (p.indexOf("//") >= 0) p.replace("//", "/");
  if (!p.startsWith("/")) p = "/" + p;
  if (p.length() > 1 && p.endsWith("/")) p.remove(p.length() - 1);
  if (p.indexOf("/..") >= 0) return "/";
  return p;
}

String parent(const String& path) {
  String p = normalize(path);
  int slash = p.lastIndexOf('/');
  return slash <= 0 ? String("/") : p.substring(0, slash);
}

String join(const String& dir, const String& name) {
  String d = normalize(dir);
  return d == "/" ? "/" + name : d + "/" + name;
}

String fileName(const String& path) {
  int slash = path.lastIndexOf('/');
  return slash < 0 ? path : path.substring(slash + 1);
}

uint64_t totalBytes() {
  return mounted ? SD.totalBytes() : 0;
}

uint64_t usedBytes() {
  return mounted ? SD.usedBytes() : 0;
}

}
