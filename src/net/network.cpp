#include "net/network.h"

#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <esp_ota_ops.h>

#include "clock.h"
#include "command.h"
#include "config.h"

namespace Network {

namespace {

QueueHandle_t otaRequests = nullptr;
volatile State current = State::Off;
bool allowPortal = false;
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

void checkUpdate() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(8000);
  String url = String("https://github.com/") + OTA_REPO + "/releases/latest/download/version.txt";
  if (!http.begin(client, url)) return;
  int code = http.GET();
  String remote = code == HTTP_CODE_OK ? http.getString() : String();
  http.end();
  remote.trim();
  Serial.printf("[ota] installed %s, latest %s (HTTP %d)\n", FW_VERSION, remote.length() ? remote.c_str() : "?", code);
  if (!isNewer(remote, FW_VERSION)) return;
  const esp_partition_t* factory =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
  if (!factory || esp_ota_set_boot_partition(factory) != ESP_OK) {
    Serial.println("[ota] updater partition not found - flash it once over USB (pio run -e updater -t upload)");
    return;
  }
  Controller::post(Cmd::Restart, 0, 0, (String("Updating to ") + remote + "...").c_str());
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

void onPortal(WiFiManager*) {
  current = State::Portal;
  Serial.printf("[net] setup portal: join WiFi \"%s\", open http://%s\n", Cfg::SETUP_AP,
                WiFi.softAPIP().toString().c_str());
}

void connected() {
  WiFi.setAutoReconnect(true);
  current = State::Connected;
  Serial.printf("[net] connected to %s, IP %s, RSSI %d dBm, free heap %u\n", WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str(), WiFi.RSSI(), (unsigned)ESP.getFreeHeap());
  announce();
  checkUpdate();
}

void connect(bool forcePortal) {
  current = State::Connecting;
  lastAttempt = millis();
  WiFiManager wm;
  wm.setHostname(Cfg::HOSTNAME);
  wm.setTitle(Cfg::DEVICE_NAME);
  wm.setConnectTimeout(Cfg::WIFI_CONNECT_TIMEOUT_MS / 1000);
  wm.setAPCallback(onPortal);
  wm.setEnableConfigPortal(allowPortal || forcePortal);
  bool ok = forcePortal ? wm.startConfigPortal(Cfg::SETUP_AP) : wm.autoConnect(Cfg::SETUP_AP);
  if (ok) {
    connected();
  } else {
    Serial.println("[net] connection failed");
    current = State::Failed;
  }
}

void task(void* arg) {
  connect(arg != nullptr);
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
      lastAttempt = millis();
      WiFi.begin();
      current = State::Failed;
    }
  }
}

}

void begin(bool portalIfNeeded, bool forcePortal) {
  allowPortal = portalIfNeeded;
  otaRequests = xQueueCreate(1, sizeof(bool));
  current = State::Connecting;
  xTaskCreatePinnedToCore(task, "net", 10240, forcePortal ? reinterpret_cast<void*>(1) : nullptr, 1, nullptr, 0);
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
    case State::Portal:
      return "portal";
    case State::Failed:
      return "failed";
    default:
      return "off";
  }
}

String ip() {
  if (current == State::Portal) return WiFi.softAPIP().toString();
  if (current == State::Connected) return WiFi.localIP().toString();
  return String();
}

int rssi() {
  return current == State::Connected ? WiFi.RSSI() : 0;
}

String ssid() {
  return current == State::Connected ? WiFi.SSID() : String();
}

bool portalActive() {
  return current == State::Portal;
}

void requestOta() {
  bool request = true;
  if (otaRequests) xQueueSend(otaRequests, &request, 0);
}

}
