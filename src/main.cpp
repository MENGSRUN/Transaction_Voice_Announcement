#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <FS.h>
#include <SPIFFS.h>
#include <memory>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>
#include "AudioFileSource.h"
#include "AudioFileSourceID3.h"
#include "AudioFileSourcePROGMEM.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include "cash_ding_mp3.h"
#include <WiFiManager.h> // Install "WiFiManager" by tzapu via Library Manager

const char* allowed_audio_host = "weknowyouknow.dev"; // Media hosting must be on this domain and contain .mp3 in path to be played. This is a security measure to prevent abuse.
const unsigned long mqttReconnectIntervalMs = 5000;
const char* default_wifi_ssid = "";      // Optional: put a lab-only default WiFi SSID here
const char* default_wifi_password = "";  // Optional: put a lab-only default WiFi password here
const unsigned long defaultWifiTimeoutMs = 15000;
const int ledPin = -1; // GPIO22 is used for OLED SCL. Set another GPIO here if you wire an LED.
const int oledSdaPin = 23;
const int oledSclPin = 22;
const uint8_t oledAddress = 0x3C;
const int screenWidth = 128;
const int screenHeight = 64;
const int i2sBclkPin = 26;
const int i2sLrcPin = 25;
const int i2sDoutPin = 27;
const size_t maxAudioBytes = 64 * 1024; // About 16 seconds at the Voice Service 32 kbps MP3 profile.
const size_t maxAudioUrlLength = 256;
const uint8_t audioQueueDepth = 4;
const float voiceGain = 1;
const float cashDingGain = 0.7;
const unsigned long i2sSettleMs = 80;
const unsigned long doubleResetWindowMs = 5000;
const char* resetFlagPath = "/reset_flag";

class AudioFileSourceMemory : public AudioFileSource {
public:
  AudioFileSourceMemory(const uint8_t *data, uint32_t len) {
    open(data, len);
  }

  bool open(const uint8_t *data, uint32_t len) {
    if (!data || len == 0) return false;
    audioData = data;
    audioLen = len;
    pos = 0;
    opened = true;
    return true;
  }

  uint32_t read(void *data, uint32_t len) override {
    if (!opened || !data || pos >= audioLen) return 0;

    uint32_t remaining = audioLen - pos;
    uint32_t toRead = len < remaining ? len : remaining;
    memcpy(data, audioData + pos, toRead);
    pos += toRead;
    return toRead;
  }

  uint32_t readNonBlock(void *data, uint32_t len) override {
    return read(data, len);
  }

  bool seek(int32_t offset, int dir) override {
    if (!opened) return false;

    int32_t newPos = 0;
    if (dir == SEEK_SET) {
      newPos = offset;
    } else if (dir == SEEK_CUR) {
      newPos = pos + offset;
    } else if (dir == SEEK_END) {
      newPos = audioLen + offset;
    } else {
      return false;
    }

    if (newPos < 0 || (uint32_t)newPos > audioLen) return false;
    pos = newPos;
    return true;
  }

  bool close() override {
    opened = false;
    audioData = NULL;
    audioLen = 0;
    pos = 0;
    return true;
  }

  bool isOpen() override {
    return opened;
  }

  uint32_t getSize() override {
    return audioLen;
  }

  uint32_t getPos() override {
    return pos;
  }

private:
  const uint8_t *audioData = NULL;
  uint32_t audioLen = 0;
  uint32_t pos = 0;
  bool opened = false;
};

