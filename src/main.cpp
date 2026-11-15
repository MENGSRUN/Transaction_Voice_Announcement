#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "storage/config_store.h"
#include "audio/audio_player.h"
#include "display/oled_display.h"
#include "network/wifi_svc.h"
#include "network/time_sync.h"
#include "network/mqtt_svc.h"

static char mqttServer[40] = "";
static VoiceCommand activeCommand = {};
static unsigned long completedAt = 0;
static bool displayNeedsReady = false;
static uint32_t transactionCount = 0;
static unsigned long lastScreenRefresh = 0;

void onVoiceCommandReceived(const VoiceCommand& cmd) {
    activeCommand = cmd;
    transactionCount++;

    Serial.printf("Main: Queued transaction #%lu (%s) for audio: %s\n",
                  (unsigned long)transactionCount, cmd.id, cmd.url);

    OledDisplay::showPlaying(cmd.id, transactionCount);
    MqttService::publishStatus(cmd.id, "processing");

    if (!AudioPlayer::startVoiceStream(cmd.url)) {
        OledDisplay::showError("Stream open failed");
        MqttService::publishStatus(cmd.id, "failed", "stream open failed");
        activeCommand = {};
        completedAt = millis();
        displayNeedsReady = true;
    } else {
        MqttService::publishStatus(cmd.id, "playing");
    }
}

void onPlaybackComplete(bool success) {
    if (activeCommand.id[0] != '\0') {
        MqttService::publishStatus(activeCommand.id, success ? "completed" : "failed",
                                  success ? nullptr : "decoder stopped early");
        activeCommand = {};
    }

    if (success) {
        OledDisplay::showCompleted(transactionCount);
    } else {
        OledDisplay::showError("Decode failed");
    }

    completedAt = millis();
    displayNeedsReady = true;
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n========================================================");
    Serial.println("  Luysot Speaker ESP32-S3 (Modular Low-Latency Engine)  ");
    Serial.println("========================================================");

    // 1. OLED Display Initialization (GPIO 15 & 16)
    OledDisplay::init();

    // 2. Storage & Double-Reset safety check
    ConfigStore::init();
    if (ConfigStore::isDoubleResetDetected()) {
        Serial.println("Main: Double reset detected; resetting saved configuration.");
        ConfigStore::resetAllSettings();
    }

    if (!ConfigStore::loadMqttServer(mqttServer, sizeof(mqttServer))) {
        strlcpy(mqttServer, DEFAULT_MQTT_SERVER, sizeof(mqttServer));
    }

    // 3. Hardware I2S Audio Driver Initialization
    if (!AudioPlayer::init()) {
        Serial.println("Main: ERROR - Failed to initialize I2S audio driver");
    } else {
        // Boot sound test: confirm I2S & MAX98357A speaker wiring
        Serial.println("Main: Sound test (Cash Register Ding)...");
        AudioPlayer::playCashDing();
    }

    AudioPlayer::setCompletionCallback(onPlaybackComplete);

    // 4. Network Setup (WiFi + Captive Portal fallback)
    OledDisplay::showWiFiConnecting(nullptr);
    WiFiService::connect(mqttServer, sizeof(mqttServer));
    OledDisplay::showWiFiConnected(WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
    delay(800);

    // 5. Time Sync (NTP for Let's Encrypt TLS validation)
    TimeSync::sync(CLOCK_SYNC_TIMEOUT_MS);

    // 6. MQTT Subscriptions (QoS 1)
    MqttService::init(mqttServer, DEFAULT_MQTT_PORT, onVoiceCommandReceived);

    OledDisplay::showReady(WiFi.SSID().c_str(), WiFi.RSSI(), WiFi.localIP().toString().c_str(), transactionCount);
    lastScreenRefresh = millis();

    Serial.println("Main: System initialization complete. Ready for voice jobs!\n");
}

void loop() {
    static bool resetFlagHandled = false;
    if (!resetFlagHandled && millis() > DOUBLE_RESET_WINDOW_MS) {
        ConfigStore::clearResetFlag();
        resetFlagHandled = true;
    }

    // Reset screen back to Ready state after finishing transaction
    if (displayNeedsReady && (millis() - completedAt > 2500)) {
        displayNeedsReady = false;
        OledDisplay::showReady(WiFi.SSID().c_str(), WiFi.RSSI(), WiFi.localIP().toString().c_str(), transactionCount);
        lastScreenRefresh = millis();
    }

    // Periodic screen refresh (every 10s) when idle to update WiFi signal strength bars
    if (!AudioPlayer::isPlaying() && !displayNeedsReady && (millis() - lastScreenRefresh > 10000)) {
        lastScreenRefresh = millis();
        OledDisplay::showReady(WiFi.SSID().c_str(), WiFi.RSSI(), WiFi.localIP().toString().c_str(), transactionCount);
    }

    // Real-time audio decode loop
    AudioPlayer::loop();

    // Background network loops
    WiFiService::loop();
    MqttService::loop();
}
