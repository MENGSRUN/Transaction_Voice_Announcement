#pragma once

#include <Arduino.h>

class TimeSync {
public:
    static bool sync(unsigned long timeoutMs = 10000);
    static bool isSynced();
};
