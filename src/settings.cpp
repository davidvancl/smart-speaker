#include "settings.h"

#include <Preferences.h>

namespace Settings {

namespace {

constexpr const char* NAMESPACE = "soundbar";
constexpr const char* MODE_NAMES[] = {"bluetooth", "radio", "sd"};
constexpr const char* EFFECT_NAMES[] = {"solid", "breathe", "rainbow", "vu", "wave"};
constexpr const char* TOUCH_NAMES[] = {"leds", "bass", "play", "none"};

SettingsData current;
uint32_t rev = 1;
bool dirty = false;
uint32_t changedAt = 0;

template <typename T, size_t N>
bool parseName(const char* text, const char* const (&names)[N], T& out) {
  if (!text) return false;
  for (size_t i = 0; i < N; i++) {
    if (strcasecmp(text, names[i]) == 0) {
      out = static_cast<T>(i);
      return true;
    }
  }
  return false;
}

void sanitize(SettingsData& s) {
  if (static_cast<uint8_t>(s.mode) > static_cast<uint8_t>(Mode::Sd)) s.mode = Mode::Bluetooth;
  if (s.volume > Cfg::VOLUME_MAX) s.volume = Cfg::VOLUME_DEFAULT;
  if (s.bassLevel > Cfg::DSP_BASS_LEVEL_MAX) s.bassLevel = Cfg::DSP_BASS_LEVEL_MAX;
  s.dim = constrain(s.dim, 10, 100);
  if (s.ledEffect >= LedEffect::Count) s.ledEffect = LedEffect::Solid;
  if (s.touchAction >= TouchAction::Count) s.touchAction = TouchAction::Leds;
  if (s.station < 0) s.station = 0;
  s.tempOffset = constrain(s.tempOffset, -10.0f, 10.0f);
}

}

void begin() {
  Preferences p;
  p.begin(NAMESPACE, true);
  SettingsData d;
  current.mode = static_cast<Mode>(p.getUChar("mode", static_cast<uint8_t>(d.mode)));
  current.volume = p.getUChar("volume", d.volume);
  current.bass = p.getBool("bass", d.bass);
  current.bassLevel = p.getUChar("bassLevel", d.bassLevel);
  current.loudness = p.getBool("loudness", d.loudness);
  current.dim = p.getUChar("dim", d.dim);
  current.autoDim = p.getBool("autoDim", d.autoDim);
  current.led = p.getBool("led", d.led);
  current.ledBrightness = p.getUChar("ledBright", d.ledBrightness);
  current.ledColor = p.getULong("ledColor", d.ledColor);
  current.ledEffect = static_cast<LedEffect>(p.getUChar("ledEffect", static_cast<uint8_t>(d.ledEffect)));
  current.touchAction = static_cast<TouchAction>(p.getUChar("touch", static_cast<uint8_t>(d.touchAction)));
  current.station = p.getShort("station", d.station);
  current.file = p.getString("file", d.file);
  current.filePos = p.getULong("filePos", d.filePos);
  current.tempOffset = p.getFloat("tempOffset", d.tempOffset);
  current.autoOff = p.getUShort("autoOff", d.autoOff);
  p.end();
  sanitize(current);
  Serial.printf("[settings] mode %s, volume %u, bass %s/%u, led %s/%u, station %d, file %s\n",
                modeName(current.mode), current.volume, current.bass ? "on" : "off", current.bassLevel,
                current.led ? "on" : "off", current.ledBrightness, current.station, current.file.c_str());
}

SettingsData& data() {
  return current;
}

void changed() {
  sanitize(current);
  rev++;
  dirty = true;
  changedAt = millis();
}

void saveNow() {
  dirty = false;
  Preferences p;
  p.begin(NAMESPACE, false);
  p.putUChar("mode", static_cast<uint8_t>(current.mode));
  p.putUChar("volume", current.volume);
  p.putBool("bass", current.bass);
  p.putUChar("bassLevel", current.bassLevel);
  p.putBool("loudness", current.loudness);
  p.putUChar("dim", current.dim);
  p.putBool("autoDim", current.autoDim);
  p.putBool("led", current.led);
  p.putUChar("ledBright", current.ledBrightness);
  p.putULong("ledColor", current.ledColor);
  p.putUChar("ledEffect", static_cast<uint8_t>(current.ledEffect));
  p.putUChar("touch", static_cast<uint8_t>(current.touchAction));
  p.putShort("station", current.station);
  p.putString("file", current.file);
  p.putULong("filePos", current.filePos);
  p.putFloat("tempOffset", current.tempOffset);
  p.putUShort("autoOff", current.autoOff);
  p.end();
  Serial.println("[settings] saved");
}

void loop() {
  if (dirty && millis() - changedAt > Cfg::SETTINGS_SAVE_DELAY_MS) saveNow();
}

uint32_t revision() {
  return rev;
}

bool takeFlag(const char* key) {
  Preferences p;
  p.begin(NAMESPACE, false);
  bool set = p.getBool(key, false);
  if (set) p.remove(key);
  p.end();
  return set;
}

void setFlag(const char* key) {
  Preferences p;
  p.begin(NAMESPACE, false);
  p.putBool(key, true);
  p.end();
}

const char* modeName(Mode mode) {
  return MODE_NAMES[static_cast<uint8_t>(mode)];
}

bool parseMode(const char* text, Mode& out) {
  return parseName(text, MODE_NAMES, out);
}

const char* effectName(LedEffect effect) {
  return EFFECT_NAMES[static_cast<uint8_t>(effect)];
}

bool parseEffect(const char* text, LedEffect& out) {
  return parseName(text, EFFECT_NAMES, out);
}

const char* touchName(TouchAction action) {
  return TOUCH_NAMES[static_cast<uint8_t>(action)];
}

bool parseTouch(const char* text, TouchAction& out) {
  return parseName(text, TOUCH_NAMES, out);
}

}
