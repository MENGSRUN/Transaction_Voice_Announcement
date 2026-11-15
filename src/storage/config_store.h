#pragma once

#include <Arduino.h>

class ConfigStore {
public:
    static bool init();
    static bool isDoubleResetDetected();
    static void clearResetFlag();
    static bool loadMqttServer(char* outBuffer, size_t maxLen);
    static bool saveMqttServer(const char* server);
    static void resetAllSettings();

private:
    static bool doubleResetFlag;
    static bool resetFlagCleared;
};
