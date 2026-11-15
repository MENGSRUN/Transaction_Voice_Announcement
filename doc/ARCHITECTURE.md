# ESP32-S3 Ultra-Low Latency Voice Announcement Architecture

## 1. Overview & Objectives

The goal of this architecture is to reduce end-to-end transaction voice announcement latency to **sub-second (< 30ms to first sound)** on the **ESP32-S3 DevKitC-1 N16R8** with **MAX98357A I2S Amplifier**, while eliminating all audio popping, crackling, and network-induced playback stutter.

### Key Performance Targets
* **Time to First Audio (Cash Ding):** `< 30ms` from MQTT message arrival (instant auditory feedback).
* **Network Stutter:** `0%` (Voice decoded directly from 8MB PSRAM, completely decoupled from WiFi jitter).
* **Audio Transients:** Zero clicks or pops during amplifier initialization or between audio segments.

---

## 2. Low-Latency Execution Pipeline

```text
                  MQTT Command Arrives (store1/transaction-voice/commands)
                                       │
                    ┌──────────────────┴──────────────────┐
                    ▼                                     ▼
        CORE 1 (Audio Engine)                  CORE 0 (Network Engine)
   ┌────────────────────────────────┐     ┌────────────────────────────────┐
   │ INSTANT TRIGGER (<10ms)        │     │ PARALLEL DOWNLOAD (<300ms)     │
   │ 1. Play Cash Ding (PROGMEM)    │     │ 1. Cached DNS & TLS Session    │
   │ 2. Continuous I2S DMA feed     │     │ 2. Download ~20KB voice MP3    │
   │ 3. Zero click/pop transitions  │     │ 3. Save directly to 8MB PSRAM  │
   │                                │     │ 4. Close TLS connection early  │
   └────────────────┬───────────────┘     └────────────────┬───────────────┘
                    │                                      │
                    └──────────────────┬───────────────────┘
                                       │
                                       ▼
        Cash Ding finishes (~1.0s) ──► Voice MP3 already in 8MB PSRAM!
                                       │
                                       ▼
                     Voice plays seamlessly from RAM with
                       ZERO network jitter & ZERO delay!
```

---

## 3. Modular Code Structure

```text
Transaction_Voice_Announcement/
├── platformio.ini              # Configured for esp32-s3-devkitc-1 (N16R8)
├── include/
│   ├── config.h                # Central hardware pins, buffer limits, timeouts
│   ├── audio_assets.h          # Embedded sounds (Cash Register Ding PROGMEM)
│   ├── audio_url_validation.h  # URL safety & allowed host filters
│   ├── letsencrypt_root_ca.h   # Let's Encrypt Root CA certificate
│   └── wifi_credentials.h     # Fallback WiFi credentials
└── src/
    ├── main.cpp                # Clean FreeRTOS orchestrator (~60 lines)
    ├── audio/
    │   ├── i2s_driver.h/.cpp   # Persistent MAX98357A driver singleton (no pops)
    │   └── audio_player.h/.cpp # Audio Engine coordinating Ding and Voice playback
    ├── display/
    │   └── oled_display.h/.cpp # SSD1306 OLED (128x64) on GPIO 15 (SDA) & 16 (SCL)
    ├── network/
    │   ├── time_sync.h/.cpp    # NTP clock sync for TLS certificates
    │   ├── wifi_svc.h/.cpp     # WiFiManager portal + fallback WiFi connection
    │   └── mqtt_svc.h/.cpp     # QoS 1 command listener & status reporter
    └── storage/
        └── config_store.h/.cpp # SPIFFS config.json loader & double-reset flag
```

---

## 4. Hardware Pinout Reference (ESP32-S3 DevKitC-1 N16R8)

| Component | Pin Function | ESP32-S3 GPIO | Electrical Notes |
| :--- | :--- | :--- | :--- |
| **MAX98357A** | BCLK (Bit Clock) | **GPIO 4** | Standard GP-IO |
| **MAX98357A** | LRC / WS (Word Select) | **GPIO 5** | Standard GP-IO |
| **MAX98357A** | DIN (Data In) | **GPIO 6** | Standard GP-IO |
| **MAX98357A** | VIN (Power) | **5V / VBUS** | Recommended 5V for 3.2W max output |
| **MAX98357A** | GND (Ground) | **GND** | Shared common ground |
| **MAX98357A** | GAIN | **Floating** | Default +9dB gain |
| **MAX98357A** | SD / EN | **3V3 or Floating**| Left channel active, amp enabled |
| **SSD1306 OLED** | SDA (Data) | **GPIO 15** | I2C Data (400kHz Fast Mode) |
| **SSD1306 OLED** | SCL (Clock) | **GPIO 16** | I2C Clock (400kHz Fast Mode) |
| **SSD1306 OLED** | VCC | **3V3** | 3.3V Power rail |
| **SSD1306 OLED** | GND | **GND** | Shared common ground |
