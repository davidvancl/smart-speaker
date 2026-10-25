#include "audio/stream_player.h"

#include <SD.h>

#include <atomic>
#include <memory>

#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecAACHelix.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/AudioCodecs/CodecWAV.h"
#include "AudioTools/Communication/HTTP/ICYStream.h"
#include "audio/output.h"
#include "command.h"
#include "config.h"

namespace StreamPlayer {

namespace {

enum class Kind : uint8_t { Url, File, Pause, Resume, Stop };
enum class Source : uint8_t { None, Url, File };
enum class Codec : uint8_t { Mp3, Aac, Wav };

struct Request {
  Kind kind;
  uint32_t offset;
  char path[256];
};

class RadioStream : public audio_tools::ICYStream {
 public:
  RadioStream() : audio_tools::ICYStream(1024) {}
  void setClientTimeout(int ms) { url.setTimeout(ms); }
};

QueueHandle_t requests = nullptr;
RadioStream radio;
fs::File file;
std::unique_ptr<audio_tools::RingBuffer<uint8_t>> ring;
audio_tools::MetaDataID3 id3;
std::unique_ptr<audio_tools::AudioDecoder> decoder;
Source source = Source::None;
String current;
bool paused = false;
bool decoding = false;
size_t prebuffer = Cfg::STREAM_PREBUFFER_BYTES;
uint32_t lastData = 0;
uint8_t retries = 0;
std::atomic<uint32_t> position{0};
uint8_t chunk[1024];

void phase(StreamPhase p) {
  Controller::post(Cmd::StreamState, static_cast<int32_t>(p));
}

void meta(MetaField field, const String& text) {
  Controller::post(Cmd::StreamMeta, static_cast<int32_t>(field), 0, text.c_str());
}

String metaText(const char* str, int len) {
  String text;
  text.reserve(len);
  for (int i = 0; i < len && str[i]; i++) text += str[i];
  text.trim();
  return text;
}

void onIcy(audio_tools::MetaDataType type, const char* str, int len) {
  String text = metaText(str, len);
  while (text.endsWith("'") || text.endsWith(";")) text.remove(text.length() - 1);
  Serial.printf("[stream] icy %s: %s\n", audio_tools::MetaDataTypeStr[type], text.c_str());
  if (type == audio_tools::Title) meta(MetaField::Title, text);
  if (type == audio_tools::Name) meta(MetaField::Name, text);
}

void onId3(audio_tools::MetaDataType type, const char* str, int len) {
  String text = metaText(str, len);
  Serial.printf("[stream] id3 %s: %s\n", audio_tools::MetaDataTypeStr[type], text.c_str());
  if (type == audio_tools::Title) meta(MetaField::Title, text);
  if (type == audio_tools::Artist) meta(MetaField::Artist, text);
  if (type == audio_tools::Album) meta(MetaField::Album, text);
}

bool allocateRing() {
  ring.reset(new (std::nothrow) audio_tools::RingBuffer<uint8_t>(Cfg::STREAM_BUFFER_BYTES));
  return ring && ring->address() != nullptr;
}

Codec codecFor(const String& name, const char* mime) {
  String m = mime ? String(mime) : String();
  m.toLowerCase();
  if (m.indexOf("aac") >= 0) return Codec::Aac;
  if (m.indexOf("mpeg") >= 0 || m.indexOf("mp3") >= 0) return Codec::Mp3;
  if (m.indexOf("wav") >= 0) return Codec::Wav;
  String n = name;
  n.toLowerCase();
  if (n.endsWith(".aac")) return Codec::Aac;
  if (n.endsWith(".wav")) return Codec::Wav;
  return Codec::Mp3;
}

bool startDecoder(Codec codec) {
  switch (codec) {
    case Codec::Aac:
      decoder.reset(new audio_tools::AACDecoderHelix());
      break;
    case Codec::Wav:
      decoder.reset(new audio_tools::WAVDecoder());
      break;
    default:
      decoder.reset(new audio_tools::MP3DecoderHelix());
      break;
  }
  decoder->setOutput(AudioOut::sink());
  bool ok = decoder->begin();
  Serial.printf("[stream] decoder %s %s, free heap %u\n", codec == Codec::Aac ? "AAC" : codec == Codec::Wav ? "WAV" : "MP3",
                ok ? "ready" : "failed", (unsigned)ESP.getFreeHeap());
  return ok;
}

void close() {
  if (decoder) {
    decoder->end();
    decoder.reset();
  }
  if (source == Source::Url) radio.end();
  if (file) file.close();
  ring.reset();
  source = Source::None;
  decoding = false;
}

bool openUrl() {
  phase(StreamPhase::Connecting);
  Serial.printf("[stream] connecting %s\n", current.c_str());
  radio.setMetadataCallback(onIcy);
  radio.setClientTimeout(Cfg::STREAM_CONNECT_TIMEOUT_MS);
  if (!radio.begin(current.c_str())) {
    Serial.println("[stream] connection failed");
    radio.end();
    return false;
  }
  const char* mime = radio.getReplyHeader("Content-Type");
  const char* bitrate = radio.getReplyHeader("icy-br");
  Serial.printf("[stream] connected, type %s, bitrate %s\n", mime ? mime : "?", bitrate ? bitrate : "?");
  if (bitrate) Controller::post(Cmd::StreamInfo, atoi(bitrate));
  if (!allocateRing() || !startDecoder(codecFor(current, mime))) {
    Serial.println("[stream] out of memory");
    radio.end();
    ring.reset();
    decoder.reset();
    return false;
  }
  source = Source::Url;
  decoding = false;
  prebuffer = Cfg::STREAM_PREBUFFER_BYTES;
  lastData = millis();
  phase(StreamPhase::Buffering);
  return true;
}

void startUrl() {
  close();
  for (retries = 0; retries < Cfg::STREAM_RETRIES; retries++) {
    if (openUrl()) {
      retries = 0;
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  phase(StreamPhase::Failed);
}

void startFile(uint32_t offset) {
  close();
  file = SD.open(current, FILE_READ);
  if (!file) {
    Serial.printf("[stream] cannot open %s\n", current.c_str());
    phase(StreamPhase::Failed);
    return;
  }
  id3.setCallback(onId3);
  id3.begin();
  if (offset > 0 && offset < file.size()) {
    for (uint32_t read = 0; read < Cfg::ID3_RESUME_SCAN_BYTES && read < offset;) {
      int n = file.read(chunk, sizeof(chunk));
      if (n <= 0) break;
      id3.write(chunk, n);
      read += n;
    }
    file.seek(offset);
  }
  if (!startDecoder(codecFor(current, nullptr))) {
    file.close();
    phase(StreamPhase::Failed);
    return;
  }
  source = Source::File;
  position = file.position();
  Serial.printf("[stream] playing %s from %u (%u bytes)\n", current.c_str(), (unsigned)file.position(), (unsigned)file.size());
  phase(StreamPhase::Playing);
}

void stepUrl() {
  bool busy = false;
  int avail = radio.available();
  if (avail > 0 && static_cast<size_t>(ring->availableForWrite()) > 0) {
    size_t n = radio.readBytes(chunk, min({static_cast<size_t>(avail), static_cast<size_t>(ring->availableForWrite()), sizeof(chunk)}));
    if (n > 0) {
      ring->writeArray(chunk, n);
      lastData = millis();
      busy = true;
    }
  }

  if (!decoding && ring->available() >= prebuffer) {
    decoding = true;
    phase(StreamPhase::Playing);
  }
  if (decoding) {
    if (ring->available() == 0) {
      decoding = false;
      prebuffer = Cfg::STREAM_REBUFFER_BYTES;
      Serial.println("[stream] buffer underrun");
      phase(StreamPhase::Buffering);
    } else {
      size_t n = ring->readArray(chunk, 512);
      decoder->write(chunk, n);
      busy = true;
    }
  }

  if (millis() - lastData > Cfg::STREAM_TIMEOUT_MS) {
    Serial.println("[stream] no data, reconnecting");
    startUrl();
    return;
  }
  if (!busy) vTaskDelay(pdMS_TO_TICKS(5));
}

void stepFile() {
  int n = file.read(chunk, sizeof(chunk));
  if (n <= 0) {
    Serial.printf("[stream] end of %s\n", current.c_str());
    close();
    position = 0;
    Controller::post(Cmd::StreamEnded);
    return;
  }
  id3.write(chunk, n);
  decoder->write(chunk, n);
  position = file.position();
}

void handle(const Request& req) {
  switch (req.kind) {
    case Kind::Url:
      current = req.path;
      paused = false;
      startUrl();
      break;
    case Kind::File:
      current = req.path;
      paused = false;
      startFile(req.offset);
      break;
    case Kind::Pause:
      if (source == Source::None || paused) break;
      paused = true;
      if (source == Source::Url) close();
      phase(StreamPhase::Paused);
      break;
    case Kind::Resume:
      if (!paused) break;
      paused = false;
      if (source == Source::File) {
        phase(StreamPhase::Playing);
      } else if (current.length()) {
        startUrl();
      }
      break;
    case Kind::Stop:
      close();
      paused = false;
      current = "";
      position = 0;
      phase(StreamPhase::Idle);
      break;
  }
}

void task(void*) {
  Request req;
  for (;;) {
    bool idle = source == Source::None || paused;
    if (xQueueReceive(requests, &req, idle ? portMAX_DELAY : 0) == pdTRUE) {
      handle(req);
      continue;
    }
    if (source == Source::Url) stepUrl();
    else if (source == Source::File) stepFile();
  }
}

void send(Kind kind, const String& path = String(), uint32_t offset = 0) {
  if (!requests) return;
  Request req;
  req.kind = kind;
  req.offset = offset;
  strlcpy(req.path, path.c_str(), sizeof(req.path));
  if (xQueueSend(requests, &req, pdMS_TO_TICKS(100)) != pdTRUE) Serial.println("[stream] request queue full");
}

}

void begin() {
  requests = xQueueCreate(4, sizeof(Request));
  xTaskCreatePinnedToCore(task, "audio", Cfg::AUDIO_TASK_STACK, nullptr, Cfg::AUDIO_TASK_PRIORITY, nullptr,
                          Cfg::AUDIO_TASK_CORE);
}

void playUrl(const String& url) {
  send(Kind::Url, url);
}

void playFile(const String& path, uint32_t offset) {
  send(Kind::File, path, offset);
}

void pause() {
  send(Kind::Pause);
}

void resume() {
  send(Kind::Resume);
}

void stop() {
  send(Kind::Stop);
}

uint32_t filePosition() {
  return position;
}

}
