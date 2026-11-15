#pragma once

#include <stdint.h>
#include <stddef.h>

// -----------------------------------------------------------------------------
// HARDWARE PIN DEFINITIONS (ESP32-S3 DevKitC-1 N16R8)
// -----------------------------------------------------------------------------
// MAX98357A I2S Amplifier
constexpr int I2S_BCLK_PIN = 4;
constexpr int I2S_LRC_PIN  = 5;
constexpr int I2S_DOUT_PIN = 6;

// Peripherals (OLED Display on GPIO 15 & 16)
constexpr int LED_PIN      = -1;    // S3 uses WS2812 RGB LED, disabled
constexpr bool OLED_ENABLED = true;
constexpr int OLED_SDA_PIN = 15;
constexpr int OLED_SCL_PIN = 16;
constexpr uint8_t OLED_ADDRESS = 0x3C;
constexpr int SCREEN_WIDTH  = 128;
constexpr int SCREEN_HEIGHT = 64;

// -----------------------------------------------------------------------------
// AUDIO CONFIGURATION
// -----------------------------------------------------------------------------
constexpr float VOICE_GAIN     = 1.0f;
constexpr float CASH_DING_GAIN = 0.7f;
constexpr size_t MAX_AUDIO_BYTES = 64 * 1024;    // 64 KB = ~16s of 32 kbps mono MP3
constexpr size_t PSRAM_AUDIO_BUFFER_SIZE = 64 * 1024; // Pre-cache buffer in 8MB PSRAM
constexpr size_t MAX_AUDIO_URL_LENGTH = 256;
constexpr size_t MAX_TRANSACTION_ID_LENGTH = 64;
constexpr uint8_t AUDIO_QUEUE_DEPTH = 4;

// -----------------------------------------------------------------------------
// NETWORK & CLOUD DEFAULTS
// -----------------------------------------------------------------------------
constexpr const char* DEFAULT_MQTT_SERVER = "192.168.18.120";
constexpr int DEFAULT_MQTT_PORT = 1883;
constexpr const char* MQTT_TOPIC_COMMANDS = "store/transaction-voice/commands";
constexpr const char* MQTT_TOPIC_STATUS   = "store/transaction-voice/status";

// Allowed media host for TLS stream validation
constexpr const char* ALLOWED_AUDIO_HOST  = "voice-service-api.weknowyouknow.dev";

// -----------------------------------------------------------------------------
// TIMING & TIMEOUTS
// -----------------------------------------------------------------------------
constexpr unsigned long DEFAULT_WIFI_TIMEOUT_MS   = 15000;
constexpr unsigned long CLOCK_SYNC_TIMEOUT_MS     = 10000;
constexpr unsigned long AUDIO_CONNECT_TIMEOUT_MS  = 8000;
constexpr unsigned long AUDIO_REQUEST_TIMEOUT_MS  = 10000;
constexpr unsigned long AUDIO_READ_TIMEOUT_MS     = 5000;
constexpr unsigned long AUDIO_RETRY_DELAY_MS      = 750;
constexpr uint8_t AUDIO_STREAM_CONNECT_ATTEMPTS   = 3;
constexpr unsigned long MQTT_RECONNECT_INTERVAL_MS = 5000;
constexpr unsigned long DOUBLE_RESET_WINDOW_MS    = 5000;

// -----------------------------------------------------------------------------
// STORAGE PATHS
// -----------------------------------------------------------------------------
constexpr const char* RESET_FLAG_PATH = "/reset_flag";
constexpr const char* CONFIG_FILE_PATH = "/config.json";
constexpr const char* WIFI_PORTAL_SSID = "Luysot_Speaker_Setup";