// --------------------------------------------------------------------------------
// CONFIGURATION
// --------------------------------------------------------------------------------
// WiFi credentials are now managed by WiFiManager (Web Interface)
char mqtt_server[40] = "192.168.18.120"; // Default IP, will be overwritten by config
const int mqtt_port = 1883;
const char* mqtt_topic = "store/transaction-voice/commands";

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
uint8_t *downloadedAudio = NULL;
size_t downloadedAudioSize = 0;
char audioQueue[audioQueueDepth][maxAudioUrlLength];
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
bool enqueueAudioUrl(const char* url);
bool dequeueAudioUrl(char* url, size_t urlSize);
void processQueuedAudio();
bool ensureAudioBuffer();
bool downloadAudio(const char* url);
bool connectDefaultWiFi();
void playAudio(const char* url);
void stopAudio();
void setupAudioOutput();
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
  if (!id || strlen(id) == 0) {
    Serial.println("Ignoring message with invalid id");
    return;
  }

  const char* audio_url = doc["url"];
  if (!audio_url || !isAllowedAudioUrl(audio_url)) {
    Serial.println("Ignoring invalid url");
    return;
  }

  if (!enqueueAudioUrl(audio_url)) {
    Serial.println("Audio queue full. Dropping transaction voice.");
    showStatus("Queue full", "Voice dropped");
    return;
  }

  Serial.printf("Transaction ID: %s\n", id);
  Serial.printf("Queued URL: %s\n", audio_url);
  showStatus("Voice queued", "Waiting playback...");
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
  if (!url) return false;

  const char* hostStart = NULL;
  if (strncmp(url, "https://", 8) == 0) {
    hostStart = url + 8;
  } else {
    return false;
  }

  const char* pathStart = strchr(hostStart, '/');
  if (!pathStart) return false;

  size_t hostLen = pathStart - hostStart;
  if (hostLen != strlen(allowed_audio_host) || strncmp(hostStart, allowed_audio_host, hostLen) != 0) {
    return false;
  }

  const char* pathEnd = strpbrk(pathStart, "?#");
  size_t pathLen = pathEnd ? (size_t)(pathEnd - pathStart) : strlen(pathStart);
  if (pathLen < 4) return false;

  return strncmp(pathStart + pathLen - 4, ".mp3", 4) == 0;
}

bool enqueueAudioUrl(const char* url) {
  if (!url || strlen(url) >= maxAudioUrlLength) {
    return false;
  }

  if (audioQueueCount >= audioQueueDepth) {
    return false;
  }

  strlcpy(audioQueue[audioQueueTail], url, maxAudioUrlLength);
  audioQueueTail = (audioQueueTail + 1) % audioQueueDepth;
  audioQueueCount++;
  return true;
}

bool dequeueAudioUrl(char* url, size_t urlSize) {
  if (!url || urlSize == 0 || audioQueueCount == 0) {
    return false;
  }

  strlcpy(url, audioQueue[audioQueueHead], urlSize);
  audioQueue[audioQueueHead][0] = '\0';
  audioQueueHead = (audioQueueHead + 1) % audioQueueDepth;
  audioQueueCount--;
  return true;
}

void processQueuedAudio() {
  if (isPlaying) return;

  char nextUrl[maxAudioUrlLength];
  if (!dequeueAudioUrl(nextUrl, sizeof(nextUrl))) {
    return;
  }

  Serial.printf("Playing queued URL: %s\n", nextUrl);
  showStatus("Voice received", "Downloading audio...");
  playAudio(nextUrl);
}

bool ensureAudioBuffer() {
  if (downloadedAudio) {
    return true;
  }

  downloadedAudio = (uint8_t*)malloc(maxAudioBytes);
  if (!downloadedAudio) {
    Serial.printf("Failed to allocate reusable audio buffer: %u bytes\n", (unsigned)maxAudioBytes);
    showStatus("Audio buffer", "No memory");
    return false;
  }

  Serial.printf("Reusable audio buffer allocated: %u bytes\n", (unsigned)maxAudioBytes);
  return true;
}

