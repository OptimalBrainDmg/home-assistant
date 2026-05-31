# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Structure

Collection of Arduino sketches for ESP8266/ESP32-based sensors and actuators that integrate with Home Assistant via MQTT. Each sketch lives in its own subdirectory and has its own `CLAUDE.md` with hardware pinout, required libraries, build/flash commands, and architecture notes.

| Sketch | Hardware | HA Entities | FQBN |
|--------|----------|-------------|------|
| `temp_humidity_sensor/` | LOLIN D1 Mini V4 (ESP8266) | Temperature, Humidity | `esp8266:esp8266:d1_mini` |
| `temp_humidity_sensor_battery/` | Seeed XIAO ESP32C6 (deep sleep) | Temperature, Humidity | `esp32:esp32:XIAO_ESP32C6` |
| `lightstrip/` | Adafruit HUZZAH32 ESP32 Feather | Light zones (on/off, brightness, RGB) | `esp32:esp32:featheresp32` |
| `fallout-terminal-pyportal/` | Adafruit PyPortal (SAMD51 + ESP32 WiFi) | Temperature, Light; touch UI for light zones | `adafruit:samd:adafruit_pyportal_m4` |

## Toolchain

All sketches use `arduino-cli`. Find the connected board's port before uploading:

```bash
arduino-cli board list
```

General pattern for any sketch (substitute the correct FQBN and port from the table above):

```bash
arduino-cli compile --fqbn <fqbn> <sketch-dir>
arduino-cli upload  --fqbn <fqbn> --port <port> <sketch-dir>
arduino-cli monitor --port <port> --config baudrate=115200
```

Board package URLs to add to Board Manager:
- ESP8266: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
- ESP32 (Espressif): `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json` (requires core 3.0.0+ for ESP32-C6)
- Adafruit SAMD: `https://adafruit.github.io/arduino-board-index/package_adafruit_index.json`

## Conventions Across All Sketches

- Credentials go in `secrets.h` (ESP8266/ESP32 sketches) or `sdcard/config.jsn` (PyPortal); both are gitignored. Example files with placeholder values are committed instead.
- Device identity is derived from chip ID — `ESP.getChipId()` on ESP8266, lower 3 bytes of `ESP.getEfuseMac()` on ESP32, WiFi MAC on PyPortal — so multiple devices don't collide on the broker.
- MQTT auto-discovery publishes **retained** config payloads to `homeassistant/<domain>/<deviceId>/…/config`. If you rename a zone or remove a device, clean up stale HA entities by publishing an empty retained message to the old discovery topic (e.g. via MQTT Explorer), then removing the entity from HA's MQTT integration page.
- `mqtt.setBufferSize()` must be called in `setup()` — discovery payloads exceed PubSubClient's 256-byte default. Values: 512 B (temp sensors, PyPortal) and 1024 B (lightstrip).
- **ArduinoJson versions differ by sketch**: `lightstrip/` uses v6 (`StaticJsonDocument` API); `fallout-terminal-pyportal/` uses v7 (`JsonDocument` API). Do not mix APIs within a sketch — check the sketch's own `CLAUDE.md` before adding JSON code.
- **HA JSON light schema**: state payloads must include `"color_mode": "rgb"` — without it HA does not show the color picker even if the discovery config declares RGB support. The deprecated `"rgb": true` / `"brightness": true` fields are ignored by current HA versions.
- **MQTT config overrides**: the PyPortal supports retained MQTT topics (e.g. `tz_topic`) that override values loaded from the SD card at boot. Publishing a retained value to the topic updates the device immediately and restores automatically on every reconnect — use this pattern when a config value needs to change without reflashing or SD card edits.
