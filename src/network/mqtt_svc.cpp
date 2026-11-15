#include "mqtt_svc.h"
#include "config.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

static WiFiClient espTcpClient;
static PubSubClient mqttClient(espTcpClient);

char MqttService::broker[40] = "";
int MqttService::brokerPort = DEFAULT_MQTT_PORT;
VoiceCommandCallback MqttService::commandHandler = nullptr;

bool MqttService::init(const char* brokerIp, int port, VoiceCommandCallback onCommand) {
    if (brokerIp) strlcpy(broker, brokerIp, sizeof(broker));
    brokerPort = port;
    commandHandler = onCommand;

    mqttClient.setServer(broker, brokerPort);
    mqttClient.setCallback(handleIncoming);
    mqttClient.setBufferSize(512);

    Serial.printf("MqttService: Configured for broker %s:%d\n", broker, brokerPort);
    return true;
}

bool MqttService::isConnected() {
    return mqttClient.connected();
}

void MqttService::handleIncoming(char* topic, byte* payload, unsigned int length) {
    if (strcmp(topic, MQTT_TOPIC_COMMANDS) != 0) return;
    if (length == 0 || length > 512) return;

    Serial.print("MqttService: Command payload received: ");
    Serial.write(payload, length);
    Serial.println();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) {
        Serial.printf("MqttService: JSON parse error: %s\n", err.c_str());
        return;
    }

    const char* id = doc["id"];
    const char* url = doc["url"];

    if (!id || strlen(id) == 0 || !url || strlen(url) == 0) {
        Serial.println("MqttService: Missing 'id' or 'url' in command");
        return;
    }

    VoiceCommand cmd;
    strlcpy(cmd.id, id, sizeof(cmd.id));
    strlcpy(cmd.url, url, sizeof(cmd.url));

    publishStatus(cmd.id, "queued");

    if (commandHandler) {
        commandHandler(cmd);
    }
}

bool MqttService::publishStatus(const char* id, const char* status, const char* detail) {
    if (!mqttClient.connected() || !id || !status) return false;

    JsonDocument doc;
    doc["id"] = id;
    doc["status"] = status;
    if (detail && detail[0]) doc["detail"] = detail;

    char buf[256];
    size_t len = serializeJson(doc, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return false;

    return mqttClient.publish(MQTT_TOPIC_STATUS, (const uint8_t*)buf, len, false);
}

void MqttService::reconnect() {
    static unsigned long lastAttempt = 0;
    if (lastAttempt != 0 && millis() - lastAttempt < MQTT_RECONNECT_INTERVAL_MS) {
        return;
    }
    lastAttempt = millis();

    if (!WiFi.isConnected()) return;

    Serial.printf("MqttService: Connecting to MQTT broker %s... ", broker);

    char clientId[32];
    uint64_t chipId = ESP.getEfuseMac();
    snprintf(clientId, sizeof(clientId), "voice-%04X%08lX",
             (uint16_t)(chipId >> 32), (unsigned long)(uint32_t)chipId);

    if (mqttClient.connect(clientId, nullptr, nullptr, nullptr, 0, false, nullptr, false)) {
        Serial.println("Connected!");
        if (mqttClient.subscribe(MQTT_TOPIC_COMMANDS, 1)) {
            Serial.printf("MqttService: Subscribed to %s (QoS 1)\n", MQTT_TOPIC_COMMANDS);
        } else {
            Serial.println("MqttService: Subscription failed");
        }
    } else {
        Serial.printf("Failed, rc=%d. Will retry.\n", mqttClient.state());
    }
}

void MqttService::loop() {
    if (!mqttClient.connected()) {
        reconnect();
    } else {
        mqttClient.loop();
    }
}
