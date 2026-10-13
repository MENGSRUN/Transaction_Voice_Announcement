#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <FS.h>
#include <SPIFFS.h>
#include <memory>
#include <new>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>
#include <time.h>
#include "AudioFileSource.h"
#include "audio_stream_buffer.h"
#include "audio_https_stream.h"
#include "AudioFileSourceID3.h"
#include "AudioFileSourcePROGMEM.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include "cash_ding_mp3.h"
#include "audio_url_validation.h"
#include "letsencrypt_root_ca.h"
#include "wifi_credentials.h"
#include <WiFiManager.h> // Install "WiFiManager" by tzapu via Library Manager

const char* allowed_audio_host = "voice-service-api.weknowyouknow.dev"; // Media hosting must be on this exact domain and contain .mp3 in path to be played.
const unsigned long mqttReconnectIntervalMs = 5000;
const bool useDefaultWiFi = true; // Set false to use WiFiManager's saved network/setup portal.
const unsigned long defaultWifiTimeoutMs = 15000;
const unsigned long clockSyncTimeoutMs = 10000;
const unsigned long audioConnectTimeoutMs = 8000;
const unsigned long audioRequestTimeoutMs = 10000;
const unsigned long audioReadTimeoutMs = 5000;
const unsigned long audioRetryDelayMs = 750;
const uint8_t audioStreamConnectAttempts = 3;
const int ledPin = -1; // The S3 board's RGB LED is not a plain active-low LED.
const bool oledEnabled = false; // Set true when the optional OLED is connected.
const int oledSdaPin = 8;
const int oledSclPin = 9;
const uint8_t oledAddress = 0x3C;
const int screenWidth = 128;
const int screenHeight = 64;
const int i2sBclkPin = 4;
const int i2sLrcPin = 5;
const int i2sDoutPin = 6;
const size_t maxAudioBytes = 64 * 1024; // About 16 seconds at the Voice Service 32 kbps MP3 profile.
const size_t maxAudioUrlLength = 256;
const uint8_t audioQueueDepth = 4;
const float voiceGain = 1;
const float cashDingGain = 0.7;
const unsigned long i2sSettleMs = 80;
const unsigned long audioFinishedDisplayDelayMs = 250;
const unsigned long doubleResetWindowMs = 5000;
const char* resetFlagPath = "/reset_flag";

// --------------------------------------------------------------------------------
// CONFIGURATION
// --------------------------------------------------------------------------------
// WiFi credentials are now managed by WiFiManager (Web Interface)
char mqtt_server[40] = "192.168.18.120"; // Default IP, will be overwritten by config
const int mqtt_port = 1883;
const char* mqtt_topic = "store1/transaction-voice/commands";
const char* mqtt_status_topic = "store1/transaction-voice/status";
const size_t maxTransactionIdLength = 64;

// --------------------------------------------------------------------------------
// OBJECTS
// --------------------------------------------------------------------------------
WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_SSD1306 display(screenWidth, screenHeight, &Wire, -1);

AudioGeneratorMP3 *mp3;
AudioFileSource *file;
AudioFileSourceID3 *id3;
AudioOutputI2S *out;

alignas(8) uint8_t mp3Preallocate[AudioGeneratorMP3::preAllocSize()];
AudioHttpsStream *audioStream = NULL;
uint8_t streamBufferMemory[8 * 1024]; // Two seconds at 32 kbps.
struct AudioCommand {
  char id[maxTransactionIdLength + 1];
  char url[maxAudioUrlLength];
};

AudioCommand audioQueue[audioQueueDepth];
AudioCommand currentCommand = {};
uint8_t audioQueueHead = 0;
uint8_t audioQueueTail = 0;
uint8_t audioQueueCount = 0;

bool isPlaying = false;
bool displayReady = false;
uint8_t speakingFrame = 0;
unsigned long startPlayTime = 0;
unsigned long lastBlinkTime = 0;

// Flag for saving data
bool shouldSaveConfig = false;
bool resetFlagCleared = false;

