#pragma once

#include <Arduino.h>
#include <functional>

struct VoiceCommand {
    char id[65];
    char url[256];
};

typedef std::function<void(const VoiceCommand& cmd)> VoiceCommandCallback;

class MqttService {
public:
    static bool init(const char* brokerIp, int port, VoiceCommandCallback onCommand);
    static void loop();
    static bool publishStatus(const char* id, const char* status, const char* detail = nullptr);
    static bool isConnected();

private:
    static void reconnect();
    static void handleIncoming(char* topic, byte* payload, unsigned int length);

    static char broker[40];
    static int brokerPort;
    static VoiceCommandCallback commandHandler;
};
