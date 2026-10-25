#pragma once

#include <Arduino.h>

enum class Cmd : uint8_t {
  TogglePlay,
  Play,
  Pause,
  Next,
  Previous,
  ToggleMute,
  Volume,
  VolumeDelta,
  Source,
  Station,
  File,
  Settings,
  Restart,
  WifiSetup,
  Ota,
  StationsChanged,
  BtConnection,
  BtPlaying,
  BtMeta,
  BtVolume,
  StreamState,
  StreamMeta,
  StreamInfo,
  StreamEnded,
};

enum class StreamPhase : uint8_t { Idle, Connecting, Buffering, Playing, Paused, Failed };

enum class MetaField : uint8_t { Title, Artist, Album, Name };

struct Command {
  Cmd cmd;
  int32_t a;
  int32_t b;
  char text[240];
};

namespace Controller {

bool post(Cmd cmd, int32_t a = 0, int32_t b = 0, const char* text = nullptr);

}