void mqttCallback(char* topic, byte* payload, unsigned int length);
void audioStatusCallback(void *cbData, int code, const char *string);
void setupLed();
void ledWrite(bool off);
void ledToggle();
void setupDisplay();
void showStatus(const char* line1, const char* line2 = "", const char* line3 = "");
void showSpeakFace();
bool isAllowedAudioUrl(const char* url);
bool enqueueAudioCommand(const char* id, const char* url);
bool dequeueAudioCommand(AudioCommand& command);
void publishTransactionStatus(const char* id, const char* status, const char* detail = NULL);
void processQueuedAudio();
bool openAudioStream(const char* url);
bool waitForWiFi(unsigned long timeoutMs);
bool connectDefaultWiFi();
bool syncClock();
bool playAudio(const char* url);
void stopAudio();
bool setupAudioOutput();
void playCashDing();
void waitForAudioOutputSettle();
void resetSavedConfig();
void clearResetFlag();
void reconnect();

// Callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setupLed() {
  if (ledPin < 0) return;
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, HIGH);
}

void ledWrite(bool off) {
  if (ledPin < 0) return;
  digitalWrite(ledPin, off ? HIGH : LOW);
}

void ledToggle() {
  if (ledPin < 0) return;
  digitalWrite(ledPin, !digitalRead(ledPin));
}

void setupDisplay() {
  if (!oledEnabled) {
    Serial.println("OLED disabled; running without display");
    return;
  }
  Wire.begin(oledSdaPin, oledSclPin);
  displayReady = display.begin(SSD1306_SWITCHCAPVCC, oledAddress);
  if (!displayReady) {
    Serial.println("OLED not found");
    return;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Luysot Speaker");
  display.println("Booting...");
  display.display();
}

void showStatus(const char* line1, const char* line2, const char* line3) {
  if (!displayReady) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Luysot Speaker");
  display.drawLine(0, 10, screenWidth, 10, SSD1306_WHITE);
  display.setCursor(0, 16);
  display.println(line1);
  if (line2 && strlen(line2)) display.println(line2);
  if (line3 && strlen(line3)) display.println(line3);
  display.display();
}

void showSpeakFace() {
  if (!displayReady) return;

  display.clearDisplay();
  display.drawCircle(64, 30, 24, SSD1306_WHITE);
  display.fillCircle(54, 24, 3, SSD1306_WHITE);
  display.fillCircle(74, 24, 3, SSD1306_WHITE);

  if (speakingFrame % 2 == 0) {
    display.drawRoundRect(52, 38, 24, 10, 4, SSD1306_WHITE);
  } else {
    display.fillRoundRect(54, 36, 20, 16, 7, SSD1306_WHITE);
    display.fillRoundRect(57, 39, 14, 10, 4, SSD1306_BLACK);
  }

  display.setCursor(0, 56);
  display.print("Speaking...");
  display.display();
  speakingFrame++;
}

// --------------------------------------------------------------------------------
// SETUP
// --------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  unsigned long bootStartedAt = millis();
  
  setupLed();
  setupDisplay();
  showStatus("Starting...");

  // 0. Mount File System to load config
  if (SPIFFS.begin(true)) {
    if (SPIFFS.exists(resetFlagPath)) {
      Serial.println("Double reset detected. Clearing WiFi and MQTT settings...");
      showStatus("Double reset", "Clearing settings");
      resetSavedConfig();
    } else {
      File resetFlag = SPIFFS.open(resetFlagPath, "w");
      if (resetFlag) resetFlag.close();
      Serial.println("Reset flag set. Press RST again within 5s to clear WiFi and MQTT settings.");
    }

    if (SPIFFS.exists("/config.json")) {
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        size_t size = configFile.size();
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        
        JsonDocument json;
        DeserializationError error = deserializeJson(json, buf.get(), size);
        if (!error) {
          if (json["mqtt_server"].is<const char*>()) {
            strlcpy(mqtt_server, json["mqtt_server"], sizeof(mqtt_server));
          }
        }
      }
      configFile.close();
    }
  }

  if (!resetFlagCleared) {
    showStatus("Reset window", "Press RST to clear");
    while (millis() - bootStartedAt < doubleResetWindowMs) {
      delay(25);
    }
    clearResetFlag();
  }

  // 1. Connect to WiFi
  showStatus("WiFi connecting...");
  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  // Add custom parameter for MQTT Server
  WiFiManagerParameter custom_mqtt_server("server", "MQTT Server IP", mqtt_server, 40);
  wifiManager.addParameter(&custom_mqtt_server);

  // wifiManager.resetSettings(); // Uncomment this line and upload to reset saved WiFi settings
  
  // Try optional built-in WiFi first. If it fails, WiFiManager falls back to saved credentials or setup portal.
  if (!connectDefaultWiFi()) {
    // Tries to connect to saved WiFi. If it fails, it starts an AP named "Luysot_Speaker_Setup"
    // Connect to that AP with your phone to configure WiFi and MQTT IP.
    showStatus("Setup portal", "AP: Luysot_Speaker", "IP: 192.168.4.1");
    if (!wifiManager.autoConnect("Luysot_Speaker_Setup")) {
      Serial.println("WiFi setup failed. Restarting...");
      showStatus("WiFi failed", "Restarting...");
      delay(1000);
      ESP.restart();
    }
  }

  // Save the custom parameters to FS
  if (shouldSaveConfig) {
    strlcpy(mqtt_server, custom_mqtt_server.getValue(), sizeof(mqtt_server));
    
    JsonDocument json;
    json["mqtt_server"] = mqtt_server;
    
    File configFile = SPIFFS.open("/config.json", "w");
    if (configFile) {
      serializeJson(json, configFile);
      configFile.close();
    }
  }

  Serial.println("\nWiFi connected");
  showStatus("WiFi connected", WiFi.localIP().toString().c_str(), mqtt_server);
  syncClock();

  // 2. Setup MQTT
  Serial.print("MQTT Server: ");
  Serial.println(mqtt_server);
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
  client.setBufferSize(512); // Enough for the compact id/url command payload

  // 3. Test Audio for MAX98357A I2S amplifier
  showStatus("Sound test", "Cash ding...");
  playCashDing();

  Serial.println("💡 BLINKING LED TEST...");
  for (int i = 0; i < 3; i++) {
    ledWrite(false);
    delay(200);
    ledWrite(true);
    delay(200);
  }
  Serial.println("✅ LED TEST DONE");
}

