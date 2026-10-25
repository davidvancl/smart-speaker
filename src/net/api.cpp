#include "net/api.h"

#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <SD.h>

#include "command.h"
#include "config.h"
#include "controller.h"
#include "sd_card.h"
#include "settings.h"
#include "stations.h"

namespace Api {

namespace {

AsyncWebServer* server = nullptr;
AsyncWebSocket* socket = nullptr;
fs::File upload;
String uploadPath;
bool uploadFailed = false;
uint32_t cleanupAt = 0;

void sendJson(AsyncWebServerRequest* req, int code, const JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  req->send(code, "application/json", out);
}

void sendOk(AsyncWebServerRequest* req) {
  req->send(200, "application/json", "{\"ok\":true}");
}

void sendError(AsyncWebServerRequest* req, int code, const char* message) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = message;
  sendJson(req, code, doc);
}

String param(AsyncWebServerRequest* req, const char* name, const char* fallback = "") {
  const AsyncWebParameter* p = req->getParam(name);
  if (!p) p = req->getParam(name, true);
  return p ? p->value() : String(fallback);
}

bool execute(JsonObjectConst in) {
  const char* cmd = in["cmd"] | "";
  JsonVariantConst value = in["value"];
  Serial.printf("[api] cmd %s\n", cmd);
  if (!strcmp(cmd, "toggle")) return Controller::post(Cmd::TogglePlay);
  if (!strcmp(cmd, "play")) return Controller::post(Cmd::Play);
  if (!strcmp(cmd, "pause")) return Controller::post(Cmd::Pause);
  if (!strcmp(cmd, "next")) return Controller::post(Cmd::Next);
  if (!strcmp(cmd, "prev")) return Controller::post(Cmd::Previous);
  if (!strcmp(cmd, "mute")) return Controller::post(Cmd::ToggleMute);
  if (!strcmp(cmd, "volume") && value.is<int>()) return Controller::post(Cmd::Volume, value.as<int>());
  if (!strcmp(cmd, "volumeUp")) return Controller::post(Cmd::VolumeDelta, 1);
  if (!strcmp(cmd, "volumeDown")) return Controller::post(Cmd::VolumeDelta, -1);
  if (!strcmp(cmd, "source") && value.is<const char*>()) {
    Mode m;
    return Settings::parseMode(value.as<const char*>(), m) && Controller::post(Cmd::Source, 0, 0, value.as<const char*>());
  }
  if (!strcmp(cmd, "station") && value.is<int>()) return Controller::post(Cmd::Station, value.as<int>());
  if (!strcmp(cmd, "file") && value.is<const char*>()) return Controller::post(Cmd::File, 0, 0, value.as<const char*>());
  if (!strcmp(cmd, "settings") && value.is<JsonObjectConst>()) {
    String text;
    serializeJson(value, text);
    if (text.length() >= sizeof(Command::text)) return false;
    return Controller::post(Cmd::Settings, 0, 0, text.c_str());
  }
  if (!strcmp(cmd, "restart")) return Controller::post(Cmd::Restart);
  if (!strcmp(cmd, "wifiSetup")) return Controller::post(Cmd::WifiSetup);
  if (!strcmp(cmd, "ota")) return Controller::post(Cmd::Ota);
  return false;
}

String wrapStatus(const String& json) {
  return "{\"t\":\"status\",\"status\":" + (json.length() ? json : String("{}")) + "}";
}

void onSocket(AsyncWebSocket*, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("[api] ws client %u connected\n", client->id());
    client->text(wrapStatus(Controller::statusJson()));
    return;
  }
  if (type != WS_EVT_DATA) return;
  AwsFrameInfo* info = static_cast<AwsFrameInfo*>(arg);
  if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) return;
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) {
    client->text("{\"t\":\"err\",\"error\":\"bad json\"}");
    return;
  }
  bool ok = execute(doc.as<JsonObjectConst>());
  client->text(ok ? "{\"t\":\"ack\"}" : "{\"t\":\"err\",\"error\":\"rejected\"}");
}

void stationsChanged(AsyncWebServerRequest* req, bool ok) {
  if (!ok) {
    sendError(req, 400, "invalid station");
    return;
  }
  Controller::post(Cmd::StationsChanged);
  sendOk(req);
}

