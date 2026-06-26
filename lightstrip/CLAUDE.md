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

1. Copy `secrets.h.example` → `secrets.h` and fill in credentials. Fields: `WIFI_SSID`, `WIFI_PASSWORD`, `MQTT_HOST` (default `homeassistant.local`), `MQTT_PORT` (default `1883`), `MQTT_USER`, `MQTT_PASS`.
2. In `lightstrip.ino`, uncomment exactly one mode (`MODE_MULTI_STRIP` or `MODE_SEGMENTS`).
3. Edit the `STRIPS[]` or `SEGMENTS[]` array to match your hardware (names, pins, counts).
4. For `MODE_SEGMENTS`, set `SEG_PIN` and `SEG_NUM_LEDS` to match the physical strip.
5. Optionally lower `MAX_BRIGHTNESS_SCALE` (default `1.0f`) to cap peak current — e.g. `0.5f` limits output to 50% max brightness across all zones.

## Required Libraries (install via Arduino Library Manager)

- **Adafruit NeoPixel** by Adafruit
- **ArduinoJson** by Benoit Blanchon (v6.x — `StaticJsonDocument` API)
- **PubSubClient** by Nick O'Leary
- **Adafruit MCP9808** by Adafruit (required even if sensor is not installed)
- **WiFi**, **Wire** (bundled with the ESP32 board package)

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

- **`deviceId`**: `"huzzah32_" + hex(ESP.getEfuseMac() >> 24)` — e.g. `huzzah32_a3f2c1b0`. Appears in all MQTT topic paths and HA `unique_id` fields; used to distinguish multiple devices on the same broker.
- **Zones**: both modes share the same runtime concept of a "zone" — an independently controllable light with its own `ZoneState` (on/off, brightness, R, G, B) and its own MQTT topics.
- **`NUM_ZONES`**: derived at compile time from the length of `STRIPS[]` or `SEGMENTS[]` via `sizeof`. Used to size all global arrays.
- **`ZoneState zoneState[NUM_ZONES]`**: per-zone runtime state. Zones initialize `on=false`, brightness=255, color white (255, 255, 255) — off but ready to turn on at full white.
- **`applyZone(i)`**: writes the zone's current state to the NeoPixel hardware. In `MODE_MULTI_STRIP` each zone has its own `Adafruit_NeoPixel*` (heap-allocated in setup); in `MODE_SEGMENTS` all zones share one instance, and `pixelStrip.show()` re-transmits the full physical strip buffer — updating one segment momentarily redraws all others too (harmless but relevant if adding timed effects).
- **MQTT topics** (all per-zone, `i` = 0-based zone index):
  - Subscribe: `home/<deviceId>/light/<i>/set` — receives JSON commands from HA
  - Publish (retained): `home/<deviceId>/light/<i>/state` — current zone state
  - Publish (retained): `homeassistant/light/<deviceId>_zone<i>/config` — HA auto-discovery
- **JSON schema**: HA's JSON light schema. Command payloads carry `state` ("ON"/"OFF"), `brightness` (0–255), and `color` (`{"r":…,"g":…,"b":…}`). Partial updates (e.g., brightness-only) are handled — missing keys leave current values unchanged. State payloads must also include `"color_mode": "rgb"` — without it HA does not expose the color picker even if the discovery config declares RGB support. The discovery config uses `"supported_color_modes": ["rgb"]`; the old `"rgb": true` / `"brightness": true` fields are deprecated and ignored by current HA versions.
- **Brightness**: applied by first clamping `s.brightness` via `MAX_BRIGHTNESS_SCALE` to get `bri`, then scaling each channel as `s.r * bri / 255`. Uses manual channel math rather than `NeoPixel.setBrightness()`, which modifies the pixel buffer destructively.
- **`mqtt.setBufferSize(1024)`**: required — the discovery payload exceeds PubSubClient's 256-byte default.
- **Reconnect**: both `connectWiFi()` and `connectMQTT()` are blocking — the device is unresponsive to incoming MQTT commands during reconnection. `connectMQTT()` retries every 5 s. On every reconnect the sketch re-subscribes all command topics, re-publishes all discovery configs (zones + temp + diagnostics), and re-publishes all zone states so HA stays in sync.
- **Half-open link healing**: a stale/half-open TCP socket can leave the device "connected but deaf" — `mqtt.connected()` stays true while no commands arrive — which previously required a power cycle. Two defenses: MQTT keepalive is 15 s (faster dead-link detection by PubSubClient), and `loop()` does a **periodic forced reconnect** every `MQTT_FORCE_RECONNECT_MS` (default 15 min) via `mqtt.disconnect()`, so a deaf socket self-heals within one interval. Set `MQTT_FORCE_RECONNECT_MS` to 0 to disable. The older 5-min `lastMqttOkMs` watchdog (reboots if `mqtt.loop()` can't stay healthy) remains as a backstop.
- **Diagnostics**: `publishDiag()` publishes a retained JSON payload to `home/<deviceId>/diag` every `DIAG_INTERVAL` (default 60 s) and on each connect, exposing five HA `diagnostic` sensors: **Last Reset Reason** (`esp_reset_reason()` mapped via `resetReasonName()` — e.g. `Brownout`, `Task WDT`, `Panic/Crash`, `Software`, `Power-on`), **Uptime**, **WiFi Signal** (RSSI), **Free Heap**, **MQTT Reconnects**. This is the primary tool for debugging an inaccessible unit: after any lockup/reboot, HA shows *why* it reset (power vs. firmware) and whether heap is leaking or reconnects are climbing. All five share the one diag JSON topic via `value_template`.
- **MCP9808 (optional)**: detected at runtime via `mcp9808.begin()` over I2C (HUZZAH32: SDA=23, SCL=22). If found, a temperature sensor entity is auto-discovered under the same HA device as the light zones and published every `TEMP_READ_INTERVAL` ms (default 30 s) to `home/<deviceId>/sensor/temperature`. Temperature publishes are **not retained** (unlike zone state/discovery), so HA will show "unavailable" after a reboot until the first publish fires. The `Adafruit_MCP9808` library must be installed regardless of whether the sensor is physically present.

## Enclosure

`enclosure/lightstrip.scad` is a parametric OpenSCAD model for a 3D-printable enclosure. Open with OpenSCAD to adjust dimensions and export STL for printing.

## Home Assistant Setup

1. Install the **Mosquitto broker** add-on in HA and enable the MQTT integration.
2. Flash the device — HA will auto-discover one light entity per zone, all grouped under "HUZZAH32 LED Strip".
3. Each entity has an on/off toggle, brightness slider, and RGB color picker.

**Stale entities:** Discovery payloads are retained on the broker. If you rename a zone, change `unique_id`, or remove a zone, the old entity persists in HA. To clean up: use an MQTT client (e.g. MQTT Explorer) to publish an empty retained message to the old `homeassistant/light/<deviceId>_zone<i>/config` topic, then remove the entity from HA's MQTT integration page.