// --------------------------------------------------------------------------------
// MQTT CALLBACK
// --------------------------------------------------------------------------------
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, mqtt_topic) != 0) {
    Serial.printf("Ignoring unexpected topic: %s\n", topic);
    return;
  }

  if (length == 0 || length > 512) {
    Serial.printf("Ignoring invalid payload length: %u\n", length);
    return;
  }

  Serial.print("Message arrived: ");
  Serial.write(payload, length);
  Serial.println();

  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) {
    Serial.print("JSON Failed: ");
    Serial.println(error.c_str());
    return;
  }

  const char* id = doc["id"];
  if (!id || strlen(id) == 0 || strlen(id) > maxTransactionIdLength) {
    Serial.println("Ignoring message with invalid id");
    return;
  }

  const char* audio_url = doc["url"];
  if (!audio_url || !isAllowedAudioUrl(audio_url)) {
    Serial.println("Ignoring invalid url");
    return;
  }

  if (!enqueueAudioCommand(id, audio_url)) {
    Serial.println("Audio queue full. Dropping transaction voice.");
    showStatus("Queue full", "Voice dropped");
    publishTransactionStatus(id, "rejected", "queue full");
    return;
  }

  Serial.printf("Transaction ID: %s\n", id);
  Serial.printf("Queued URL: %s\n", audio_url);
  showStatus("Voice queued", "Waiting playback...");
  publishTransactionStatus(id, "queued");
}

