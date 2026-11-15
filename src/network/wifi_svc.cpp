#include "wifi_svc.h"
#include "config.h"
#include "wifi_credentials.h"
#include "storage/config_store.h"
#include <WiFi.h>
#include <WiFiManager.h>

static bool shouldSaveConfig = false;

static void saveConfigCallback() {
    Serial.println("WiFiService: User submitted custom settings via portal");
    shouldSaveConfig = true;
}

bool WiFiService::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}

bool WiFiService::connect(char* mqttServerBuffer, size_t mqttServerLen) {
    // 1. Try hardcoded fallback if provided
    if (default_wifi_ssid && default_wifi_ssid[0] != '\0') {
        Serial.printf("WiFiService: Trying fallback WiFi: %s\n", default_wifi_ssid);
        WiFi.mode(WIFI_STA);
        WiFi.begin(default_wifi_ssid, default_wifi_password);

        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < DEFAULT_WIFI_TIMEOUT_MS) {
            delay(250);
            Serial.print(".");
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            WiFi.setAutoReconnect(true);
            WiFi.setSleep(false); // Disable modem sleep: reduces MQTT arrival latency to < 5ms
            Serial.printf("WiFiService: Connected via fallback credentials! IP: %s (NoSleep enabled)\n",
                          WiFi.localIP().toString().c_str());
            return true;
        }
        Serial.println("WiFiService: Fallback WiFi failed; falling back to WiFiManager.");
        WiFi.disconnect(true);
        delay(100);
    }

    // 2. Setup WiFiManager with custom MQTT parameter
    WiFiManager wm;
    wm.setSaveConfigCallback(saveConfigCallback);

    WiFiManagerParameter customMqtt("server", "MQTT Server IP", mqttServerBuffer, 40);
    wm.addParameter(&customMqtt);

    if (ConfigStore::isDoubleResetDetected()) {
        Serial.println("WiFiService: Double reset detected; clearing saved WiFi...");
        wm.resetSettings();
    }

    Serial.printf("WiFiService: Starting WiFiManager (AP: %s)...\n", WIFI_PORTAL_SSID);
    if (!wm.autoConnect(WIFI_PORTAL_SSID)) {
        Serial.println("WiFiService: Portal failed or timed out. Restarting...");
        delay(1000);
        ESP.restart();
        return false;
    }

    if (shouldSaveConfig) {
        strlcpy(mqttServerBuffer, customMqtt.getValue(), mqttServerLen);
        ConfigStore::saveMqttServer(mqttServerBuffer);
    }

    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false); // Disable modem sleep for instant response
    Serial.printf("WiFiService: WiFi Connected! Local IP: %s (NoSleep enabled)\n", WiFi.localIP().toString().c_str());
    return true;
}

void WiFiService::loop() {
    if (WiFi.status() != WL_CONNECTED) {
        static unsigned long lastCheck = 0;
        if (millis() - lastCheck > 10000) {
            lastCheck = millis();
            Serial.println("WiFiService: Disconnected, attempting reconnect...");
            WiFi.reconnect();
        }
    }
}
