#include "audio/dsp.h"

#include "config.h"

namespace {

constexpr float GAIN_SMOOTHING = 0.002f;

inline int16_t toPcm(float x) {
  float s = x * 32767.0f;
  if (s > 32767.0f) return 32767;
  if (s < -32768.0f) return -32768;
  return static_cast<int16_t>(s);
}

}

float Dsp::volumeDb(uint8_t volume) {
  return Cfg::VOLUME_MIN_DB + (Cfg::VOLUME_MAX_DB - Cfg::VOLUME_MIN_DB) * volume / Cfg::VOLUME_MAX;
}

float Dsp::shelfFor(const DspParams& p) {
  if (!p.bass) return 0;
  float db = p.bassLevel * Cfg::DSP_BASS_DB_PER_LEVEL;
  if (p.loudness) db += Cfg::DSP_LOUDNESS_MAX_DB * (1.0f - static_cast<float>(p.volume) / Cfg::VOLUME_MAX);
  return min(db, Cfg::DSP_SHELF_MAX_DB);
}

void Dsp::setOutput(audio_tools::AudioStream& out) {
  output = &out;
}

void Dsp::setParams(const DspParams& params) {
  portENTER_CRITICAL(&mux);
  pending = params;
  pendingRev++;
  portEXIT_CRITICAL(&mux);
}

void Dsp::setAudioInfo(audio_tools::AudioInfo info) {
  audio_tools::AudioOutput::setAudioInfo(info);
  supported = info.bits_per_sample == 16 && (info.channels == 1 || info.channels == 2);
  if (!supported) {
    Serial.printf("[dsp] unsupported format %d bit, %d ch\n", info.bits_per_sample, info.channels);
    return;
  }
  channels = info.channels;
  if (info.sample_rate != rate) {
    rate = info.sample_rate;
    appliedRev = 0;
  }
  if (output) {
    audio_tools::AudioInfo out(rate, 2, 16);
    output->setAudioInfo(out);
  }
  Serial.printf("[dsp] input %u Hz, %d ch\n", (unsigned)rate, channels);
}

bool Dsp::begin() {
  is_active = true;
  return true;
}

void Dsp::end() {
  is_active = false;
}

void Dsp::apply() {
  DspParams p;
  portENTER_CRITICAL(&mux);
  p = pending;
  appliedRev = pendingRev;
  portEXIT_CRITICAL(&mux);

  float shelfDb = shelfFor(p);
  float gainDb = min(volumeDb(p.volume), Cfg::VOLUME_MAX_DB - shelfDb * Cfg::DSP_HEADROOM_RATIO);
  shelfActive = shelfDb > 0.05f;
  for (int c = 0; c < 2; c++) {
    for (auto& stage : highPass[c]) stage.begin(Cfg::DSP_HIGHPASS_HZ, rate);
    if (shelfActive) shelf[c].begin(Cfg::DSP_SHELF_HZ, rate, shelfDb);
  }
  release = 1.0f - expf(-1000.0f / (Cfg::DSP_LIMITER_RELEASE_MS * rate));
  gainTarget = (p.muted || p.volume == 0) ? 0.0f : powf(10.0f, gainDb / 20.0f);
}

void Dsp::writeOut(const int16_t* samples, size_t bytes) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(samples);
  while (bytes > 0) {
    size_t written = output->write(p, bytes);
    if (written == 0) {
      vTaskDelay(1);
      continue;
    }
    p += written;
    bytes -= written;
  }
}

size_t Dsp::write(const uint8_t* data, size_t len) {
  if (!output) return len;
  if (appliedRev != pendingRev) apply();
  if (!supported) return len;

  const int16_t* in = reinterpret_cast<const int16_t*>(data);
  size_t frames = len / (2 * channels);
  uint16_t blockPeak = 0;

  while (frames > 0) {
    size_t n = min(frames, BLOCK_FRAMES);
    for (size_t i = 0; i < n; i++) {
      float l = in[0] / 32768.0f;
      float r = channels == 2 ? in[1] / 32768.0f : l;
      in += channels;

      for (auto& stage : highPass[0]) l = stage.process(l);
      for (auto& stage : highPass[1]) r = stage.process(r);
      if (shelfActive) {
        l = shelf[0].process(l);
        r = shelf[1].process(r);
      }
      gain += (gainTarget - gain) * GAIN_SMOOTHING;
      l *= gain;
      r *= gain;
      float level = max(fabsf(l), fabsf(r));
      float ceiling = level > Cfg::DSP_LIMITER_THRESHOLD ? Cfg::DSP_LIMITER_THRESHOLD / level : 1.0f;
      limit = min(limit + (1.0f - limit) * release, ceiling);
      int16_t sl = toPcm(l * limit);
      int16_t sr = toPcm(r * limit);
      block[i * 2] = sl;
      block[i * 2 + 1] = sr;
      uint16_t a = static_cast<uint16_t>(max(abs(sl), abs(sr)));
      if (a > blockPeak) blockPeak = a;
    }
    writeOut(block, n * 4);
    frames -= n;
  }

  uint16_t prev = peak.load();
  while (blockPeak > prev && !peak.compare_exchange_weak(prev, blockPeak)) {
  }
  return len;
}

uint16_t Dsp::takePeak() {
  return peak.exchange(0);
}
