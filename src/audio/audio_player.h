#pragma once

#include <Arduino.h>
#include <functional>

typedef std::function<void(bool success)> AudioCompletionCallback;

class AudioPlayer {
public:
    static bool init();
    static bool playCashDing();
    static bool startVoiceStream(const char* url);
    static void loop();
    static void stop();
    static bool isPlaying();
    static void setCompletionCallback(AudioCompletionCallback cb);

    static size_t downloadToPsram(const char* url);
    static void setPsramAudioSize(size_t sz) { psramAudioSize = sz; }

private:
    static bool isAudioPlaying;
    static unsigned long playStartTime;
    static AudioCompletionCallback onComplete;

    static uint8_t* psramAudioBuf;
    static size_t psramAudioSize;
};
