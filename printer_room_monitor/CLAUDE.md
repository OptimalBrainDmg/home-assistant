# CLAUDE.md

Always-on (source-powered) printer room monitor. Reads all sensors every 10 seconds and publishes to MQTT via a persistent connection. WiFi and MQTT connections are kept alive between readings — do NOT change this to a connect/disconnect-per-cycle pattern; at a 10 s interval that would spend more time reconnecting than measuring.

## Hardware

**Seeed XIAO ESP32C6**
- FQBN: `esp32:esp32:XIAO_ESP32C6`
- I2C: SDA=D4 (GPIO22), SCL=D5 (GPIO23) — board defaults

**Modules:**

| Module | Interface | What it measures | Pin / Address |
|--------|-----------|-----------------|---------------|
| Adafruit SHT40 breakout | I2C 0x44 | Temperature (°C), Humidity (%RH) | SDA/SCL |
| DFRobot Fermion MEMS Smoke Detection Sensor | Analog | Smoke / EtOH (raw mV) | A0 (GPIO2, ADC1_CH2) |

**Smoke sensor wiring — CRITICAL:**
Wire the sensor VCC to the XIAO's **3V3 pin**, not 5V. The analog output is ratiometric to VCC; at 5V supply it can swing above 3.3V and will saturate or damage the ADC input. The sensor spec supports 3.3–5V, so 3.3V is safe and correct here.

The sensor is on ADC1, which coexists with WiFi on the ESP32-C6 — the classic "ADC2 unusable while WiFi is on" issue does not apply.

**Smoke sensor output:**
Raw millivolts are published as-is. Set alert thresholds in Home Assistant automations by observing baseline mV in clean air vs. elevated readings near the printer. Approximate PPM conversion requires knowing the onboard load resistor value (typically 10 kΩ) and a clean-air R0 calibration — not implemented; raw mV is sufficient for threshold-based alerting.

**Smoke sensor warmup:**
MEMS gas sensors need ~30 s from power-on before readings stabilize. `setup()` waits out any remaining warmup time after WiFi/MQTT connect (which partially overlaps). First readings after a cold start may read high; this is normal.

## Power

Source-powered only (USB 5V). Not suitable for battery use — always-on is required for smoke monitoring.

## Required Libraries

- **Adafruit SHT4x** — temp/humidity; install from Arduino Library Manager
- **Adafruit Unified Sensor** — dependency of Adafruit SHT4x
- **PubSubClient** — MQTT
- **ArduinoJson v7** — `JsonDocument` API (no template size parameter)

## Build & Flash

```bash
cp secrets.h.example secrets.h  # then fill in credentials
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C6 printer_room_monitor/
arduino-cli compile --upload --fqbn esp32:esp32:XIAO_ESP32C6 --port <port> printer_room_monitor/
arduino-cli monitor --port <port> --config baudrate=115200
```

Requires ESP32 core 3.0.0+ (for ESP32-C6 support).

## Configuration

Copy `secrets.h.example` to `secrets.h` and set WiFi + MQTT broker credentials.

## MQTT Topics

- Device ID: `"smoke_" + lower 4 bytes of EfuseMac as hex` (e.g. `smoke_a1b2c3d4`)
- State topic: `home/<deviceId>/state`
- Discovery topics: `homeassistant/sensor/<deviceId>/<entity>/config` (retained)
- MQTT buffer size: 512 bytes

## HA Entities

All published to a single state topic as a flat JSON object; 3 entities auto-discovered:

| Entity | JSON key | device_class | Unit |
|--------|----------|-------------|------|
| Temperature | `temperature` | `temperature` | °C |
| Humidity | `humidity` | `humidity` | % |
| Smoke | `smoke_mv` | *(none — HA smoke device_class is binary only)* | mV |

`expire_after` is set to 60 s — HA marks entities unavailable if no update arrives within that window.

## Architecture

`setup()` initializes sensors and establishes persistent WiFi + MQTT connections; `loop()` runs the read/publish cycle every 10 s, maintaining connections across iterations.

**setup():**
1. `Wire.begin()` → I2C scan
2. `sht4.begin()` → `setPrecision(HIGH)` / `setHeater(NO_HEATER)`
3. `mqtt.setServer()` / `mqtt.setBufferSize(512)`
4. `connectWiFi()` → `connectMQTT()` → publish retained discovery configs
5. `delay()` for any remaining MEMS warmup time

**loop():**
1. `readSHT40()` — `getEvent()` for T/H
2. `readSmokeMv()` — `analogReadMilliVolts(A0)`
3. `connectWiFi()` / `connectMQTT()` — no-ops if already connected; reconnect otherwise and re-publish discovery
4. Publish single JSON state payload to `stateTopic`
5. `mqtt.loop()` to service keep-alive
6. `delay(10000)` — 10 s interval
