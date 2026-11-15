#pragma once

#include <Arduino.h>

class WiFiService {
public:
    static bool connect(char* mqttServerBuffer, size_t mqttServerLen);
    static bool isConnected();
    static void loop();
};
