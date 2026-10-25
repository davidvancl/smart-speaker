#include "audio/bt_sink.h"

#include "AudioTools.h"
#include "BluetoothA2DPSink.h"
#include "audio/output.h"
#include "command.h"
#include "config.h"

namespace BtSink {

namespace {

BluetoothA2DPSink* a2dp = nullptr;
A2DPNoVolumeControl noVolume;
bool started = false;

uint8_t toA2dp(uint8_t volume) {
  return static_cast<uint8_t>((volume * 127 + Cfg::VOLUME_MAX / 2) / Cfg::VOLUME_MAX);
}

uint8_t fromA2dp(int value) {
  return static_cast<uint8_t>((constrain(value, 0, 127) * Cfg::VOLUME_MAX + 63) / 127);
}

void onConnection(esp_a2d_connection_state_t state, void*) {
  bool connected = state == ESP_A2D_CONNECTION_STATE_CONNECTED;
  Serial.printf("[bt] %s\n", a2dp->to_str(state));
  if (state == ESP_A2D_CONNECTION_STATE_CONNECTED || state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
    Controller::post(Cmd::BtConnection, connected ? 1 : 0);
  }
}

void onAudioState(esp_a2d_audio_state_t state, void*) {
  bool playing = state == ESP_A2D_AUDIO_STATE_STARTED;
  Serial.printf("[bt] audio %s\n", playing ? "started" : "stopped");
  Controller::post(Cmd::BtPlaying, playing ? 1 : 0);
}

void onPlayStatus(esp_avrc_playback_stat_t status) {
  Serial.printf("[bt] play status %d\n", status);
  if (status == ESP_AVRC_PLAYBACK_PLAYING) Controller::post(Cmd::BtPlaying, 1);
  if (status == ESP_AVRC_PLAYBACK_PAUSED || status == ESP_AVRC_PLAYBACK_STOPPED) Controller::post(Cmd::BtPlaying, 0);
}

void onMetadata(uint8_t id, const uint8_t* text) {
  const char* value = reinterpret_cast<const char*>(text);
  Serial.printf("[bt] meta 0x%02X: %s\n", id, value);
  MetaField field;
  switch (id) {
    case ESP_AVRC_MD_ATTR_TITLE:
      field = MetaField::Title;
      break;
    case ESP_AVRC_MD_ATTR_ARTIST:
      field = MetaField::Artist;
      break;
    case ESP_AVRC_MD_ATTR_ALBUM:
      field = MetaField::Album;
      break;
    default:
      return;
  }
  Controller::post(Cmd::BtMeta, static_cast<int32_t>(field), 0, value);
}

void onVolume(int value) {
  Serial.printf("[bt] remote volume %d/127\n", value);
  Controller::post(Cmd::BtVolume, fromA2dp(value));
}

}

void begin(uint8_t volume) {
  a2dp = new BluetoothA2DPSink(AudioOut::sink());
  a2dp->set_volume_control(&noVolume);
  a2dp->set_avrc_metadata_attribute_mask(ESP_AVRC_MD_ATTR_TITLE | ESP_AVRC_MD_ATTR_ARTIST | ESP_AVRC_MD_ATTR_ALBUM);
  a2dp->set_avrc_metadata_callback(onMetadata);
  a2dp->set_avrc_rn_playstatus_callback(onPlayStatus);
  a2dp->set_avrc_rn_volumechange(onVolume);
  a2dp->set_on_connection_state_changed(onConnection);
  a2dp->set_on_audio_state_changed(onAudioState);
  a2dp->set_auto_reconnect(true);
  a2dp->start(Cfg::DEVICE_NAME, true);
  a2dp->set_volume(toA2dp(volume));
  started = true;
  Serial.printf("[bt] sink \"%s\" started, free heap %u\n", Cfg::DEVICE_NAME, (unsigned)ESP.getFreeHeap());
}

void play() {
  if (connected()) a2dp->play();
}

void pause() {
  if (connected()) a2dp->pause();
}

void next() {
  if (connected()) a2dp->next();
}

void previous() {
  if (connected()) a2dp->previous();
}

void setVolume(uint8_t volume) {
  if (started) a2dp->set_volume(toA2dp(volume));
}

bool connected() {
  return started && a2dp->is_connected();
}

String deviceName() {
  if (!connected()) return String();
  const char* name = a2dp->get_peer_name();
  return name ? String(name) : String();
}

void end() {
  if (!started) return;
  a2dp->set_auto_reconnect(false);
  a2dp->disconnect();
  started = false;
}

}
