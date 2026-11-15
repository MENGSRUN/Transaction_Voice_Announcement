#pragma once

#include <Arduino.h>
#include "AudioOutputI2S.h"

class I2SDriver {
public:
    static bool init();
    static AudioOutputI2S* getOutput();
    static void setGain(float gain);
    static void mute();

private:
    static AudioOutputI2S* i2sOut;
};
