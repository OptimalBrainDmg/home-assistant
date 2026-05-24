# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Arduino firmware for a LOLIN D1 Mini V4 (ESP8266) that reads temperature and humidity from an I2C sensor and publishes to Home Assistant via MQTT with auto-discovery.

## Hardware

| Component | Connection |
|-----------|-----------|
| LOLIN D1 Mini V4 | ESP8266-based, 3.3V logic |
| AM2320 | I2C: SDA→D2 (GPIO4), SCL→D1 (GPIO5), VCC→3.3V — requires 4.7kΩ pull-ups |
| SHTC3 | Same I2C pins, no pull-ups needed |

## Required Libraries (install via Arduino Library Manager)

- **Adafruit AM2320 sensor library** (depends on Adafruit Unified Sensor) — for AM2320
- **Adafruit SHTC3 library** — for SHTC3
- **PubSubClient** by Nick O'Leary
- **ESP8266WiFi** (bundled with the ESP8266 board package)

Board package: `esp8266` by ESP8266 Community — add `http://arduino.esp8266.com/stable/package_esp8266com_index.json` to Board Manager URLs. Select board: **LOLIN(WEMOS) D1 mini**.

## Build & Flash

```bash
# Compile
arduino-cli compile --fqbn esp8266:esp8266:d1_mini .

# Upload (find port with: arduino-cli board list)
arduino-cli upload --fqbn esp8266:esp8266:d1_mini --port /dev/cu.usbserial-* .

# Monitor serial at 115200 baud
arduino-cli monitor --port /dev/cu.usbserial-* --config baudrate=115200
```

## Configuration Before Flashing

1. Copy `secrets.h.example` → `secrets.h` and fill in WiFi and MQTT credentials.
2. In `temp_humidity_sensor.ino`, uncomment the `#define` for the sensor in use (`SENSOR_AM2320` or `SENSOR_SHTC3`).

## Architecture

Single-file sketch (`temp_humidity_sensor.ino`):

- **Sensor abstraction**: a compile-time `#define` (`SENSOR_AM2320` or `SENSOR_SHTC3`) selects the driver; `readSensor()` normalizes both into a `float temp, humid` pair.
- **Device identity**: `deviceId` is derived from `ESP.getChipId()` (e.g. `d1mini_a1b2c3`) so multiple devices don't collide on the same broker.
- **MQTT topics**: state published to `home/<deviceId>/state` as `{"temperature":23.4,"humidity":55.1}`; retained discovery configs published to `homeassistant/sensor/<deviceId>/temperature|humidity/config` on each connect.
- **`mqtt.setBufferSize(512)`** is required — discovery JSON payloads exceed PubSubClient's 256-byte default.
- WiFi and MQTT reconnection are handled in `loop()` before every sensor publish.

## Home Assistant Setup

1. Install the **Mosquitto broker** add-on in HA and enable the MQTT integration.
2. Flash the device — HA will auto-discover "Temperature" and "Humidity" entities grouped under "D1 Mini Sensor".