void routeApi() {
  server->on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "application/json", Controller::statusJson());
  });

  server->addHandler(new AsyncCallbackJsonWebHandler("/api/cmd", [](AsyncWebServerRequest* req, JsonVariant& json) {
    if (execute(json.as<JsonObjectConst>())) sendOk(req);
    else sendError(req, 400, "unknown or invalid command");
  }));

  server->on("/api/stations", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    Stations::toJson(doc.to<JsonArray>());
    sendJson(req, 200, doc);
  });

  server->on("/api/stations", HTTP_DELETE, [](AsyncWebServerRequest* req) {
    stationsChanged(req, Stations::remove(param(req, "index", "-1").toInt()));
  });

  server->addHandler(new AsyncCallbackJsonWebHandler("/api/stations/edit", [](AsyncWebServerRequest* req, JsonVariant& json) {
    stationsChanged(req, Stations::update(json["index"] | -1, {json["name"] | "", json["url"] | ""}));
  }));

  server->addHandler(new AsyncCallbackJsonWebHandler("/api/stations/move", [](AsyncWebServerRequest* req, JsonVariant& json) {
    stationsChanged(req, Stations::move(json["from"] | -1, json["to"] | -1));
  }));

  server->addHandler(new AsyncCallbackJsonWebHandler("/api/stations", [](AsyncWebServerRequest* req, JsonVariant& json) {
    if (req->method() == HTTP_PUT) {
      stationsChanged(req, json.is<JsonArray>() && Stations::replace(json.as<JsonArrayConst>()));
      return;
    }
    stationsChanged(req, Stations::add({json["name"] | "", json["url"] | ""}));
  }));

  server->on("/api/files", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!SdCard::ready()) {
      sendError(req, 503, "no SD card");
      return;
    }
    String path = SdCard::normalize(param(req, "path", "/"));
    std::vector<SdEntry> entries;
    if (!SdCard::list(path, entries, false)) {
      sendError(req, 404, "not a directory");
      return;
    }
    JsonDocument doc;
    doc["path"] = path;
    doc["total"] = SdCard::totalBytes();
    doc["used"] = SdCard::usedBytes();
    JsonArray arr = doc["entries"].to<JsonArray>();
    for (const SdEntry& e : entries) {
      JsonObject o = arr.add<JsonObject>();
      o["name"] = e.name;
      o["dir"] = e.dir;
      o["size"] = e.size;
      o["audio"] = !e.dir && SdCard::isAudio(e.name);
    }
    sendJson(req, 200, doc);
  });

  server->on("/api/files", HTTP_DELETE, [](AsyncWebServerRequest* req) {
    String path = param(req, "path");
    Serial.printf("[api] delete %s\n", path.c_str());
    if (SdCard::remove(path)) sendOk(req);
    else sendError(req, 400, "delete failed");
  });

  server->on("/api/mkdir", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (SdCard::makeDir(param(req, "path"))) sendOk(req);
    else sendError(req, 400, "mkdir failed");
  });

  server->on(
      "/api/upload", HTTP_POST,
      [](AsyncWebServerRequest* req) {
        if (uploadFailed) sendError(req, 500, "upload failed");
        else sendOk(req);
      },
      [](AsyncWebServerRequest* req, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
        if (index == 0) {
          uploadFailed = !SdCard::ready();
          uploadPath = SdCard::join(param(req, "path", "/"), SdCard::fileName(filename));
          if (!uploadFailed) upload = SD.open(uploadPath, FILE_WRITE);
          uploadFailed = !upload;
          Serial.printf("[api] upload %s%s, free heap %u\n", uploadPath.c_str(), uploadFailed ? " FAILED" : "",
                        (unsigned)ESP.getFreeHeap());
        }
        if (upload && len && upload.write(data, len) != len) uploadFailed = true;
        if (final && upload) {
          Serial.printf("[api] upload done %s, %u bytes\n", uploadPath.c_str(), (unsigned)upload.size());
          upload.close();
        }
      });
}

void routeFallback() {
  server->onNotFound([](AsyncWebServerRequest* req) {
    sendError(req, 404, "not found");
  });
}

}

void begin() {
  if (server) return;
  server = new AsyncWebServer(80);
  socket = new AsyncWebSocket("/ws");
  socket->onEvent(onSocket);
  server->addHandler(socket);
  routeApi();
  routeFallback();
  server->begin();
  Serial.println("[api] server on port 80");
}

void loop() {
  if (!socket || millis() - cleanupAt < 1000) return;
  cleanupAt = millis();
  socket->cleanupClients(3);
}

void broadcastStatus(const String& json) {
  if (socket && socket->count()) socket->textAll(wrapStatus(json));
}

void broadcastTouch(uint8_t page, uint8_t id) {
  if (!socket || !socket->count()) return;
  socket->textAll("{\"t\":\"touch\",\"page\":" + String(page) + ",\"id\":" + String(id) + "}");
}

}
