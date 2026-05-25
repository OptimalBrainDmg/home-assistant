# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Arduino firmware for an Adafruit HUZZAH32 ESP32 Feather that drives WS2812B LED strips and exposes each zone to Home Assistant as an independent MQTT light (on/off, brightness, RGB color). Supports two mutually exclusive modes selected at compile time.

## Modes

### MODE_MULTI_STRIP
Multiple physically separate strips, each connected to its own data pin. Each strip is an independent HA light entity.

```cpp
struct StripConfig { const char* name; uint8_t pin; uint16_t numLeds; };
static const StripConfig STRIPS[] = {
  { "Desk Strip",  21, 30 },
  { "Shelf Strip", 14, 15 },
};
```

### MODE_SEGMENTS
One physical strip wired to a single data pin, divided into named segments by LED index range. Each segment is an independent HA light entity; changing one segment does not disturb the others.

```cpp
#define SEG_PIN      21
#define SEG_NUM_LEDS 60

struct SegmentConfig { const char* name; uint16_t start; uint16_t count; };
static const SegmentConfig SEGMENTS[] = {
  { "Top Shelf",    0,  20 },
  { "Middle Shelf", 20, 20 },
  { "Bottom Shelf", 40, 20 },
};
```

## Hardware

| Component | Connection |
|-----------|-----------|
| Adafruit HUZZAH32 ESP32 Feather | ESP32-based, 3.3V logic |
| WS2812B strip(s) | Data → configurable GPIO pin(s), 5V power, GND common with HUZZAH32 |

Available data pins on HUZZAH32: 14, 15, 21, 22, 25, 26, 27, 32, 33. Avoid GPIO 0 (boot button), 13 (onboard red LED), and 34–39 (input-only, no output).

**Power note:** USB (500 mA) can safely drive roughly 10–15 LEDs at full white. For longer strips use an external 5V supply; connect its GND to the HUZZAH32 GND.

**Level shifting:** WS2812B data line expects 5V logic; most strips accept 3.3V but a 74AHCT125 level shifter improves reliability for longer runs.

## Configuration Before Flashing

1. Copy `secrets.h.example` → `secrets.h` and fill in WiFi and MQTT credentials.
2. In `lightstrip.ino`, uncomment exactly one mode (`MODE_MULTI_STRIP` or `MODE_SEGMENTS`).
3. Edit the `STRIPS[]` or `SEGMENTS[]` array to match your hardware (names, pins, counts).
4. For `MODE_SEGMENTS`, set `SEG_PIN` and `SEG_NUM_LEDS` to match the physical strip.

## Required Libraries (install via Arduino Library Manager)

- **Adafruit NeoPixel** by Adafruit
- **ArduinoJson** by Benoit Blanchon (v6.x — `StaticJsonDocument` API)
- **PubSubClient** by Nick O'Leary
- **WiFi** (bundled with the ESP32 board package)

Board package: `esp32` by Espressif — add `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json` to Board Manager URLs. Select board: **Adafruit ESP32 Feather**.

## Build & Flash

```bash
# Compile
arduino-cli compile --fqbn esp32:esp32:featheresp32 .

# Upload (find port with: arduino-cli board list)
arduino-cli upload --fqbn esp32:esp32:featheresp32 --port /dev/cu.usbserial-* .

# Monitor serial at 115200 baud
arduino-cli monitor --port /dev/cu.usbserial-* --config baudrate=115200
```

## Architecture

Single-file sketch (`lightstrip.ino`). All user configuration lives in the block at the top of the file between the `CONFIGURATION` banners.

- **Zones**: both modes share the same runtime concept of a "zone" — an independently controllable light with its own `ZoneState` (on/off, brightness, R, G, B) and its own MQTT topics.
- **`NUM_ZONES`**: derived at compile time from the length of `STRIPS[]` or `SEGMENTS[]` via `sizeof`. Used to size all global arrays.
- **`ZoneState zoneState[NUM_ZONES]`**: per-zone runtime state. All zones start off at full brightness (255) white (255, 255, 255).
- **`applyZone(i)`**: writes the zone's current state to the NeoPixel hardware. In `MODE_MULTI_STRIP` each zone has its own `Adafruit_NeoPixel*` (heap-allocated in setup); in `MODE_SEGMENTS` all zones share one instance, and only the relevant LED range is updated before calling `show()`.
- **MQTT topics** (all per-zone, `i` = 0-based zone index):
  - Subscribe: `home/<deviceId>/light/<i>/set` — receives JSON commands from HA
  - Publish (retained): `home/<deviceId>/light/<i>/state` — current zone state
  - Publish (retained): `homeassistant/light/<deviceId>_zone<i>/config` — HA auto-discovery
- **JSON schema**: HA's JSON light schema. Command payloads carry `state` ("ON"/"OFF"), `brightness` (0–255), and `color` (`{"r":…,"g":…,"b":…}`). Partial updates (e.g., brightness-only) are handled — missing keys leave current values unchanged. State payloads must also include `"color_mode": "rgb"` — without it HA does not expose the color picker even if the discovery config declares RGB support. The discovery config uses `"supported_color_modes": ["rgb"]`; the old `"rgb": true` / `"brightness": true` fields are deprecated and ignored by current HA versions.
- **Brightness**: applied by scaling RGB channels manually (`s.r * s.brightness / 255`) rather than `NeoPixel.setBrightness()`, which modifies the pixel buffer destructively.
- **`mqtt.setBufferSize(1024)`**: required — the discovery payload exceeds PubSubClient's 256-byte default.
- **Reconnect**: on every MQTT reconnect the sketch re-subscribes all command topics, re-publishes all discovery configs, and re-publishes all zone states so HA stays in sync.

## Home Assistant Setup

1. Install the **Mosquitto broker** add-on in HA and enable the MQTT integration.
2. Flash the device — HA will auto-discover one light entity per zone, all grouped under "HUZZAH32 LED Strip".
3. Each entity has an on/off toggle, brightness slider, and RGB color picker.

**Stale entities:** Discovery payloads are retained on the broker. If you rename a zone, change `unique_id`, or remove a zone, the old entity persists in HA. To clean up: use an MQTT client (e.g. MQTT Explorer) to publish an empty retained message to the old `homeassistant/light/<deviceId>_zone<i>/config` topic, then remove the entity from HA's MQTT integration page.
