# Transaction Voice Announcement ESP32

ESP32 firmware for a transaction voice speaker. The device connects to WiFi, subscribes to an MQTT topic, downloads an MP3 transaction voice announcement, plays a cash-register sound, then plays the downloaded voice through a MAX98357A I2S amplifier.

## License

This project is source-available for non-commercial use under the [PolyForm Noncommercial License 1.0.0](LICENSE).

Official license text:

https://spdx.org/licenses/PolyForm-Noncommercial-1.0.0.html

You can:

- Use it for personal, learning, research, school, hobby, charity, or government use
- Copy, modify, and share the source code for non-commercial purposes
- Build your own device for personal or internal non-commercial use

You cannot:

- Sell this software
- Sell ESP32 devices flashed with this firmware
- Use this firmware in a paid product or paid service
- Repackage or resell this project without written permission

Commercial licensing requires separate written permission from the copyright holder.

Note: this is not an OSI-approved "open source" license because it restricts commercial use. It is intentionally source-available non-commercial software.

## Features

- ESP32 Arduino firmware using PlatformIO
- WiFi setup with WiFiManager captive portal
- Optional built-in default WiFi fallback
- MQTT transaction command listener
- HTTPS MP3 download into a fixed RAM buffer before playback
- Embedded cash-register MP3 sound stored in firmware
- MAX98357A I2S audio output
- SSD1306 OLED status display
- Double-reset settings clear flow

## Hardware

Tested target:

- ESP32 Lite V1.0.0 / ESP32 dev board
- MAX98357A I2S amplifier
- 0.9 inch SSD1306 OLED, 128x64, I2C address `0x3C`
- Speaker connected to MAX98357A output

## Wiring

### MAX98357A I2S Amplifier

| ESP32 GPIO | MAX98357A Pin |
|---|---|
| GPIO26 | BCLK |
| GPIO25 | LRC / WS |
| GPIO27 | DIN |
| 3V3 | VIN |
| GND | GND |

The MAX98357A `SD` pin can be tied to `3V3` to keep the amplifier enabled.

### OLED Display

| ESP32 GPIO | OLED Pin |
|---|---|
| GPIO23 | SDA |
| GPIO22 | SCL |
| 3V3 | VCC |
| GND | GND |

GPIO22 is used for OLED SCL, so the firmware disables the old LED pin by setting `ledPin = -1`.

## Software Requirements

- VS Code
- PlatformIO extension
- ESP32 USB driver if your board needs one

Libraries are installed by PlatformIO from `platformio.ini`:

- `tzapu/WiFiManager`
- `knolleary/PubSubClient`
- `bblanchon/ArduinoJson`
- `earlephilhower/ESP8266Audio@1.9.9`
- `adafruit/Adafruit GFX Library`
- `adafruit/Adafruit SSD1306`

## Configure Firmware

Main configuration is in [src/main.cpp](src/main.cpp).

Important constants:

```cpp
const char* allowed_audio_host = "voice-service-api.weknowyouknow.dev";
const char* default_wifi_ssid = "";
const char* default_wifi_password = "";
char mqtt_server[40] = "192.168.18.120";
const int mqtt_port = 1883;
const char* mqtt_topic = "store/transaction-voice/commands";
```

Leave the built-in WiFi values empty for normal use. Configure WiFi through the setup portal so real credentials are not compiled into firmware.

## Build, Upload, And Monitor

Using PlatformIO:

```bash
pio run
pio run --target upload
pio device monitor
```

Serial monitor speed is:

```ini
monitor_speed = 115200
```

In VS Code PlatformIO, you can also use:

- `Build`
- `Upload`
- `Monitor`
- `Upload and Monitor`

## First Boot Setup

On boot, the firmware tries this order:

1. Load saved MQTT config from SPIFFS.
2. Keep a 5-second double-reset window for clearing settings.
3. Try the optional built-in default WiFi SSID/password if configured.
4. If that fails, open a WiFiManager setup AP.

Setup AP:

```text
SSID: Luysot_Speaker_Setup
IP:   192.168.4.1
```

Connect your phone or laptop to that AP, then enter:

- WiFi SSID
- WiFi password
- MQTT server IP

