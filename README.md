# Transaction Voice Announcement ESP32-S3

ESP32 firmware for a transaction voice speaker. The device connects to WiFi, subscribes to an MQTT topic, opens an HTTPS MP3 stream, plays a cash-register sound, then streams the voice through a MAX98357A I2S amplifier.

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

- ESP32-S3 Arduino firmware using PlatformIO
- WiFi setup with WiFiManager captive portal
- Optional built-in default WiFi fallback
- MQTT transaction command listener
- HTTPS MP3 streaming through a reusable 8 KB RAM buffer
- Embedded cash-register MP3 sound stored in firmware
- MAX98357A I2S audio output
- Optional SSD1306 OLED status display (disabled by default)
- Double-reset settings clear flow

## Hardware

Target:

- ESP32-S3 DevKitC-1 with an N16R8 module (16 MB flash and 8 MB octal PSRAM)
- MAX98357A I2S amplifier
- Optional 0.9 inch SSD1306 OLED, 128x64, I2C address `0x3C`
- Speaker connected to MAX98357A output

## Wiring

### MAX98357A I2S Amplifier

| ESP32-S3 GPIO | MAX98357A Pin |
|---|---|
| GPIO4 | BCLK |
| GPIO5 | LRC / WS |
| GPIO6 | DIN |
| 3V3 | VIN |
| GND | GND |

The MAX98357A `SD` pin can be tied to `3V3` to keep the amplifier enabled.

### OLED Display

No OLED connection is needed with the current configuration. `oledEnabled = false`
in `src/main.cpp` skips display initialization and leaves GPIO8/9 unused by the
display. WiFi setup, MQTT, and audio work without it; use the serial monitor at
115200 baud for diagnostics. Set `oledEnabled = true` when you connect the OLED.

| ESP32-S3 GPIO | OLED Pin |
|---|---|
| GPIO8 | SDA |
| GPIO9 | SCL |
| 3V3 | VCC |
| GND | GND |

The firmware leaves `ledPin = -1`. Many ESP32-S3 boards use an addressable RGB LED,
which cannot be driven as the active-low LED used by the original firmware.

Do not reuse the original ESP32 Lite GPIO22/23/25/26/27 wiring. Those GPIOs are
not general-purpose header pins on this ESP32-S3 configuration, and some S3 pins
in that range are reserved for the module's flash and octal PSRAM.

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
const bool useDefaultWiFi = true;
char mqtt_server[40] = "192.168.18.120";
const int mqtt_port = 1883;
const char* mqtt_topic = "store/transaction-voice/commands";
const char* mqtt_status_topic = "store/transaction-voice/status";
```

The default network is configured in the local, Git-ignored
`include/wifi_credentials.h`. With `useDefaultWiFi = true`, the board tries it
first, then falls back to WiFiManager's saved network or setup portal. Set
`useDefaultWiFi = false` in `src/main.cpp` to skip the default network and use
WiFiManager directly. Rebuild and upload after changing this switch.
The local credentials are compiled into the device firmware; keep the build
artifacts private as well.

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
3. If `useDefaultWiFi` is enabled, try the configured default network.
4. Otherwise, or if it fails, try WiFiManager's saved network and then its setup AP.

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
- MP3 size must be greater than `0` and at most `64 KB`
- Up to 4 valid audio URLs are queued while the device is already busy
- IDs are limited to 64 characters and used only for status correlation; repeated IDs are accepted and played again

The device uses a stable MQTT client ID, requests a persistent broker session,
and subscribes at QoS 1. The publisher must also publish at QoS 1 and the broker
must retain persistent sessions for offline command delivery to work.

## Transaction Status

Device lifecycle events are published as JSON to:

```text
store/transaction-voice/status
```

Example:

```json
{
  "id": "example-id",
  "status": "completed"
}
```

Possible statuses are `queued`, `processing`, `playing`, `completed`, `failed`,
and `rejected`. Status publication is best effort because
PubSubClient only publishes at QoS 0; the transaction service must use
`completed` as positive confirmation and handle a missing status conservatively.

## Audio Flow

When a valid MQTT message arrives:

1. Open a validated HTTPS connection and check the response headers.
2. Prime the 8 KB FIFO, then play the embedded cash-register sound.
3. Start MP3 decoding, refilling the FIFO as playback proceeds.
4. Close the HTTPS connection and release decoder/output objects when playback finishes.

The buffer is independent of the file size; the existing 64 KB response limit
remains. The server must return HTTP 200 with a positive Content-Length and an
unencoded, non-chunked MP3 body. Redirects are rejected.
The ding indicates an accepted stream, not guaranteed successful playback.

Before HTTPS streaming, the device synchronizes UTC time over NTP and validates
the server certificate against the embedded Let's Encrypt ISRG Root X1 trust
anchor. If clock synchronization fails, audio streaming fails closed.

Cash sound volume is controlled here:

```cpp
dingOut->SetGain(0.7);
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
- Stream connection status
- Cash ding status
- Speaking face animation during voice playback
- Audio errors

With the default `oledEnabled = false`, firmware skips all OLED access and prints:

```text
OLED disabled; running without display
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

### `ERROR: Failed to open HTTPS audio stream`

Check:

- ESP32 has WiFi
- URL is reachable from the ESP32 network
- MP3 file is at most `64 KB`
- Server returns HTTP `200`
- HTTPS server supports ESP32 TLS connection
- Server does not require HTTP redirects

Transient connection failures are retried up to three times before playback.
Permanent response validation errors fail immediately. A disconnect or five
seconds without progress during a required stream read reports `failed`;
playback is not restarted automatically because that would repeat spoken audio.
Serial logs include HTTPS/header timing.

### `ERROR: Failed to open audio stream`

Check the HTTPS connection and response headers. The stream must stay open for
the entire playback.

### Voice is laggy or broken

Streaming depends on steady WiFi throughput. The 8 KB buffer provides roughly
two seconds of compressed audio at 32 kbps; a longer network stall can interrupt
sound. Keep MP3 files small and simple:

- MP3 format
- Mono preferred
- 22050 Hz or 44100 Hz
- Small file size, at most `64 KB`

### No sound

Check:

- MAX98357A `VIN` to `3V3`
- MAX98357A `GND` to ESP32 `GND`
- MAX98357A `BCLK` to GPIO4
- MAX98357A `LRC` to GPIO5
- MAX98357A `DIN` to GPIO6
- MAX98357A `SD` tied to `3V3`
- Speaker connected to amplifier output

## Project Files

```text
src/main.cpp              Main firmware
include/cash_ding_mp3.h   Embedded cash-register MP3 data
platformio.ini            PlatformIO environment and dependencies
include/audio_url_validation.h  Testable HTTPS/host/path validation
include/audio_https_stream.h    TLS-validated streaming source
include/audio_stream_buffer.h   Lossless FIFO with background refill
include/letsencrypt_root_ca.h    HTTPS trust anchor
test/test_validation/     Device-side URL validation tests
```

## Notes

- MQTT is intentionally unauthenticated in this firmware. Treat the LAN/VPN as the trust boundary and do not expose the broker publicly.
- HTTPS certificate validation depends on NTP access and on the server continuing to present a chain to the embedded ISRG Root X1 certificate. The root expires in June 2035 and must be updated before then.
- Voice streaming uses a fixed `8 KB` RAM buffer. The `64 KB` response-size limit remains independent of this buffer.
- The `esp32-s3-devkitc1-n16r8` PlatformIO board definition selects the matching 16 MB flash, octal PSRAM, and 16 MB partition layout.
- Avoid committing real WiFi credentials to shared repositories.
