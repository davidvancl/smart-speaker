#pragma once

#include <atomic>

#include "AudioTools.h"

struct DspParams {
  uint8_t volume = 0;
  bool muted = false;
  bool bass = true;
  uint8_t bassLevel = 0;
  bool loudness = true;
};

class Dsp : public audio_tools::AudioOutput {
 public:
  void setOutput(audio_tools::AudioStream& out);
  void setParams(const DspParams& params);
  void setAudioInfo(audio_tools::AudioInfo info) override;
  bool begin() override;
  void end() override;
  size_t write(const uint8_t* data, size_t len) override;
  uint16_t takePeak();

  static float volumeDb(uint8_t volume);
  static float shelfFor(const DspParams& params);

 private:
  static constexpr size_t BLOCK_FRAMES = 128;

  audio_tools::AudioStream* output = nullptr;
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  DspParams pending;
  uint32_t pendingRev = 1;
  uint32_t appliedRev = 0;
  uint32_t rate = 44100;
  uint8_t channels = 2;
  bool supported = true;
  audio_tools::HighPassFilter<float> highPass[2];
  audio_tools::LowShelfFilter<float> shelf[2];
  bool shelfActive = false;
  float gain = 0;
  float gainTarget = 0;
  std::atomic<uint16_t> peak{0};
  int16_t block[BLOCK_FRAMES * 2];

  void apply();
  void writeOut(const int16_t* samples, size_t bytes);
};
