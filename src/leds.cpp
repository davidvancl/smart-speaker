#include "leds.h"

#include <NeoPixelBus.h>

#include "audio/output.h"
#include "config.h"

namespace Leds {

namespace {

NeoPixelBus<NeoGrbFeature, NeoEsp32I2s1Ws2812xMethod> strip(Cfg::LED_COUNT, Pins::LEDS);
NeoGamma<NeoGammaTableMethod> gammaTable;
RgbColor frame[Cfg::LED_COUNT];
bool enabled = false;
uint8_t brightness = 0;
uint32_t color = 0;
LedEffect effect = LedEffect::Solid;
float ambient = 1.0f;
bool lowBattery = false;
bool busy = false;
bool dark = true;
float vu = 0;
uint32_t frameAt = 0;

RgbColor unpack(uint32_t c) {
  return RgbColor((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

uint8_t ratio(float f) {
  return static_cast<uint8_t>(constrain(f, 0.0f, 1.0f) * 255);
}

void fill(const RgbColor& c) {
  for (RgbColor& p : frame) p = c;
}

void render(uint32_t now) {
  RgbColor base = unpack(color);
  switch (effect) {
    case LedEffect::Solid:
      fill(base);
      break;
    case LedEffect::Breathe: {
      float phase = (now % 4000) / 4000.0f;
      fill(base.Dim(ratio(0.15f + 0.85f * (0.5f - 0.5f * cosf(phase * 2 * PI)))));
      break;
    }
    case LedEffect::Rainbow:
      for (uint16_t i = 0; i < Cfg::LED_COUNT; i++) {
        float hue = fmodf(static_cast<float>(i) / Cfg::LED_COUNT + now / 8000.0f, 1.0f);
        frame[i] = RgbColor(HsbColor(hue, 1.0f, 1.0f));
      }
      break;
    case LedEffect::Wave:
      for (uint16_t i = 0; i < Cfg::LED_COUNT; i++) {
        float phase = i * 2 * PI / Cfg::LED_COUNT * 2 - now / 400.0f;
        frame[i] = base.Dim(ratio(0.1f + 0.9f * (0.5f + 0.5f * sinf(phase))));
      }
      break;
    case LedEffect::Vu: {
      float level = AudioOut::level();
      vu = level > vu ? level : vu * 0.90f;
      float half = Cfg::LED_COUNT / 2.0f;
      float lit = min(1.0f, vu * 1.6f) * half;
      RgbColor peak(255, 25, 0);
      for (uint16_t i = 0; i < Cfg::LED_COUNT; i++) {
        float d = fabsf(i + 0.5f - half);
        frame[i] = d > lit ? base.Dim(15) : RgbColor::LinearBlend(base, peak, d / half);
      }
      break;
    }
    default:
      fill(RgbColor(0));
      break;
  }
}

void show(uint8_t bright) {
  float level = bright / 255.0f * ambient;
  float current = 0;
  for (const RgbColor& p : frame) {
    current += (p.R + p.G + p.B) / 255.0f * level * Cfg::LED_MA_PER_CHANNEL + Cfg::LED_IDLE_MA;
  }
  float budget = lowBattery ? Cfg::LED_BUDGET_LOW_BATTERY_MA : Cfg::LED_BUDGET_MA;
  float idle = Cfg::LED_COUNT * Cfg::LED_IDLE_MA;
  if (current > budget && current > idle) level *= max(0.0f, budget - idle) / (current - idle);
  for (uint16_t i = 0; i < Cfg::LED_COUNT; i++) strip.SetPixelColor(i, gammaTable.Correct(frame[i]).Dim(ratio(level)));
  strip.Show();
}

void clear() {
  strip.ClearTo(RgbColor(0));
  strip.Show();
}

}

void begin() {
  strip.Begin();
  const RgbColor test[] = {RgbColor(50, 0, 0), RgbColor(0, 50, 0), RgbColor(0, 0, 50)};
  for (const RgbColor& c : test) {
    strip.ClearTo(c);
    strip.Show();
    delay(200);
  }
  clear();
  Serial.printf("[leds] %u LEDs on GPIO%d, test red -> green -> blue (wrong order = change NeoGrbFeature)\n",
                Cfg::LED_COUNT, Pins::LEDS);
}

void loop() {
  uint32_t now = millis();
  if (now - frameAt < Cfg::LED_FRAME_MS || !strip.CanShow()) return;
  frameAt = now;
  if (busy) {
    dark = false;
    fill((now / Cfg::LED_BUSY_BLINK_MS) % 2 ? RgbColor(0) : unpack(Cfg::LED_BUSY_COLOR));
    show(Cfg::LED_BUSY_BRIGHTNESS);
    return;
  }
  if (!enabled || brightness == 0) {
    if (!dark) {
      clear();
      dark = true;
    }
    if (effect == LedEffect::Vu) AudioOut::level();
    return;
  }
  dark = false;
  render(now);
  show(brightness);
}

void configure(bool on, uint8_t level, uint32_t rgb, LedEffect fx) {
  if (on != enabled || level != brightness || rgb != color || fx != effect) {
    Serial.printf("[leds] %s, brightness %u, color #%06X, effect %s\n", on ? "on" : "off", level, (unsigned)rgb,
                  Settings::effectName(fx));
  }
  enabled = on;
  brightness = level;
  color = rgb;
  effect = fx;
}

void setAmbient(float factor) {
  ambient = constrain(factor, 0.0f, 1.0f);
}

void setLowBattery(bool low) {
  lowBattery = low;
}

void setBusy(bool on) {
  busy = on;
}

void off() {
  busy = false;
  enabled = false;
  while (!strip.CanShow()) delay(1);
  clear();
  dark = true;
}

}
