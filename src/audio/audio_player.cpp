#include "audio_player.h"
#include "i2s_driver.h"
#include "config.h"
#include "cash_ding_mp3.h"
#include "letsencrypt_root_ca.h"
#include "AudioFileSourcePROGMEM.h"
#include "AudioFileSourceID3.h"
#include "AudioGeneratorMP3.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

alignas(8) static uint8_t mp3Prealloc[AudioGeneratorMP3::preAllocSize()];

static AudioGeneratorMP3* mp3Decoder = nullptr;
static AudioFileSource* audioSource  = nullptr;
static AudioFileSourceID3* id3Wrapper = nullptr;

bool AudioPlayer::isAudioPlaying = false;
unsigned long AudioPlayer::playStartTime = 0;
AudioCompletionCallback AudioPlayer::onComplete = nullptr;
uint8_t* AudioPlayer::psramAudioBuf = nullptr;
size_t AudioPlayer::psramAudioSize = 0;

static char pendingUrl[256] = "";
static volatile bool dlRunning = false;
static volatile bool dlComplete = false;
static volatile bool dlSuccess = false;

static void audioStatusCallback(void *cbData, int code, const char *string) {
    (void) cbData;
    Serial.printf("AudioPlayer Status [%d]: %s\n", code, string);
}

static void downloadWorkerTask(void* param) {
    dlRunning = true;
    dlComplete = false;
    dlSuccess = false;

    size_t size = AudioPlayer::downloadToPsram(pendingUrl);
    if (size > 0) {
        AudioPlayer::setPsramAudioSize(size);
        dlSuccess = true;
    } else {
        dlSuccess = false;
    }

    dlComplete = true;
    dlRunning = false;
    vTaskDelete(NULL);
}

bool AudioPlayer::init() {
    if (!psramAudioBuf) {
        if (psramFound()) {
            psramAudioBuf = (uint8_t*)ps_malloc(PSRAM_AUDIO_BUFFER_SIZE);
            if (psramAudioBuf) {
                Serial.printf("AudioPlayer: Allocated %u KB buffer in 8MB PSRAM (0x%p)\n",
                              (unsigned)(PSRAM_AUDIO_BUFFER_SIZE / 1024), psramAudioBuf);
            }
        }
        if (!psramAudioBuf) {
            psramAudioBuf = (uint8_t*)malloc(32 * 1024);
            Serial.println("AudioPlayer: Warning: Fallback to internal SRAM (32KB)");
        }
    }
    return I2SDriver::init();
}

void AudioPlayer::setCompletionCallback(AudioCompletionCallback cb) {
    onComplete = cb;
}

bool AudioPlayer::isPlaying() {
    return isAudioPlaying;
}

void AudioPlayer::stop() {
    if (mp3Decoder) {
        if (mp3Decoder->isRunning()) mp3Decoder->stop();
        delete mp3Decoder;
        mp3Decoder = nullptr;
    }
    if (id3Wrapper) {
        delete id3Wrapper;
        id3Wrapper = nullptr;
    }
    if (audioSource) {
        delete audioSource;
        audioSource = nullptr;
    }
    isAudioPlaying = false;
}

bool AudioPlayer::playCashDing() {
    stop();

    AudioOutputI2S* out = I2SDriver::getOutput();
    if (!out) return false;

    I2SDriver::setGain(CASH_DING_GAIN);

    AudioFileSourcePROGMEM* dingSrc = new (std::nothrow) AudioFileSourcePROGMEM(cash_ding_mp3, cash_ding_mp3_len);
    if (!dingSrc) return false;

    id3Wrapper = new (std::nothrow) AudioFileSourceID3(dingSrc);
    if (!id3Wrapper) {
        delete dingSrc;
        return false;
    }

    mp3Decoder = new (std::nothrow) AudioGeneratorMP3(mp3Prealloc, sizeof(mp3Prealloc));
    if (!mp3Decoder) {
        delete id3Wrapper;
        delete dingSrc;
        return false;
    }

    if (!mp3Decoder->begin(id3Wrapper, out)) {
        Serial.println("AudioPlayer: Failed to begin cash ding playback");
        delete mp3Decoder; mp3Decoder = nullptr;
        delete id3Wrapper; id3Wrapper = nullptr;
        delete dingSrc;
        return false;
    }

    unsigned long startDing = millis();
    while (mp3Decoder->isRunning() && millis() - startDing < 3000) {
        if (!mp3Decoder->loop()) {
            mp3Decoder->stop();
            break;
        }
        delay(1);
    }

    if (mp3Decoder->isRunning()) mp3Decoder->stop();
    delete mp3Decoder; mp3Decoder = nullptr;
    delete id3Wrapper; id3Wrapper = nullptr;
    delete dingSrc;

    I2SDriver::setGain(VOICE_GAIN);
    return true;
}

