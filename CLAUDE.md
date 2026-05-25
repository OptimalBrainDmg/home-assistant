# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Structure

Collection of Arduino sketches for ESP8266/ESP32-based sensors and actuators that integrate with Home Assistant via MQTT. Each sketch lives in its own subdirectory and has its own `CLAUDE.md` with hardware pinout, required libraries, build/flash commands, and architecture notes.

Current sketches:
- `temp_humidity_sensor/` — LOLIN D1 Mini V4 reading AM2320 or SHTC3 over I2C, publishing to HA via MQTT auto-discovery
- `lightstrip/` — Adafruit HUZZAH32 ESP32 Feather driving a WS2812B LED strip, exposed to HA as an MQTT light (on/off, brightness, RGB color)

## Conventions Across All Sketches

- Credentials (WiFi SSID/password, MQTT host/user/pass) go in `secrets.h`, which is gitignored. A `secrets.h.example` with placeholder values is committed instead.
- Device identity is derived from chip ID (`ESP.getChipId()` on ESP8266, `ESP.getEfuseMac()` on ESP32) so multiple flashed devices don't collide on the broker.
- MQTT auto-discovery targets Home Assistant's `homeassistant/sensor/<deviceId>/…/config` topic with retained messages.

## Toolchain

Sketches target either ESP8266 (LOLIN D1 Mini) or ESP32 (Adafruit HUZZAH32). See individual sketch CLAUDE.md files for exact `arduino-cli` compile/upload/monitor commands and required library lists.
