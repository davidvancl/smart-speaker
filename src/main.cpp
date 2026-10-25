#include <Arduino.h>
#include <esp_bt.h>

#include "audio/bt_sink.h"
#include "audio/output.h"
#include "audio/stream_player.h"
#include "clock.h"
#include "controller.h"
#include "display/ui.h"
#include "input.h"
#include "leds.h"
#include "net/network.h"
#include "net/api.h"
#include "power.h"
#include "sd_card.h"
#include "sensors.h"
#include "settings.h"
#include "stations.h"

namespace {

bool wifiMode = false;

void startBluetooth() {
  AudioOut::begin();
  BtSink::begin(Settings::data().volume);
}

void startWifi() {
  esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
  Serial.printf("[main] BT memory released, free heap %u\n", (unsigned)ESP.getFreeHeap());
  Stations::begin();
  SdCard::begin();
  AudioOut::begin();
  StreamPlayer::begin();
  Network::begin();
  wifiMode = true;
}

}

void setup() {
  AudioOut::enableDac(false);
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n[main] ESP32 Soundbar %s\n", FW_VERSION);

  Settings::begin();
  Power::begin();
  if (Power::critical()) {
    Serial.println("[main] battery critical, back to sleep");
    Power::deepSleep();
  }

  Controller::init();
  Ui::begin();
  Input::begin();
  Sensors::begin();
  Clock::begin();
  Leds::begin();

  if (Settings::data().mode == Mode::Bluetooth) startBluetooth();
  else startWifi();

  Controller::begin();
  delay(100);
  AudioOut::enableDac(true);
  Serial.printf("[main] ready, free heap %u (min %u)\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
}

void loop() {
  Input::loop();
  Controller::loop();
  Ui::loop();
  Power::loop();
  Sensors::loop();
  Leds::loop();
  Settings::loop();
  if (wifiMode) Api::loop();
  delay(1);
}
