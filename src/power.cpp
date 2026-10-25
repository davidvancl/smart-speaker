#include "power.h"

#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "config.h"

namespace Power {

namespace {

struct Point {
  float volts;
  uint8_t percent;
};

const Point CURVE[] = {
    {4.20f, 100}, {4.10f, 90}, {4.00f, 80}, {3.92f, 70}, {3.85f, 60}, {3.79f, 50},
    {3.75f, 40},  {3.71f, 30}, {3.67f, 20}, {3.61f, 10}, {3.50f, 5},  {3.30f, 0},
};

float samples[Cfg::BATTERY_AVERAGE];
uint8_t sampleCount = 0;
uint8_t sampleIndex = 0;
float average = 0;
uint8_t criticalCount = 0;
uint32_t sampledAt = 0;

float measure() {
  uint32_t mv = 0;
  for (int i = 0; i < 16; i++) mv += analogReadMilliVolts(Pins::BATTERY);
  return mv / 16.0f * Cfg::BATTERY_DIVIDER / 1000.0f;
}

void sample() {
  float v = measure();
  if (sampleCount == 0) {
    for (float& s : samples) s = v;
    sampleCount = Cfg::BATTERY_AVERAGE;
  }
  samples[sampleIndex] = v;
  sampleIndex = (sampleIndex + 1) % Cfg::BATTERY_AVERAGE;
  float sum = 0;
  for (float s : samples) sum += s;
  average = sum / Cfg::BATTERY_AVERAGE;
  if (present() && average < Cfg::BATTERY_CRITICAL_V) {
    if (criticalCount < 255) criticalCount++;
  } else {
    criticalCount = 0;
  }
}

}

void begin() {
  analogSetPinAttenuation(Pins::BATTERY, ADC_11db);
  sample();
  if (criticalCount) criticalCount = Cfg::BATTERY_CRITICAL_COUNT;
  sampledAt = millis();
  Serial.printf("[power] battery %.2f V ~%d %%, wake: %s\n", average, percent(), wakeReason());
}

void loop() {
  if (millis() - sampledAt < Cfg::BATTERY_SAMPLE_MS) return;
  sampledAt = millis();
  sample();
  static uint32_t loggedAt = 0;
  if (millis() - loggedAt > 60000) {
    loggedAt = millis();
    Serial.printf("[power] battery %.2f V ~%d %%, free heap %u\n", average, percent(), (unsigned)ESP.getFreeHeap());
  }
}

bool present() {
  return average >= Cfg::BATTERY_ABSENT_V;
}

float voltage() {
  return average;
}

int percent() {
  if (!present()) return 0;
  if (average >= CURVE[0].volts) return 100;
  for (size_t i = 1; i < sizeof(CURVE) / sizeof(CURVE[0]); i++) {
    if (average >= CURVE[i].volts) {
      const Point& hi = CURVE[i - 1];
      const Point& lo = CURVE[i];
      float t = (average - lo.volts) / (hi.volts - lo.volts);
      return static_cast<int>(lo.percent + t * (hi.percent - lo.percent) + 0.5f);
    }
  }
  return 0;
}

bool low() {
  return present() && percent() < Cfg::BATTERY_LOW_PERCENT;
}

bool critical() {
  return criticalCount >= Cfg::BATTERY_CRITICAL_COUNT;
}

const char* wakeReason() {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_EXT0:
      return "encoder";
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      return "power on / reset";
    default:
      return "other";
  }
}

void deepSleep() {
  Serial.println("[power] deep sleep, wake with encoder press");
  Serial.flush();
  rtc_gpio_pullup_en(static_cast<gpio_num_t>(Pins::ENC_SW));
  rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(Pins::ENC_SW));
  esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(Pins::ENC_SW), 0);
  esp_deep_sleep_start();
}

}