// --------------------------------------------------------------------------------
// AUDIO PLAYBACK
// --------------------------------------------------------------------------------
void audioStatusCallback(void *cbData, int code, const char *string) {
  (void) cbData;
  Serial.printf("Audio status %d: ", code);
  Serial.println(string);
}

bool isAllowedAudioUrl(const char* url) {
  return isAllowedMp3HttpsUrl(url, allowed_audio_host);
}

bool enqueueAudioCommand(const char* id, const char* url) {
  if (!id || !strlen(id) || strlen(id) > maxTransactionIdLength ||
      !url || strlen(url) >= maxAudioUrlLength) {
    return false;
  }

  if (audioQueueCount >= audioQueueDepth) {
    return false;
  }

  strlcpy(audioQueue[audioQueueTail].id, id, sizeof(audioQueue[audioQueueTail].id));
  strlcpy(audioQueue[audioQueueTail].url, url, sizeof(audioQueue[audioQueueTail].url));
  audioQueueTail = (audioQueueTail + 1) % audioQueueDepth;
  audioQueueCount++;
  return true;
}

bool dequeueAudioCommand(AudioCommand& command) {
  if (audioQueueCount == 0) {
    return false;
  }

  command = audioQueue[audioQueueHead];
  audioQueue[audioQueueHead] = {};
  audioQueueHead = (audioQueueHead + 1) % audioQueueDepth;
  audioQueueCount--;
  return true;
}

void publishTransactionStatus(const char* id, const char* status, const char* detail) {
  if (!client.connected() || !id || !status) return;

  JsonDocument doc;
  doc["id"] = id;
  doc["status"] = status;
  if (detail && strlen(detail)) doc["detail"] = detail;

  char payload[256];
  size_t length = serializeJson(doc, payload, sizeof(payload));
  if (length == 0 || length >= sizeof(payload)) {
    Serial.println("Failed to serialize transaction status");
    return;
  }

  if (!client.publish(mqtt_status_topic, (const uint8_t*)payload, length, false)) {
    Serial.printf("Failed to publish %s status for %s\n", status, id);
  }
}

void processQueuedAudio() {
  if (isPlaying) return;

  if (!dequeueAudioCommand(currentCommand)) {
    return;
  }

  Serial.printf("Playing queued transaction %s: %s\n", currentCommand.id, currentCommand.url);
  showStatus("Voice received", "Connecting stream...");
  publishTransactionStatus(currentCommand.id, "processing");
  if (!playAudio(currentCommand.url)) {
    publishTransactionStatus(currentCommand.id, "failed", "audio could not start");
    currentCommand = {};
    showStatus("Voice failed", "Waiting for MQTT");
  } else {
    publishTransactionStatus(currentCommand.id, "playing");
  }
}

bool openAudioStream(const char* url) {
  if (!isAllowedAudioUrl(url) || time(nullptr) < 1704067200) {
    Serial.println("Rejected stream: unsafe URL or clock not synchronized");
    return false;
  }
  audioStream = new (std::nothrow) AudioHttpsStream(
      letsEncryptRootCa, audioConnectTimeoutMs, audioRequestTimeoutMs,
      audioReadTimeoutMs, maxAudioBytes);
  if (!audioStream) return false;

  for (uint8_t attempt = 1; attempt <= audioStreamConnectAttempts; attempt++) {
    Serial.printf("Audio stream connection attempt %u/%u\n", attempt, audioStreamConnectAttempts);
    if (waitForWiFi(audioConnectTimeoutMs)) {
      if (audioStream->open(url)) return true;
      if (!audioStream->canRetry()) return false;
    }
    if (attempt < audioStreamConnectAttempts) {
      unsigned long startedAt = millis();
      while (millis() - startedAt < audioRetryDelayMs) {
        if (client.connected()) client.loop();
        delay(10);
      }
    }
  }
  return false;
}

