# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Structure

Collection of Arduino sketches for ESP8266/ESP32-based sensors and actuators that integrate with Home Assistant via MQTT. Each sketch lives in its own subdirectory and has its own `CLAUDE.md` with hardware pinout, required libraries, build/flash commands, and architecture notes.

Current sketches:
- `temp_humidity_sensor/` — LOLIN D1 Mini V4 reading AM2320 or SHTC3 over I2C, publishing to HA via MQTT auto-discovery
- `lightstrip/` — Adafruit HUZZAH32 ESP32 Feather driving a WS2812B LED strip, exposed to HA as an MQTT light (on/off, brightness, RGB color)
- `fallout-terminal-pyportal/` — Adafruit PyPortal (SAMD51 + ESP32 WiFi co-proc) rendering a Fallout-terminal-style dashboard; publishes sensor readings and shows touch-toggleable light zone buttons; config lives on an SD card instead of `secrets.h`

## Conventions Across All Sketches

- Credentials go in `secrets.h` (ESP8266/ESP32 sketches) or `sdcard/config.jsn` (PyPortal); both are gitignored. Example files with placeholder values are committed instead.
- Device identity is derived from chip ID (`ESP.getChipId()` on ESP8266, `ESP.getEfuseMac()` on ESP32, WiFi MAC on PyPortal) so multiple devices don't collide on the broker.
- MQTT auto-discovery publishes retained config payloads to `homeassistant/<domain>/<deviceId>/…/config`. If you rename a zone or remove a device, clean up stale HA entities by publishing an empty retained message to the old discovery topic, then removing the entity from HA's MQTT integration page.
- `mqtt.setBufferSize()` must be called in `setup()` — discovery payloads exceed PubSubClient's 256-byte default. Current values: 512 B (temp sensor, PyPortal) and 1024 B (lightstrip).

## Toolchain

All sketches use `arduino-cli`. Find the connected board's port before uploading:

```bash
arduino-cli board list
```

See individual sketch CLAUDE.md files for exact compile/upload/monitor commands, required library lists, and board package URLs.
