#include "config_store.h"
#include "config.h"
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

bool ConfigStore::doubleResetFlag = false;
bool ConfigStore::resetFlagCleared = false;

bool ConfigStore::init() {
    if (!SPIFFS.begin(true)) {
        Serial.println("ConfigStore: Failed to mount SPIFFS");
        return false;
    }

    if (SPIFFS.exists(RESET_FLAG_PATH)) {
        Serial.println("ConfigStore: Double reset detected! Will clear settings.");
        doubleResetFlag = true;
    } else {
        File flag = SPIFFS.open(RESET_FLAG_PATH, "w");
        if (flag) flag.close();
        Serial.println("ConfigStore: Reset flag set (press RST within 5s to clear settings).");
    }
    return true;
}

bool ConfigStore::isDoubleResetDetected() {
    return doubleResetFlag;
}

void ConfigStore::clearResetFlag() {
    if (resetFlagCleared) return;
    if (SPIFFS.exists(RESET_FLAG_PATH)) {
        SPIFFS.remove(RESET_FLAG_PATH);
        Serial.println("ConfigStore: Boot successful. Reset flag cleared.");
    }
    resetFlagCleared = true;
}

bool ConfigStore::loadMqttServer(char* outBuffer, size_t maxLen) {
    if (!outBuffer || maxLen == 0) return false;
    if (!SPIFFS.exists(CONFIG_FILE_PATH)) return false;

    File f = SPIFFS.open(CONFIG_FILE_PATH, "r");
    if (!f) return false;

    size_t size = f.size();
    std::unique_ptr<char[]> buf(new (std::nothrow) char[size + 1]);
    if (!buf) {
        f.close();
        return false;
    }

    f.readBytes(buf.get(), size);
    buf[size] = '\0';
    f.close();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, buf.get());
    if (!err && doc["mqtt_server"].is<const char*>()) {
        strlcpy(outBuffer, doc["mqtt_server"], maxLen);
        Serial.printf("ConfigStore: Loaded MQTT server from SPIFFS: %s\n", outBuffer);
        return true;
    }
    return false;
}

bool ConfigStore::saveMqttServer(const char* server) {
    if (!server || !server[0]) return false;

    JsonDocument doc;
    doc["mqtt_server"] = server;

    File f = SPIFFS.open(CONFIG_FILE_PATH, "w");
    if (!f) return false;

    serializeJson(doc, f);
    f.close();
    Serial.printf("ConfigStore: Saved MQTT server to SPIFFS: %s\n", server);
    return true;
}

void ConfigStore::resetAllSettings() {
    Serial.println("ConfigStore: Resetting all settings...");
    if (SPIFFS.exists(RESET_FLAG_PATH)) SPIFFS.remove(RESET_FLAG_PATH);
    if (SPIFFS.exists(CONFIG_FILE_PATH)) SPIFFS.remove(CONFIG_FILE_PATH);
}
