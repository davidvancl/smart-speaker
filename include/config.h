#pragma once

#include <Arduino.h>

namespace Pins {
constexpr int NX_RX = 16;
constexpr int NX_TX = 17;
constexpr int I2S_BCK = 26;
constexpr int I2S_LRCK = 25;
constexpr int I2S_DATA = 27;
constexpr int DAC_XSMT = 2;
constexpr int I2C_SDA = 21;
constexpr int I2C_SCL = 22;
constexpr int SD_SCK = 18;
constexpr int SD_MISO = 19;
constexpr int SD_MOSI = 23;
constexpr int SD_CS = 5;
constexpr int ENC_CLK = 32;
constexpr int ENC_DT = 33;
constexpr int ENC_SW = 13;
constexpr int BTN1 = 39;
constexpr int BTN2 = 34;
constexpr int BTN3 = 35;
constexpr int TOUCH = 4;
constexpr int LEDS = 14;
constexpr int BATTERY = 36;
}

namespace Cfg {
constexpr const char* DEVICE_NAME = "ESP32 Soundbar";
constexpr const char* HOSTNAME = "soundbar";
constexpr const char* TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr const char* NTP_SERVER_1 = "pool.ntp.org";
constexpr const char* NTP_SERVER_2 = "time.google.com";

constexpr uint32_t NEXTION_BAUD = 115200;
constexpr uint32_t NEXTION_GET_TIMEOUT_MS = 400;
constexpr uint32_t UI_REFRESH_MS = 100;

constexpr uint8_t VOLUME_MAX = 21;
constexpr uint8_t VOLUME_DEFAULT = 8;
constexpr float VOLUME_MIN_DB = -51.0f;
constexpr float VOLUME_MAX_DB = 0.0f;

constexpr float DSP_HIGHPASS_HZ = 70.0f;
constexpr float DSP_SHELF_HZ = 150.0f;
constexpr float DSP_BASS_DB_PER_LEVEL = 0.9f;
constexpr uint8_t DSP_BASS_LEVEL_MAX = 10;
constexpr float DSP_LOUDNESS_MAX_DB = 6.0f;
constexpr float DSP_SHELF_MAX_DB = 12.0f;
constexpr float DSP_HEADROOM_RATIO = 1.0f;
constexpr float DSP_LIMITER_THRESHOLD = 0.6f;
constexpr float DSP_LIMITER_RELEASE_MS = 150.0f;

constexpr int I2S_DMA_COUNT = 8;
constexpr int I2S_DMA_SIZE = 512;

constexpr size_t STREAM_BUFFER_BYTES = 24 * 1024;
constexpr size_t STREAM_PREBUFFER_BYTES = 12 * 1024;
constexpr size_t STREAM_REBUFFER_BYTES = 6 * 1024;
constexpr uint32_t STREAM_TIMEOUT_MS = 8000;
constexpr uint32_t STREAM_CONNECT_TIMEOUT_MS = 8000;
constexpr uint8_t STREAM_RETRIES = 3;
constexpr uint32_t ID3_RESUME_SCAN_BYTES = 16 * 1024;
constexpr uint32_t AUDIO_TASK_STACK = 12 * 1024;
constexpr UBaseType_t AUDIO_TASK_PRIORITY = 3;
constexpr BaseType_t AUDIO_TASK_CORE = 1;

constexpr uint32_t SD_SPI_HZ = 20000000;
constexpr size_t SD_MAX_ENTRIES = 300;

constexpr int ENC_STEPS_PER_DETENT = 4;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint32_t ENC_LONG_PRESS_MS = 2000;
constexpr uint32_t TOUCH_SAMPLE_MS = 20;
constexpr uint8_t TOUCH_CONFIRM_SAMPLES = 3;
constexpr float TOUCH_ON_RATIO = 0.75f;
constexpr float TOUCH_OFF_RATIO = 0.85f;
constexpr uint32_t TOUCH_ENCODER_LOCK_MS = 1500;

constexpr uint32_t BATTERY_SAMPLE_MS = 2000;
constexpr uint8_t BATTERY_AVERAGE = 10;
constexpr float BATTERY_DIVIDER = 2.0f;
constexpr float BATTERY_ABSENT_V = 2.5f;
constexpr float BATTERY_CRITICAL_V = 3.30f;
constexpr uint8_t BATTERY_CRITICAL_COUNT = 3;
constexpr uint8_t BATTERY_LOW_PERCENT = 10;
constexpr uint8_t BATTERY_LED_LIMIT_PERCENT = 20;

constexpr uint32_t CLIMATE_INTERVAL_MS = 30000;
constexpr uint32_t LIGHT_INTERVAL_MS = 1000;
constexpr float LUX_DARK = 2.0f;
constexpr float LUX_BRIGHT = 200.0f;
constexpr float AMBIENT_MIN_FACTOR = 0.25f;

constexpr uint16_t LED_COUNT = 40;
constexpr uint32_t LED_FRAME_MS = 33;
constexpr float LED_MA_PER_CHANNEL = 20.0f;
constexpr float LED_IDLE_MA = 1.0f;
constexpr float LED_BUDGET_MA = 700.0f;
constexpr float LED_BUDGET_LOW_BATTERY_MA = 200.0f;
constexpr uint32_t LED_BUSY_COLOR = 0xFF6A00;
constexpr uint8_t LED_BUSY_BRIGHTNESS = 96;
constexpr uint32_t LED_BUSY_BLINK_MS = 500;

constexpr uint32_t SETTINGS_SAVE_DELAY_MS = 3000;
constexpr uint32_t STATUS_REFRESH_MS = 2000;
constexpr uint32_t STATUS_MIN_INTERVAL_MS = 200;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t WIFI_RETRY_MS = 60000;
constexpr uint32_t RESTART_DELAY_MS = 600;
constexpr size_t COMMAND_QUEUE = 16;
}
