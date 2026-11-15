#pragma once

#include <Arduino.h>

class OledDisplay {
public:
    static bool init();
    static void showBooting();
    static void showWiFiConnecting(const char* ssid);
    static void showWiFiConnected(const char* ssid, const char* ip, int rssi);
    static void showWiFiPortal(const char* apName);
    static void showReady(const char* ssid, int rssi, const char* ip, uint32_t txCount, bool isOnline = true, int volumePercent = 100);
    static void showPlaying(const char* transactionId, uint32_t txCount);
    static void showCompleted(uint32_t txCount);
    static void showError(const char* msg);
    static void drawWiFiSignal(int x, int y, int rssi);
    static void clear();

private:
    static bool available;
};
