#include "oled_display.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool OledDisplay::available = false;

// Dual-color OLED layout boundaries:
// Rows 0 to 15:  YELLOW section (Header)
// Rows 16 to 63: BLUE section (Main body, 48px height)

void OledDisplay::drawWiFiSignal(int x, int y, int rssi) {
    if (!available) return;

    int activeBars = 0;
    if (rssi != 0 && rssi > -95) {
        if (rssi >= -55) activeBars = 4;
        else if (rssi >= -67) activeBars = 3;
        else if (rssi >= -78) activeBars = 2;
        else activeBars = 1;
    }

    const int heights[4] = {3, 5, 8, 11};
    const int yOffsets[4] = {8, 6, 3, 0};

    for (int i = 0; i < 4; i++) {
        int bx = x + (i * 3);
        int by = y + yOffsets[i];
        int bh = heights[i];

        if (i < activeBars) {
            display.fillRect(bx, by, 2, bh, SSD1306_WHITE);
        } else {
            display.drawPixel(bx, y + 10, SSD1306_WHITE);
        }
    }
}

static void renderTopBar(const char* title, int rssi, int vol) {
    display.clearDisplay();
    display.setTextWrap(false);
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    // Yellow section (Rows 0-14)
    display.setCursor(2, 3);
    display.print(title ? title : "LUYSOT");

    display.setCursor(56, 3);
    display.printf("V:%d%%", vol);

    OledDisplay::drawWiFiSignal(114, 2, rssi);

    // Crisp separator between Yellow and Blue zones
    display.drawFastHLine(0, 15, SCREEN_WIDTH, SSD1306_WHITE);
}

static void renderSolidHeader(const char* title) {
    display.clearDisplay();
    display.setTextWrap(false);

    // Yellow section filled bar
    display.fillRect(0, 0, SCREEN_WIDTH, 14, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.setTextSize(1);

    int textLen = strlen(title);
    int x = (SCREEN_WIDTH - (textLen * 6)) / 2;
    if (x < 2) x = 2;
    display.setCursor(x, 3);
    display.print(title);

    display.setTextColor(SSD1306_WHITE);
}

bool OledDisplay::init() {
    if (!OLED_ENABLED) {
        available = false;
        return true;
    }

    Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN, 400000);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.printf("OledDisplay: Failed to init SSD1306 at 0x%02X\n", OLED_ADDRESS);
        available = false;
        return false;
    }

    available = true;
    display.setTextWrap(false);
    Serial.printf("OledDisplay: Initialized on SDA:%d, SCL:%d\n", OLED_SDA_PIN, OLED_SCL_PIN);
    showBooting();
    return true;
}

void OledDisplay::clear() {
    if (!available) return;
    display.clearDisplay();
    display.display();
}

void OledDisplay::showBooting() {
    if (!available) return;
    renderSolidHeader("LUYSOT SPEAKER");

    display.setTextSize(1);
    display.setCursor(14, 24);
    display.println("Dual-Core ESP32-S3");

    display.setCursor(10, 38);
    display.println("Ultra Low-Latency");

    display.setCursor(18, 52);
    display.println("Starting engine...");
    display.display();
}

void OledDisplay::showWiFiConnecting(const char* ssid) {
    if (!available) return;
    renderSolidHeader("WIFI CONNECTING");

    display.setTextSize(1);
    display.setCursor(6, 24);
    display.println("Connecting to:");

    display.setCursor(6, 38);
    if (ssid && ssid[0]) {
        display.printf("%.18s\n", ssid);
    } else {
        display.println("Configured AP");
    }

    display.setCursor(6, 52);
    display.println("Please wait...");
    display.display();
}

void OledDisplay::showWiFiConnected(const char* ssid, const char* ip, int rssi) {
    if (!available) return;
    renderTopBar("LUYSOT", rssi, 100);

    display.setTextSize(1);
    display.setCursor(6, 22);
    display.printf("Net: %.16s\n", (ssid && ssid[0]) ? ssid : "Connected");

    display.setCursor(6, 36);
    display.printf("IP:  %.16s\n", ip ? ip : "0.0.0.0");

    display.setCursor(6, 50);
    display.printf("Signal: %d dBm\n", rssi);
    display.display();
}

void OledDisplay::showWiFiPortal(const char* apName) {
    if (!available) return;
    renderSolidHeader("SETUP MODE (AP)");

    display.setTextSize(1);
    display.setCursor(6, 22);
    display.println("Join WiFi:");

    display.setCursor(6, 36);
    display.printf("%.18s\n", apName ? apName : "Luysot_Speaker");

    display.setCursor(6, 50);
    display.println("Open: 192.168.4.1");
    display.display();
}

void OledDisplay::showReady(const char* ssid, int rssi, const char* ip, uint32_t txCount, int volumePercent) {
    if (!available) return;
    renderTopBar("LUYSOT", rssi, volumePercent);

    // Blue section: Spacious, minimal, professional

    // 1. Prominent Status badge (x=24, y=20, w=80, h=14)
    display.drawRoundRect(22, 19, 84, 15, 3, SSD1306_WHITE);
    display.fillCircle(30, 26, 2, SSD1306_WHITE);
    display.setCursor(38, 23);
    display.setTextSize(1);
    display.print("READY TO PAY");

    // 2. WiFi Network line (y=38) - safely truncated, never wraps
    display.setCursor(4, 38);
    if (ssid && ssid[0]) {
        display.printf("WiFi: %.12s\n", ssid);
    } else {
        display.println("WiFi: Connected");
    }

    // 3. Bottom Footer (y=52): IP on left, Tx counter on right
    display.setCursor(4, 52);
    display.printf("%.13s", ip ? ip : "No IP");

    char countBuf[10];
    snprintf(countBuf, sizeof(countBuf), "#%lu", (unsigned long)txCount);
    int cx = SCREEN_WIDTH - (strlen(countBuf) * 6) - 4;
    display.setCursor(cx, 52);
    display.print(countBuf);

    display.display();
}

void OledDisplay::showPlaying(const char* transactionId, uint32_t txCount) {
    if (!available) return;
    renderSolidHeader(">> INCOMING PAYMENT <<");

    // Big centered "SPEAKING" in size 2
    display.setTextSize(2);
    display.setCursor(16, 20);
    display.print("SPEAKING");

    // Reference ID
    display.setTextSize(1);
    display.setCursor(4, 40);
    if (transactionId && transactionId[0]) {
        display.printf("Ref: #%.14s", transactionId);
    } else {
        display.printf("Payment #%lu", (unsigned long)txCount);
    }

    // High-contrast audio pulse bar
    display.drawRoundRect(2, 52, 124, 9, 2, SSD1306_WHITE);
    display.fillRoundRect(5, 54, 118, 5, 1, SSD1306_WHITE);

    display.display();
}

void OledDisplay::showCompleted(uint32_t txCount) {
    if (!available) return;
    renderSolidHeader("PAYMENT COMPLETE");

    // Centered big "SUCCESS"
    display.setTextSize(2);
    display.setCursor(22, 20);
    display.print("SUCCESS");

    display.setTextSize(1);
    display.setCursor(12, 40);
    display.println("Voice Announced!");

    display.setCursor(12, 52);
    display.printf("Total Count: %lu tx", (unsigned long)txCount);

    display.display();
}

void OledDisplay::showError(const char* msg) {
    if (!available) return;
    renderSolidHeader("! NOTICE !");

    display.setTextSize(1);
    display.setCursor(8, 26);
    display.println(msg ? msg : "Playback Error");

    display.setCursor(8, 44);
    display.println("Retrying next alert...");
    display.display();
}