bool downloadAudio(const char* url) {
  WiFiClientSecure secureClient;
  HTTPClient http;
  downloadedAudioSize = 0;

  if (!isAllowedAudioUrl(url)) {
    Serial.println("Rejected unsafe audio URL before download");
    showStatus("Audio download", "Rejected URL");
    return false;
  }

  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

  secureClient.setInsecure();
  bool begun = http.begin(secureClient, url);

  if (!begun) {
    Serial.println("HTTP begin failed");
    showStatus("Audio download", "HTTP begin failed");
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("HTTP GET failed, code: %d\n", code);
    showStatus("Audio download", "HTTP GET failed");
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  if (contentLength <= 0 || contentLength > (int)maxAudioBytes) {
    Serial.printf("Invalid audio size: %d\n", contentLength);
    showStatus("Audio download", "Invalid size");
    http.end();
    return false;
  }

  if (!ensureAudioBuffer()) {
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  size_t totalRead = 0;
  unsigned long lastProgress = millis();

  while (totalRead < (size_t)contentLength) {
    int available = stream->available();
    if (available > 0) {
      size_t remaining = contentLength - totalRead;
      size_t toRead = available < (int)remaining ? available : remaining;
      int bytesRead = stream->read(downloadedAudio + totalRead, toRead);
      if (bytesRead > 0) {
        totalRead += bytesRead;
        lastProgress = millis();
      }
    } else if (!http.connected()) {
      break;
    } else {
      client.loop();
      delay(1);
    }

    if (millis() - lastProgress > 5000) {
      Serial.println("Audio download timed out");
      showStatus("Audio download", "Timed out");
      break;
    }
  }

  http.end();

  if (totalRead != (size_t)contentLength) {
    Serial.printf("Audio download incomplete: %u/%d bytes\n", (unsigned)totalRead, contentLength);
    showStatus("Audio download", "Incomplete");
    downloadedAudioSize = 0;
    return false;
  }

  downloadedAudioSize = totalRead;
  Serial.printf("Audio downloaded: %u bytes\n", (unsigned)downloadedAudioSize);
  showStatus("Audio ready", "Starting voice...");
  return true;
}

bool connectDefaultWiFi() {
  if (!default_wifi_ssid || strlen(default_wifi_ssid) == 0) {
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

void playAudio(const char* url) {
  stopAudio(); // Stop any current playback

  if (!downloadAudio(url)) {
    Serial.println("ERROR: Failed to download audio URL");
    showStatus("Audio error", "Download failed");
    stopAudio();
    return;
  }

  playCashDing();

  file = new AudioFileSourceMemory(downloadedAudio, downloadedAudioSize);
  file->RegisterStatusCB(audioStatusCallback, NULL);
  if (!file->isOpen()) {
    Serial.println("ERROR: Failed to open audio memory");
    showStatus("Audio error", "Memory open failed");
    stopAudio();
    return;
  }

  id3 = new AudioFileSourceID3(file);
  id3->RegisterStatusCB(audioStatusCallback, NULL);

  setupAudioOutput();

  mp3 = new AudioGeneratorMP3(mp3Preallocate, sizeof(mp3Preallocate));
  mp3->RegisterStatusCB(audioStatusCallback, NULL);
  
  if (mp3->begin(id3, out)) {
    Serial.println("MP3 playback started");
    showSpeakFace();
    isPlaying = true;
    startPlayTime = millis();
  } else {
    Serial.println("ERROR: Failed to start MP3 playback (Check URL or Firewall)");
    showStatus("Audio error", "MP3 start failed");
    stopAudio();
  }
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
  downloadedAudioSize = 0;
  if (out) {
    delete out; out = NULL;
  }
  isPlaying = false;
  ledWrite(true);
}

void setupAudioOutput() {
  if (out) {
    delete out;
    out = NULL;
  }

  out = new AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S, 8, AudioOutputI2S::APLL_DISABLE);
  out->SetPinout(i2sBclkPin, i2sLrcPin, i2sDoutPin);
  out->SetOutputModeMono(true);
  out->SetGain(voiceGain);
  Serial.println("I2S audio output ready");
}

void playCashDing() {
  showStatus("Cash ding", "Playing...");

  AudioFileSourcePROGMEM *dingFile = new AudioFileSourcePROGMEM(cash_ding_mp3, cash_ding_mp3_len);
  AudioFileSourceID3 *dingId3 = new AudioFileSourceID3(dingFile);
  AudioOutputI2S *dingOut = new AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S, 8, AudioOutputI2S::APLL_DISABLE);
  dingOut->SetPinout(i2sBclkPin, i2sLrcPin, i2sDoutPin);
  dingOut->SetOutputModeMono(true);
  dingOut->SetGain(cashDingGain);
  Serial.println("I2S audio output ready");
  waitForAudioOutputSettle();

  AudioGeneratorMP3 *dingMp3 = new AudioGeneratorMP3(mp3Preallocate, sizeof(mp3Preallocate));
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
    client.loop();
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
    client.loop();
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

  // Handle MQTT Connection
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  processQueuedAudio();

  // Handle Audio Playback
  if (isPlaying && mp3) {
    if (mp3->isRunning()) {
      if (!mp3->loop()) {
        mp3->stop();
        Serial.print("Playback finished. Duration: ");
        Serial.print(millis() - startPlayTime);
        Serial.println(" ms");
        showStatus("Voice finished", "Waiting for MQTT");
        stopAudio();
      } else {
        // Blink LED while playing 
        if (millis() - lastBlinkTime > 100) { // Blink every 100ms
          lastBlinkTime = millis();
          ledToggle();
          showSpeakFace();
        }
      }
    } else {
      isPlaying = false;
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
  String clientId = "ESP32Client-";
  clientId += String(random(0xffff), HEX);
  
  if (client.connect(clientId.c_str())) {
    Serial.println("connected");
    showStatus("MQTT connected", mqtt_server, "Waiting voice...");
    client.subscribe(mqtt_topic);
  } else {
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" will retry");
    showStatus("MQTT failed", mqtt_server, "Retrying...");
  }
}
