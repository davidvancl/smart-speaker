#include "sensors.h"

#include <Adafruit_BME280.h>
#include <BH1750.h>
#include <Wire.h>

#include "config.h"

namespace Sensors {

namespace {

Adafruit_BME280 bme;
BH1750 lightMeter;
bool climate = false;
bool bmeFound = false;
bool light = false;
float temperatureValue = NAN, humidityValue = NAN, pressureHpa = NAN;
float luxValue = NAN;
uint32_t climateAt = 0;
uint32_t lightAt = 0;

void scan() {
  Serial.print("[i2c] devices:");
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02X", a);
      found++;
    }
  }
  Serial.println(found ? "" : " none - check SDA/SCL/3V3");
}

void readClimate() {
  if (!bme.takeForcedMeasurement()) {
    Serial.println("[sensors] BME280 read failed");
    return;
  }
  temperatureValue = bme.readTemperature();
  humidityValue = bme.readHumidity();
  pressureHpa = bme.readPressure() / 100.0f;
  climate = true;
  Serial.printf("[sensors] %.1f C, %.0f %%, %.0f hPa\n", temperatureValue, humidityValue, pressureHpa);
}

}

void begin() {
  Wire.begin(Pins::I2C_SDA, Pins::I2C_SCL);
  scan();
  bmeFound = bme.begin(0x76, &Wire) || bme.begin(0x77, &Wire);
  if (bmeFound) {
    bme.setSampling(Adafruit_BME280::MODE_FORCED, Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::FILTER_OFF);
    Serial.println("[sensors] BME280 found");
    readClimate();
  } else {
    Serial.println("[sensors] BME280 not found");
  }
  light = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire);
  Serial.println(light ? "[sensors] BH1750 at 0x23" : "[sensors] BH1750 not found");
}

void loop() {
  uint32_t now = millis();
  if (bmeFound && now - climateAt > Cfg::CLIMATE_INTERVAL_MS) {
    climateAt = now;
    readClimate();
  }
  if (light && now - lightAt > Cfg::LIGHT_INTERVAL_MS && lightMeter.measurementReady()) {
    lightAt = now;
    float lux = lightMeter.readLightLevel();
    if (lux >= 0) luxValue = lux;
  }
}

void setTempOffset(float offset) {
  if (bmeFound) bme.setTemperatureCompensation(offset);
}

bool climateOk() {
  return climate;
}

float temperature() {
  return temperatureValue;
}

float humidity() {
  return humidityValue;
}

float pressure() {
  return pressureHpa;
}

bool lightOk() {
  return light && !isnan(luxValue);
}

float lux() {
  return luxValue;
}

}