size_t AudioPlayer::downloadToPsram(const char* url) {
    if (!url || url[0] == '\0' || !psramAudioBuf) return 0;

    Serial.printf("AudioPlayer [Core %d]: Pre-caching voice MP3: %s\n", xPortGetCoreID(), url);
    unsigned long startDownload = millis();

    bool isHttps = (strncmp(url, "https://", 8) == 0);

    // Allocate HTTP clients on HEAP to prevent FreeRTOS stack overflow
    HTTPClient* http = new (std::nothrow) HTTPClient();
    if (!http) return 0;

    WiFiClient* client = nullptr;
    WiFiClientSecure* secureClient = nullptr;

    if (isHttps) {
        secureClient = new (std::nothrow) WiFiClientSecure();
        if (!secureClient) {
            delete http;
            return 0;
        }
        // Skips 1.5s certificate chain crypto calculations for known endpoint
        secureClient->setInsecure();
        secureClient->setTimeout(AUDIO_CONNECT_TIMEOUT_MS);
        http->begin(*secureClient, url);
        client = secureClient;
    } else {
        WiFiClient* plainClient = new (std::nothrow) WiFiClient();
        if (!plainClient) {
            delete http;
            return 0;
        }
        plainClient->setTimeout(AUDIO_CONNECT_TIMEOUT_MS);
        http->begin(*plainClient, url);
        client = plainClient;
    }

    http->setConnectTimeout(AUDIO_CONNECT_TIMEOUT_MS);
    http->setTimeout(AUDIO_REQUEST_TIMEOUT_MS);
    http->addHeader("Accept-Encoding", "identity");

    int httpCode = http->GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("AudioPlayer: HTTP GET failed, code: %d\n", httpCode);
        http->end();
        delete http;
        if (secureClient) delete secureClient;
        else if (client) delete client;
        return 0;
    }

    int contentLength = http->getSize();
    WiFiClient* stream = http->getStreamPtr();
    size_t bytesReceived = 0;
    unsigned long lastDataTime = millis();

    while (http->connected() && (contentLength > 0 ? bytesReceived < (size_t)contentLength : true)) {
        size_t availableBytes = stream->available();
        if (availableBytes > 0) {
            size_t toRead = availableBytes;
            if (bytesReceived + toRead > PSRAM_AUDIO_BUFFER_SIZE) {
                toRead = PSRAM_AUDIO_BUFFER_SIZE - bytesReceived;
            }
            if (toRead == 0) break;

            int bytesRead = stream->readBytes(psramAudioBuf + bytesReceived, toRead);
            if (bytesRead > 0) {
                bytesReceived += bytesRead;
                lastDataTime = millis();
            }
        } else {
            if (millis() - lastDataTime > AUDIO_READ_TIMEOUT_MS) {
                Serial.println("AudioPlayer: Download stalled / timed out");
                break;
            }
            delay(1);
        }

        if (contentLength > 0 && bytesReceived >= (size_t)contentLength) break;
    }

    http->end();
    delete http;
    if (secureClient) delete secureClient;
    else if (client) delete client;

    Serial.printf("AudioPlayer [Core %d]: Cached %u bytes to PSRAM in %lu ms\n",
                  xPortGetCoreID(), (unsigned)bytesReceived, millis() - startDownload);
    return bytesReceived;
}

bool AudioPlayer::startVoiceStream(const char* url) {
    stop();

    if (!url || url[0] == '\0') return false;
    strlcpy(pendingUrl, url, sizeof(pendingUrl));

    dlComplete = false;
    dlSuccess = false;

    // 1. Launch PARALLEL background download on Core 0
    TaskHandle_t dlTask = nullptr;
    xTaskCreatePinnedToCore(downloadWorkerTask, "dl_voice", 8192, NULL, 5, &dlTask, 0);

    // 2. Play Cash Ding alert sound on Core 1 (<15ms to first sound!)
    playCashDing();

    // 3. Wait for background download (already done or finishing up)
    unsigned long waitStart = millis();
    while (!dlComplete && (millis() - waitStart < 4000)) {
        delay(5);
    }

    if (!dlSuccess || psramAudioSize == 0) {
        Serial.println("AudioPlayer: Failed to cache voice MP3 in background");
        return false;
    }

    // 4. Play voice directly from fast PSRAM memory with ZERO delay!
    AudioOutputI2S* out = I2SDriver::getOutput();
    if (!out) return false;

    I2SDriver::setGain(VOICE_GAIN);

    AudioFileSourcePROGMEM* ramSrc = new (std::nothrow) AudioFileSourcePROGMEM(psramAudioBuf, psramAudioSize);
    if (!ramSrc) return false;

    id3Wrapper = new (std::nothrow) AudioFileSourceID3(ramSrc);
    if (!id3Wrapper) {
        delete ramSrc;
        return false;
    }
    id3Wrapper->RegisterStatusCB(audioStatusCallback, nullptr);

    mp3Decoder = new (std::nothrow) AudioGeneratorMP3(mp3Prealloc, sizeof(mp3Prealloc));
    if (!mp3Decoder) {
        delete id3Wrapper;
        delete ramSrc;
        return false;
    }
    mp3Decoder->RegisterStatusCB(audioStatusCallback, nullptr);

    if (mp3Decoder->begin(id3Wrapper, out)) {
        audioSource = ramSrc;
        isAudioPlaying = true;
        playStartTime = millis();
        Serial.printf("AudioPlayer: Instant voice playback started from PSRAM (%u bytes)\n", (unsigned)psramAudioSize);
        return true;
    }

    Serial.println("AudioPlayer: Failed to begin PSRAM voice decode");
    delete mp3Decoder; mp3Decoder = nullptr;
    delete id3Wrapper; id3Wrapper = nullptr;
    delete ramSrc;
    return false;
}

void AudioPlayer::loop() {
    if (!isAudioPlaying || !mp3Decoder) return;

    if (mp3Decoder->isRunning()) {
        if (!mp3Decoder->loop()) {
            mp3Decoder->stop();
            Serial.printf("AudioPlayer: Voice playback finished successfully (%lu ms)\n",
                          millis() - playStartTime);
            stop();
            if (onComplete) onComplete(true);
        }
    } else {
        Serial.println("AudioPlayer: Decoder stopped early");
        stop();
        if (onComplete) onComplete(false);
    }
}
