#include "time_sync.h"
#include <time.h>

bool TimeSync::sync(unsigned long timeoutMs) {
    Serial.println("TimeSync: Synchronizing clock with NTP servers for TLS validation...");
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    unsigned long started = millis();
    while (time(nullptr) < 1704067200 && millis() - started < timeoutMs) {
        delay(100);
    }

    if (time(nullptr) < 1704067200) {
        Serial.println("TimeSync: WARNING - NTP synchronization timed out. HTTPS cert validation may fail.");
        return false;
    }

    Serial.println("TimeSync: Clock synchronized successfully.");
    return true;
}

bool TimeSync::isSynced() {
    return time(nullptr) >= 1704067200;
}
