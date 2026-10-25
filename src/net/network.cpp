#include "net/network.h"

#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <OtaUpdater.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>

#include "clock.h"
#include "command.h"
#include "config.h"
#include "WifiCredentials.h"

namespace Network {

namespace {

QueueHandle_t otaRequests = nullptr;
volatile State current = State::Off;
volatile bool checking = false;
bool announced = false;
uint32_t lastAttempt = 0;

bool parseVersion(const String& text, int out[3]) {
  out[0] = out[1] = out[2] = 0;
  return sscanf(text.c_str(), "%d.%d.%d", &out[0], &out[1], &out[2]) >= 1;
}

bool isNewer(const String& remote, const char* local) {
  int r[3], l[3];
  if (!parseVersion(remote, r) || !parseVersion(local, l)) return false;
  for (int i = 0; i < 3; i++) {
    if (r[i] != l[i]) return r[i] > l[i];
  }
  return false;
}

bool startUpdate() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(8000);
  String url = String("https://github.com/") + OTA_REPO + "/releases/latest/download/version.txt";
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  String remote = code == HTTP_CODE_OK ? http.getString() : String();
  http.end();
  remote.trim();
  Serial.printf("[ota] installed %s, latest %s (HTTP %d)\n", FW_VERSION, remote.length() ? remote.c_str() : "?", code);
  if (!isNewer(remote, FW_VERSION)) return false;
  const esp_partition_t* factory =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
  if (!factory || esp_ota_set_boot_partition(factory) != ESP_OK) {
    Serial.println("[ota] updater partition not found - flash it once over USB (pio run -e updater -t upload)");
    return false;
  }
  Controller::post(Cmd::Restart, 0, 0, (String("Updating to ") + remote + "...").c_str());
  return true;
}

void checkUpdate() {
  checking = true;
  checking = startUpdate();
}

bool connectWifi() {
  return OtaUpdater::connectWifi(WIFI_CREDENTIALS, Cfg::WIFI_CONNECT_TIMEOUT_MS);
}

void announce() {
  if (announced) return;
  announced = true;
  if (MDNS.begin(Cfg::HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[net] mDNS http://%s.local\n", Cfg::HOSTNAME);
  }
  Clock::startNtp();
}

void connected() {
  WiFi.setAutoReconnect(true);
  current = State::Connected;
  Serial.printf("[net] connected to %s, IP %s, RSSI %d dBm, free heap %u\n", WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str(), WiFi.RSSI(), (unsigned)ESP.getFreeHeap());
  announce();
  checkUpdate();
}

void connect() {
  current = State::Connecting;
  lastAttempt = millis();
  if (connectWifi()) {
    connected();
  } else {
    Serial.println("[net] connection failed - set the network in include/secrets.h");
    current = State::Failed;
  }
}

void task(void*) {
  WiFi.setHostname(Cfg::HOSTNAME);
  connect();
  bool request;
  for (;;) {
    if (xQueueReceive(otaRequests, &request, pdMS_TO_TICKS(1000)) == pdTRUE) {
      if (current == State::Connected) checkUpdate();
      continue;
    }
    bool up = WiFi.status() == WL_CONNECTED;
    if (current == State::Connected && !up) {
      Serial.println("[net] WiFi lost, reconnecting");
      current = State::Connecting;
      lastAttempt = millis();
    } else if (current != State::Connected && up) {
      Serial.printf("[net] reconnected, IP %s\n", WiFi.localIP().toString().c_str());
      current = State::Connected;
      announce();
    } else if (!up && millis() - lastAttempt > Cfg::WIFI_RETRY_MS) {
      Serial.println("[net] retrying WiFi");
      connect();
    }
  }
}

}

void begin() {
  otaRequests = xQueueCreate(1, sizeof(bool));
  current = State::Connecting;
  xTaskCreatePinnedToCore(task, "net", 10240, nullptr, 1, nullptr, 0);
}

State state() {
  return current;
}

const char* stateName() {
  switch (current) {
    case State::Connecting:
      return "connecting";
    case State::Connected:
      return "connected";
    case State::Failed:
      return "failed";
    default:
      return "off";
  }
}

String ip() {
  return current == State::Connected ? WiFi.localIP().toString() : String();
}

int rssi() {
  return current == State::Connected ? WiFi.RSSI() : 0;
}

String ssid() {
  return current == State::Connected ? WiFi.SSID() : String();
}

bool busy() {
  return current == State::Connecting || checking;
}

void requestOta() {
  bool request = true;
  if (otaRequests) xQueueSend(otaRequests, &request, 0);
}

}
