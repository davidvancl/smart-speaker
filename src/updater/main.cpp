#include <Arduino.h>
#include <OtaUpdater.h>
#include <esp_ota_ops.h>

#include "config.h"

namespace {

constexpr uint32_t RETRY_MS = 60000;

HardwareSerial display(2);

void nextion(const String& cmd) {
  display.print(cmd);
  display.write(0xFF);
  display.write(0xFF);
  display.write(0xFF);
}

void show(const char* text) {
  Serial.printf("[updater] %s\n", text);
  nextion(String("t_mode.txt=\"Firmware update\""));
  nextion(String("g_now.txt=\"") + text + "\"");
}

const esp_partition_t* application() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
}

bool applicationValid() {
  const esp_partition_t* app = application();
  esp_app_desc_t desc;
  if (!app || esp_ota_get_partition_description(app, &desc) != ESP_OK) return false;
  Serial.printf("[updater] installed application %s %s\n", desc.project_name, desc.version);
  return true;
}

void backToApplication() {
  if (!applicationValid()) {
    show("No valid firmware, retrying in 1 min");
    delay(RETRY_MS);
    ESP.restart();
  }
  esp_ota_set_boot_partition(application());
  delay(1500);
  ESP.restart();
}

}

void setup() {
  pinMode(Pins::DAC_XSMT, OUTPUT);
  digitalWrite(Pins::DAC_XSMT, LOW);
  Serial.begin(115200);
  delay(200);
  display.begin(Cfg::NEXTION_BAUD, SERIAL_8N1, Pins::NX_RX, Pins::NX_TX);
  nextion("");
  nextion("sleep=0");
  nextion("page home");
  show("Updating firmware, do not switch off...");

  if (!OtaUpdater::connectWifi()) {
    show("Update failed: no WiFi");
    backToApplication();
  }
  show("Downloading firmware...");
  OtaUpdater::checkAndUpdate();
  show("Update failed");
  backToApplication();
}

void loop() {
}
