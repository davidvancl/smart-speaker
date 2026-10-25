#include "audio/output.h"

#include "config.h"

namespace AudioOut {

namespace {

audio_tools::I2SStream i2s;
Dsp dsp;

}

void begin() {
  auto cfg = i2s.defaultConfig(audio_tools::TX_MODE);
  cfg.pin_bck = Pins::I2S_BCK;
  cfg.pin_ws = Pins::I2S_LRCK;
  cfg.pin_data = Pins::I2S_DATA;
  cfg.sample_rate = 44100;
  cfg.channels = 2;
  cfg.bits_per_sample = 16;
  cfg.buffer_count = Cfg::I2S_DMA_COUNT;
  cfg.buffer_size = Cfg::I2S_DMA_SIZE;
  i2s.begin(cfg);
  dsp.setOutput(i2s);
  dsp.setAudioInfo(audio_tools::AudioInfo(44100, 2, 16));
  dsp.begin();
  Serial.println("[audio] I2S ready");
}

Dsp& sink() {
  return dsp;
}

void setParams(const DspParams& params) {
  dsp.setParams(params);
  Serial.printf("[audio] volume %u/%u%s, bass %s level %u%s -> shelf %.1f dB, gain %.1f dB\n", params.volume,
                Cfg::VOLUME_MAX, params.muted ? " (muted)" : "", params.bass ? "on" : "off", params.bassLevel,
                params.loudness ? " +loudness" : "", Dsp::shelfFor(params),
                min(Dsp::volumeDb(params.volume), Cfg::VOLUME_MAX_DB - Dsp::shelfFor(params) * Cfg::DSP_HEADROOM_RATIO));
}

void enableDac(bool on) {
  pinMode(Pins::DAC_XSMT, OUTPUT);
  digitalWrite(Pins::DAC_XSMT, on ? HIGH : LOW);
}

float level() {
  return dsp.takePeak() / 32767.0f;
}

}
