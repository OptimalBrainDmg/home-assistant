# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Battery-powered Arduino firmware for a Seeed XIAO ESP32C6 that reads temperature and humidity from an SHTC3 sensor and publishes to Home Assistant via MQTT with auto-discovery. Uses deep sleep between readings for low power operation.

## Hardware

| Component | Connection |
|-----------|-----------|
| Seeed XIAO ESP32C6 | ESP32-C6, 3.3V logic, USB-C |
| SHTC3 | I2C: SDA→D4, SCL→D5, VCC→3.3V — no pull-ups needed |

Default I2C pins are set by the board variant (`Wire.begin()` with no args). Verify actual GPIO numbers with `arduino-cli board details --fqbn esp32:esp32:XIAO_ESP32C6` if needed.

## Required Libraries (install via Arduino Library Manager)

- **Adafruit SHTC3 library** (depends on Adafruit Unified Sensor)
- **PubSubClient** by Nick O'Leary
- **WiFi** (bundled with the ESP32 board package)

Board package: `esp32` by Espressif Systems — add `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json` to Board Manager URLs. Requires core version 3.0.0+ for ESP32-C6 support. Select board: **XIAO_ESP32C6**.

## Build & Flash

```bash
# Compile
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C6 .

# Upload (find port with: arduino-cli board list)
arduino-cli upload --fqbn esp32:esp32:XIAO_ESP32C6 --port /dev/cu.usbmodem* .

# Monitor serial at 115200 baud
arduino-cli monitor --port /dev/cu.usbmodem* --config baudrate=115200
```

## Configuration Before Flashing

1. Copy `secrets.h.example` → `secrets.h` and fill in WiFi and MQTT credentials.
2. Adjust `SLEEP_DURATION_SEC` in the sketch if a different interval is needed (default: 600 s / 10 min).

## Architecture

Single-file sketch (`temp_humidity_sensor_battery.ino`). Deep sleep means the device resets on each wake cycle, so `setup()` does all the work and `loop()` is empty.

Wake cycle order:
1. Init I2C and read SHTC3 — **before** WiFi to avoid radio self-heating skew on the sensor
2. Connect WiFi (bounded to `WIFI_TIMEOUT_MS`; sleeps on failure)
3. Connect MQTT and publish retained discovery payloads (bounded to `MQTT_TIMEOUT_MS`; sleeps on failure)
4. Publish state JSON to `home/<deviceId>/state`
5. Clean shutdown: `mqtt.disconnect()` → `WiFi.disconnect(true)` → `WiFi.mode(WIFI_OFF)`
6. `esp_deep_sleep_start()` for `SLEEP_DURATION_SEC` seconds

**Device identity**: derived from lower 3 bytes of `ESP.getEfuseMac()` (e.g. `xiao_a1b2c3`).

**Failure handling**: WiFi timeout, MQTT timeout, and sensor errors all call `goToSleep()` immediately rather than spinning, so a transient failure drains minimal battery.

**Discovery republish**: retained payloads are republished on every wake (simplest approach; MQTT broker deduplicates since the payload is unchanged).

## Home Assistant Setup

1. Install the **Mosquitto broker** add-on in HA and enable the MQTT integration.
2. Flash the device — HA will auto-discover "Temperature" and "Humidity" entities grouped under "XIAO Sensor".