bool waitForWiFi(unsigned long timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.println("WiFi disconnected; attempting reconnect...");
  WiFi.reconnect();
  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < timeoutMs) {
    delay(100);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool connectDefaultWiFi() {
  if (!useDefaultWiFi || default_wifi_ssid[0] == '\0') {
    return false;
  }

  Serial.print("Connecting to default WiFi: ");
  Serial.println(default_wifi_ssid);
  showStatus("Default WiFi", default_wifi_ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(default_wifi_ssid, default_wifi_password);

  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < defaultWifiTimeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Default WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
    showStatus("WiFi connected", WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.println("Default WiFi failed. Opening saved WiFi/setup portal flow.");
  showStatus("Default WiFi failed", "Opening portal");
  WiFi.disconnect(true);
  delay(100);
  return false;
}

bool playAudio(const char* url) {
  stopAudio(); // Stop any current playback

  if (!openAudioStream(url)) {
    Serial.println("ERROR: Failed to open HTTPS audio stream");
    showStatus("Audio error", "Stream failed");
    stopAudio();
    return false;
  }

  file = new (std::nothrow) AudioStreamBuffer(
      audioStream, streamBufferMemory, sizeof(streamBufferMemory));
  if (!file) {
    Serial.println("ERROR: Failed to allocate stream buffer");
    showStatus("Audio error", "No buffer memory");
    stopAudio();
    return false;
  }
  file->RegisterStatusCB(audioStatusCallback, NULL);
  if (!file->isOpen()) {
    Serial.println("ERROR: Failed to open audio stream");
    showStatus("Audio error", "Stream open failed");
    stopAudio();
    return false;
  }

  unsigned long bufferingStartedAt = millis();
  auto* buffer = static_cast<AudioStreamBuffer*>(file);
  if (!buffer->prime() || audioStream->hasFailed()) {
    Serial.println("ERROR: Initial stream buffering failed");
    stopAudio();
    return false;
  }
  Serial.printf("Stream primed: %u bytes in %lu ms\n",
                (unsigned)buffer->getFillLevel(), millis() - bufferingStartedAt);
  // Prime before the ding so the accepted TLS connection is read immediately.
  playCashDing();

  id3 = new (std::nothrow) AudioFileSourceID3(file);
  if (!id3) {
    Serial.println("ERROR: Failed to allocate ID3 wrapper");
    showStatus("Audio error", "No ID3 memory");
    stopAudio();
    return false;
  }
  id3->RegisterStatusCB(audioStatusCallback, NULL);

  if (!setupAudioOutput()) {
    stopAudio();
    return false;
  }

  waitForAudioOutputSettle();

  mp3 = new (std::nothrow) AudioGeneratorMP3(mp3Preallocate, sizeof(mp3Preallocate));
  if (!mp3) {
    Serial.println("ERROR: Failed to allocate MP3 decoder");
    showStatus("Audio error", "No MP3 memory");
    stopAudio();
    return false;
  }
  mp3->RegisterStatusCB(audioStatusCallback, NULL);
  
  if (mp3->begin(id3, out)) {
    Serial.println("MP3 streaming playback started (8 KB buffer)");
    showSpeakFace();
    isPlaying = true;
    startPlayTime = millis();
    return true;
  } else {
    Serial.println("ERROR: Failed to start MP3 playback (Check URL or Firewall)");
    showStatus("Audio error", "MP3 start failed");
    stopAudio();
    return false;
  }
}

bool syncClock() {
  Serial.println("Synchronizing clock for TLS validation...");
  showStatus("Clock sync", "Waiting for NTP...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  unsigned long startedAt = millis();
  while (time(nullptr) < 1704067200 && millis() - startedAt < clockSyncTimeoutMs) {
    delay(100);
  }

  if (time(nullptr) < 1704067200) {
    Serial.println("Clock synchronization timed out; HTTPS audio is disabled until time is valid");
    showStatus("Clock sync failed", "HTTPS unavailable");
    return false;
  }

  Serial.println("Clock synchronized");
  return true;
}

void stopAudio() {
  if (mp3) {
    if (mp3->isRunning()) mp3->stop();
    delete mp3; mp3 = NULL;
  }
  if (id3) {
    delete id3; id3 = NULL;
  }
  if (file) {
    delete file; file = NULL;
  }
  if (audioStream) {
    delete audioStream;
    audioStream = NULL;
  }
  if (out) {
    delete out; out = NULL;
  }
  isPlaying = false;
  ledWrite(true);
}

bool setupAudioOutput() {
  if (out) {
    delete out;
    out = NULL;
  }

  out = new (std::nothrow) AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S, 8, AudioOutputI2S::APLL_DISABLE);
  if (!out) {
    Serial.println("ERROR: Failed to allocate I2S output");
    showStatus("Audio error", "No I2S memory");
    return false;
  }
  out->SetPinout(i2sBclkPin, i2sLrcPin, i2sDoutPin);
  out->SetOutputModeMono(true);
  out->SetGain(voiceGain);
  Serial.println("I2S audio output ready");
  return true;
}

void playCashDing() {
  showStatus("Cash ding", "Playing...");

  AudioFileSourcePROGMEM *dingFile = new (std::nothrow) AudioFileSourcePROGMEM(cash_ding_mp3, cash_ding_mp3_len);
  if (!dingFile) {
    Serial.println("Failed to allocate cash ding source");
    showStatus("Cash ding", "No source memory");
    return;
  }

  AudioFileSourceID3 *dingId3 = new (std::nothrow) AudioFileSourceID3(dingFile);
  if (!dingId3) {
    Serial.println("Failed to allocate cash ding ID3 wrapper");
    showStatus("Cash ding", "No ID3 memory");
    delete dingFile;
    return;
  }

  AudioOutputI2S *dingOut = new (std::nothrow) AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S, 8, AudioOutputI2S::APLL_DISABLE);
  if (!dingOut) {
    Serial.println("Failed to allocate cash ding I2S output");
    showStatus("Cash ding", "No I2S memory");
    delete dingId3;
    delete dingFile;
    return;
  }
  dingOut->SetPinout(i2sBclkPin, i2sLrcPin, i2sDoutPin);
  dingOut->SetOutputModeMono(true);
  dingOut->SetGain(cashDingGain);
  Serial.println("I2S audio output ready");
  waitForAudioOutputSettle();

  AudioGeneratorMP3 *dingMp3 = new (std::nothrow) AudioGeneratorMP3(mp3Preallocate, sizeof(mp3Preallocate));
  if (!dingMp3) {
    Serial.println("Failed to allocate cash ding MP3 decoder");
    showStatus("Cash ding", "No MP3 memory");
    delete dingId3;
    delete dingFile;
    delete dingOut;
    return;
  }

  if (!dingMp3->begin(dingId3, dingOut)) {
    Serial.println("Failed to play cash ding");
    delete dingMp3;
    delete dingId3;
    delete dingFile;
    delete dingOut;
    return;
  }

  unsigned long startedAt = millis();
  while (dingMp3->isRunning() && millis() - startedAt < 3000) {
    if (!dingMp3->loop()) {
      dingMp3->stop();
      break;
    }
    delay(1);
  }

  if (dingMp3->isRunning()) {
    dingMp3->stop();
  }
  delete dingMp3;
  delete dingId3;
  delete dingFile;
  delete dingOut;
}

void waitForAudioOutputSettle() {
  unsigned long startedAt = millis();
  while (millis() - startedAt < i2sSettleMs) {
    delay(1);
  }
}

void clearResetFlag() {
  if (SPIFFS.exists(resetFlagPath)) {
    SPIFFS.remove(resetFlagPath);
    Serial.println("Boot successful. Reset flag cleared.");
  }
  resetFlagCleared = true;
}

void resetSavedConfig() {
  Serial.println("Clearing WiFi and MQTT settings...");
  showStatus("Reset settings", "Clearing WiFi/MQTT");
  stopAudio();
  client.disconnect();

  for (int i = 0; i < 20; i++) {
    ledToggle();
    delay(50);
  }
  ledWrite(true);

  WiFiManager wm;
  wm.resetSettings();
  if (SPIFFS.exists(resetFlagPath)) {
    SPIFFS.remove(resetFlagPath);
  }
  if (SPIFFS.exists("/config.json")) {
    SPIFFS.remove("/config.json");
  }

  Serial.println("Settings cleared. Restarting...");
  showStatus("Settings cleared", "Restarting...");
  delay(300);
  ESP.restart();
}

// --------------------------------------------------------------------------------
// MAIN LOOP
// --------------------------------------------------------------------------------
void loop() {
  if (!resetFlagCleared && millis() > doubleResetWindowMs) {
    clearResetFlag();
  }

  // Keep audio playback as the highest-priority loop work. MQTT reconnects and
  // OLED refreshes can block long enough to starve the I2S DMA buffer.
  if (!isPlaying && !client.connected()) {
    reconnect();
  }
  if (client.connected()) {
    client.loop();
  }

  processQueuedAudio();

  // Handle Audio Playback
  if (isPlaying && mp3) {
    if (mp3->isRunning()) {
      // The ID3 wrapper in ESP8266Audio 1.9.9 does not forward loop().
      if (file) file->loop();
      if (!mp3->loop()) {
        const bool complete = audioStream && audioStream->transferComplete();
        if (audioStream && file) {
          Serial.printf("Stream bytes: received %u/%u, consumed %u\n",
                        (unsigned)audioStream->getPos(), (unsigned)audioStream->getSize(),
                        (unsigned)file->getPos());
        }
        mp3->stop();
        Serial.print(complete ? "Playback finished. Duration: " : "Stream failed. Duration: ");
        Serial.print(millis() - startPlayTime);
        Serial.println(" ms");
        delay(audioFinishedDisplayDelayMs);
        stopAudio();
        publishTransactionStatus(currentCommand.id, complete ? "completed" : "failed",
                                 complete ? NULL : "stream incomplete or decoder stopped early");
        currentCommand = {};
        showStatus(complete ? "Voice finished" : "Stream failed", "Waiting for MQTT");
      } else {
        // Blink LED while playing 
        if (millis() - lastBlinkTime > 100) { // Blink every 100ms
          lastBlinkTime = millis();
          ledToggle();
        }
      }
    } else {
      Serial.println("Playback stopped unexpectedly");
      publishTransactionStatus(currentCommand.id, "failed", "decoder stopped unexpectedly");
      stopAudio();
      currentCommand = {};
      showStatus("Audio error", "Playback stopped");
    }
  }
}

void reconnect() {
  static unsigned long lastReconnectAttempt = 0;

  if (lastReconnectAttempt != 0 && millis() - lastReconnectAttempt < mqttReconnectIntervalMs) {
    return;
  }
  lastReconnectAttempt = millis();

  Serial.print("Attempting MQTT connection to ");
  Serial.print(mqtt_server);
  Serial.print("... ");
  showStatus("MQTT connecting", mqtt_server);
  char clientId[32];
  uint64_t chipId = ESP.getEfuseMac();
  snprintf(clientId, sizeof(clientId), "voice-%04X%08X",
           (uint16_t)(chipId >> 32), (uint32_t)chipId);
  
  // A stable client ID plus cleanSession=false lets a correctly configured
  // broker retain QoS 1 commands while this device reconnects.
  if (client.connect(clientId, NULL, NULL, NULL, 0, false, NULL, false)) {
    Serial.println("connected");
    showStatus("MQTT connected", mqtt_server, "Waiting voice...");
    if (!client.subscribe(mqtt_topic, 1)) {
      Serial.println("Failed to subscribe to command topic at QoS 1");
    }
  } else {
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" will retry");
    showStatus("MQTT failed", mqtt_server, "Retrying...");
  }
}
