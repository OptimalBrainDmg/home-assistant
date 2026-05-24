# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Structure

Collection of Arduino sketches for ESP8266-based sensors that integrate with Home Assistant via MQTT. Each sketch lives in its own subdirectory and has its own `CLAUDE.md` with hardware pinout, required libraries, build/flash commands, and architecture notes.

Current sketches:
- `temp_humidity_sensor/` — LOLIN D1 Mini V4 reading AM2320 or SHTC3 over I2C, publishing to HA via MQTT auto-discovery
- `lightstrip/` — LOLIN D1 Mini V4 driving a WS2812B LED strip, exposed to HA as an MQTT light (on/off, brightness, RGB color)

## Conventions Across All Sketches

- Credentials (WiFi SSID/password, MQTT host/user/pass) go in `secrets.h`, which is gitignored. A `secrets.h.example` with placeholder values is committed instead.
- Device identity is derived from `ESP.getChipId()` so multiple flashed devices don't collide on the broker.
- MQTT auto-discovery targets Home Assistant's `homeassistant/sensor/<deviceId>/…/config` topic with retained messages.

## Toolchain

All sketches target ESP8266 (LOLIN D1 Mini). See individual sketch CLAUDE.md files for exact `arduino-cli` compile/upload/monitor commands and required library lists.
