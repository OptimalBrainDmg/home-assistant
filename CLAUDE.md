# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Behavioral Guidelines

- **Do not overclaim.** Without serial output from the device, root cause diagnosis is a hypothesis, not a conclusion. Say so explicitly.
- **Do not promise that a change fixes a problem** unless it can be verified via serial monitor or observable HA behavior. "This should fix it" is not "this fixes it."
- **Retained MQTT payloads are stale when the device is unreachable.** If the device is stuck before reaching MQTT, retained state/diag topics reflect the last successful connect, not current state.

## Repository Structure

Collection of Arduino sketches for ESP8266/ESP32-based sensors and actuators that integrate with Home Assistant via MQTT. Each sketch lives in its own subdirectory and has its own `CLAUDE.md` with hardware pinout, required libraries, build/flash commands, and architecture notes. **Always read the sketch's own `CLAUDE.md` before editing it.**

| Sketch | Hardware | HA Entities | FQBN |
|--------|----------|-------------|------|
| `temp_humidity_sensor/` | LOLIN D1 Mini V4 (ESP8266) | Temperature, Humidity | `esp8266:esp8266:d1_mini` |
| `temp_humidity_sensor_battery/` | Seeed XIAO ESP32C6 (deep sleep) | Temperature, Humidity | `esp32:esp32:XIAO_ESP32C6` |
| `lightstrip/` | Adafruit HUZZAH32 ESP32 Feather | Light zones (on/off, brightness, RGB) | `esp32:esp32:featheresp32` |
| `fallout-terminal-pyportal/` | Adafruit PyPortal (SAMD51 + ESP32 WiFi) | Temperature; touch UI for light zones | `adafruit:samd:adafruit_pyportal_m4` |
| `epaper_weather/` | LILYGO T5 4.7" (ESP32-WROVER-E, deep sleep) | Weather dashboard (REST from NWS/HA) | `esp32:esp32:esp32wrover` |
| `indoor_air_monitor/` | Seeed XIAO ESP32C6 (always-on) | Temperature, Humidity, CO2, TVOC, eCO2 | `esp32:esp32:XIAO_ESP32C6` |
| `printer_room_monitor/` | Seeed XIAO ESP32C6 (always-on) | Temperature, Humidity, Smoke (mV), BLE filament sensors (T/H/battery) | `esp32:esp32:XIAO_ESP32C6:PartitionScheme=huge_app` |
| `xmas/` | Seeed XIAO ESP32C6 | MERRY CHRISTMAS sign (on/off, brightness, 5 animation effects) | `esp32:esp32:XIAO_ESP32C6` |

`temp_humidity_sensor_battery/enclosure/sensor.scad` and `lightstrip/enclosure/lightstrip.scad` are parametric OpenSCAD models for 3D-printable enclosures.

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

**Exception — `epaper_weather/`**: this sketch pins to ESP32 core **2.0.17** via `sketch.yaml` because LilyGo-EPD47 v1.0.1 uses an I2S/LCD clock source API that broke in IDF 5.x (core 3.x). Use `--profile default` instead of `--fqbn`; it downloads the pinned core into an isolated cache and does not affect the globally installed core 3.x used by other sketches:

```bash
arduino-cli compile --profile default epaper_weather/
arduino-cli compile --upload --profile default --port <port> epaper_weather/
```

Do **not** use `--build-property` flags or patch library files as a workaround — `sketch.yaml` is the correct fix.

Board package URLs to add to Board Manager:
- ESP8266: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
- ESP32 (Espressif): `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json` (requires core 3.0.0+ for ESP32-C6)
- Adafruit SAMD: `https://adafruit.github.io/arduino-board-index/package_adafruit_index.json`

## Conventions Across All Sketches

- Credentials go in `secrets.h` (ESP8266/ESP32 sketches) or `sdcard/config.jsn` (PyPortal); both are gitignored. Copy the committed `secrets.h.example` (or `config.jsn.example`) to the real filename and fill in values before flashing.
- Device identity is derived from chip ID — `ESP.getChipId()` on ESP8266, a lower-byte slice of `ESP.getEfuseMac()` on ESP32 (3 bytes in most sketches, 4 bytes in `indoor_air_monitor/` and `printer_room_monitor/`), WiFi MAC on PyPortal — so multiple devices don't collide on the broker. Check each sketch's CLAUDE.md for the exact prefix and byte count.
- MQTT auto-discovery publishes **retained** config payloads to `homeassistant/<domain>/<deviceId>/…/config`. If you rename a zone or remove a device, clean up stale HA entities by publishing an empty retained message to the old discovery topic (e.g. via MQTT Explorer), then removing the entity from HA's MQTT integration page.
- `mqtt.setBufferSize()` must be called in `setup()` — discovery payloads exceed PubSubClient's 256-byte default. Values: 512 B (temp sensors, PyPortal, `xmas/`, `printer_room_monitor/`) and 1024 B (`lightstrip/`, `indoor_air_monitor/`).
- **`epaper_weather/` uses MQTT only for battery reporting** — weather data comes from the HA REST API (`/api/states` and `/api/services/weather/get_forecasts`) via a long-lived access token. Battery percentage is published to MQTT once per wake cycle (after WiFi connects) for HA auto-discovery as a sensor.
- **ArduinoJson versions differ by sketch**: `lightstrip/` uses v6 (`StaticJsonDocument` API); all other sketches use v7 (`JsonDocument` API, no template size parameter). Do not mix APIs within a sketch — check the sketch's own `CLAUDE.md` before adding JSON code.
- **`lightstrip/` requires the Adafruit MCP9808 library even if no sensor is physically connected** — the sketch references it unconditionally. Omitting the library causes a compile error.
- **HA JSON light schema**: state payloads must include `"color_mode": "rgb"` — without it HA does not show the color picker even if the discovery config declares RGB support. The deprecated `"rgb": true` / `"brightness": true` fields are ignored by current HA versions.
- **MQTT config overrides**: the PyPortal supports retained MQTT topics (e.g. `tz_topic`) that override values loaded from the SD card at boot. Publishing a retained value to the topic updates the device immediately and restores automatically on every reconnect — use this pattern when a config value needs to change without reflashing or SD card edits.