The MQTT server IP is saved to `/config.json` in SPIFFS.

## Reset WiFi And MQTT Settings

The board only has an `RST` button, so the firmware uses double reset detection.

To clear saved WiFi and MQTT settings:

1. Press `RST`.
2. Press `RST` again within 5 seconds.
3. The firmware clears WiFiManager settings and `/config.json`.
4. The board restarts and opens the setup portal.

Expected serial log:

```text
Double reset detected. Clearing WiFi and MQTT settings...
Settings cleared. Restarting...
```

## MQTT Message Format

Publish JSON to:

```text
store/transaction-voice/commands
```

Example:

```json
{
  "id": "example-id",
  "url": "https://voice-service-api.weknowyouknow.dev/media/example.mp3"
}
```

Validation rules:

- Topic must match `store/transaction-voice/commands`
- Payload length must be between `1` and `512` bytes
- `id` must be present
- `url` must be HTTPS
- `url` host must match `allowed_audio_host`
- URL path must end with `.mp3` before any query string
- MP3 size must be greater than `0` and less than `96 KB`
- Up to 4 valid audio URLs are queued while the device is already busy

## Audio Flow

When a valid MQTT message arrives:

1. Download the voice MP3 into a fixed RAM buffer.
2. Play the embedded cash-register sound from `include/cash_ding_mp3.h`.
3. Play the downloaded transaction voice MP3.
4. Free audio memory when playback finishes.

Cash sound volume is controlled here:

```cpp
dingOut->SetGain(0.65);
```

Voice volume is controlled here:

```cpp
out->SetGain(1.0);
```

## OLED Status

The OLED shows:

- Boot status
- WiFi status
- Setup portal status
- MQTT connection status
- Download status
- Cash ding status
- Speaking face animation during voice playback
- Audio errors

If the OLED is not connected, firmware continues running and prints:

```text
OLED not found
```

## Troubleshooting

### Serial monitor shows unreadable text

Make sure monitor speed is `115200`.

```ini
monitor_speed = 115200
```

### `Ignoring invalid url`

Check `allowed_audio_host`. It must match the exact host in the URL.

For example, this URL:

```text
https://voice-service-api.weknowyouknow.dev/media/file.mp3
```

requires:

```cpp
const char* allowed_audio_host = "voice-service-api.weknowyouknow.dev";
```

### `ERROR: Failed to download audio URL`

Check:

- ESP32 has WiFi
- URL is reachable from the ESP32 network
- MP3 file is smaller than `96 KB`
- Server returns HTTP `200`
- HTTPS server supports ESP32 TLS connection
- Server does not require HTTP redirects

### `ERROR: Failed to open audio memory`

Usually means the downloaded audio buffer is empty or was freed before playback. The current firmware downloads first, plays the cash sound without freeing the voice buffer, then opens the downloaded MP3 from memory.

### Voice is laggy or broken

The firmware downloads the whole MP3 before playing to avoid HTTPS streaming buffer underflow. Keep MP3 files small and simple:

- MP3 format
- Mono preferred
- 22050 Hz or 44100 Hz
- Small file size, under `96 KB`

### No sound

Check:

- MAX98357A `VIN` to `3V3`
- MAX98357A `GND` to ESP32 `GND`
- MAX98357A `BCLK` to GPIO26
- MAX98357A `LRC` to GPIO25
- MAX98357A `DIN` to GPIO27
- MAX98357A `SD` tied to `3V3`
- Speaker connected to amplifier output

## Project Files

```text
src/main.cpp              Main firmware
include/cash_ding_mp3.h   Embedded cash-register MP3 data
platformio.ini            PlatformIO environment and dependencies
```

## Notes

- MQTT is intentionally unauthenticated in this firmware. Treat the LAN/VPN as the trust boundary and do not expose the broker publicly.
- The firmware still uses `WiFiClientSecure.setInsecure()` for HTTPS downloads. This skips certificate validation; replace it with a pinned CA or PlatformIO embedded certificate bundle before using hostile networks.
- Voice MP3 storage uses a fixed `96 KB` RAM buffer to avoid heap fragmentation. At `32 kbps`, that is roughly 24 seconds of audio.
- Avoid committing real WiFi credentials to shared repositories.
