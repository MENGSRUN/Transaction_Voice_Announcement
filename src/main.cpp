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

// FIFO Transaction Queue for Rush-Hour Payments
static VoiceCommand cmdQueue[AUDIO_QUEUE_DEPTH];
static uint8_t queueHead = 0;
static uint8_t queueTail = 0;
static uint8_t queueCount = 0;

static bool enqueueCommand(const VoiceCommand& cmd) {
    if (queueCount >= AUDIO_QUEUE_DEPTH) {
        return false;
    }
    cmdQueue[queueTail] = cmd;
    queueTail = (queueTail + 1) % AUDIO_QUEUE_DEPTH;
    queueCount++;
    return true;
}

static bool dequeueCommand(VoiceCommand& outCmd) {
    if (queueCount == 0) return false;
    outCmd = cmdQueue[queueHead];
    queueHead = (queueHead + 1) % AUDIO_QUEUE_DEPTH;
    queueCount--;
    return true;
}

static void playTransaction(const VoiceCommand& cmd) {
    activeCommand = cmd;
    transactionCount++;

    // 1. FAST PATH (<2ms): Launch audio playback and background download immediately!
    bool started = AudioPlayer::startVoiceStream(cmd.url);

    // 2. Perform display update and MQTT publish while Ding is ALREADY ringing in the speaker
    OledDisplay::showPlaying(cmd.id, transactionCount);

    if (!started) {
        OledDisplay::showError("Stream open failed");
        MqttService::publishStatus(cmd.id, "failed", "stream open failed");
        activeCommand = {};
        completedAt = millis();
        displayNeedsReady = true;
    } else {
        MqttService::publishStatus(cmd.id, "playing");
    }
}

void onVoiceCommandReceived(const VoiceCommand& cmd) {
    if (!AudioPlayer::isPlaying() && activeCommand.id[0] == '\0') {
        playTransaction(cmd);
    } else {
        // Audio is busy; queue the incoming transaction
        if (enqueueCommand(cmd)) {
            Serial.printf("Main: Audio busy. Queued transaction %s (queue: %d/%d)\n",
                          cmd.id, queueCount, AUDIO_QUEUE_DEPTH);
            MqttService::publishStatus(cmd.id, "queued");
        } else {
            Serial.printf("Main: Queue FULL! Dropping transaction %s\n", cmd.id);
            MqttService::publishStatus(cmd.id, "failed", "queue full");
        }
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

    // Check if there are queued transactions waiting to be played
    if (queueCount > 0) {
        VoiceCommand nextCmd;
        if (dequeueCommand(nextCmd)) {
            Serial.printf("Main: Playing next queued transaction %s (%d remaining in queue)\n",
                          nextCmd.id, queueCount);
            playTransaction(nextCmd);
            return;
        }
    }

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

    bool isOnline = WiFi.isConnected() && MqttService::isConnected();
    OledDisplay::showReady(WiFi.SSID().c_str(), WiFi.RSSI(), WiFi.localIP().toString().c_str(),
                           transactionCount, isOnline);
    lastScreenRefresh = millis();

    Serial.println("Main: System initialization complete. Ready for voice jobs!\n");
}

void loop() {
    static bool resetFlagHandled = false;
    if (!resetFlagHandled && millis() > DOUBLE_RESET_WINDOW_MS) {
        ConfigStore::clearResetFlag();
        resetFlagHandled = true;
    }

    bool isOnline = WiFi.isConnected() && MqttService::isConnected();
    static bool lastOnline = true;

    // Reset screen back to Ready state after finishing transaction
    if (displayNeedsReady && (millis() - completedAt > 2500)) {
        displayNeedsReady = false;
        OledDisplay::showReady(WiFi.SSID().c_str(), WiFi.RSSI(), WiFi.localIP().toString().c_str(),
                               transactionCount, isOnline);
        lastScreenRefresh = millis();
    }

    // Refresh screen if online status changed or periodic idle refresh (every 10s)
    if (!AudioPlayer::isPlaying() && !displayNeedsReady) {
        if (isOnline != lastOnline || millis() - lastScreenRefresh > 10000) {
            lastOnline = isOnline;
            lastScreenRefresh = millis();
            OledDisplay::showReady(WiFi.SSID().c_str(), WiFi.RSSI(), WiFi.localIP().toString().c_str(),
                                   transactionCount, isOnline);
        }
    }

    // Real-time audio decode loop
    AudioPlayer::loop();

    // Background network loops
    WiFiService::loop();
    MqttService::loop();
}
