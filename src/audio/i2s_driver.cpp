#include "i2s_driver.h"
#include "config.h"

AudioOutputI2S* I2SDriver::i2sOut = nullptr;

bool I2SDriver::init() {
    if (i2sOut) return true; // Already initialized

    i2sOut = new (std::nothrow) AudioOutputI2S(0, AudioOutputI2S::EXTERNAL_I2S, 8, AudioOutputI2S::APLL_DISABLE);
    if (!i2sOut) {
        Serial.println("I2SDriver: Failed to allocate AudioOutputI2S");
        return false;
    }

    i2sOut->SetPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DOUT_PIN);
    i2sOut->SetOutputModeMono(true);
    i2sOut->SetGain(VOICE_GAIN);

    Serial.println("I2SDriver: Persistent I2S output ready on GPIO 4 (BCLK), 5 (LRC), 6 (DIN)");
    return true;
}

AudioOutputI2S* I2SDriver::getOutput() {
    if (!i2sOut) init();
    return i2sOut;
}

void I2SDriver::setGain(float gain) {
    if (i2sOut) {
        i2sOut->SetGain(gain);
    }
}

void I2SDriver::mute() {
    if (i2sOut) {
        i2sOut->SetGain(0.0f);
    }
}
