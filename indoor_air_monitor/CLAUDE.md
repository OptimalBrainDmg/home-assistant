# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Always-on (source-powered) indoor air quality station. Reads all sensors every 5 minutes, publishes to MQTT, then delays. Both sensors stay continuously powered and initialized — the STCC4 runs in continuous measurement mode and the ENS160 in STANDARD_MODE at all times.


## Hardware

**Seeed XIAO ESP32C6**
- FQBN: `esp32:esp32:XIAO_ESP32C6`
- I2C: SDA=D4, SCL=D5 (board defaults)
- Both modules connect via STEMMA QT / Qwiic (JST SH, same connector as XIAO)

**Modules:**

| Module | Chips | What it measures | I2C address(es) |
|--------|-------|-----------------|-----------------|
| Adafruit 6478 | STCC4 + SHT41 | CO2 (ppm), Temperature (°C), Humidity (%RH) | STCC4: 0x64 |
| ENS160 breakout | ENS160 | TVOC (ppb), eCO2 (ppm, estimated) | 0x53 (ADDR high) |

**Adafruit 6478 notes:**
- The STCC4 and SHT41 are co-located on the PCB and wired together — STCC4's dedicated I2C controller (SDA_C/SCL_C) reads the SHT41 directly for its internal temp/humidity compensation. This is the intended use case; no manual wiring of the compensation connection required.
- SHT41 is wired only to the STCC4's internal I2C controller — it does **not** appear on the main I2C bus. Temperature and humidity are obtained via `readMeasurement()` on the STCC4, not through a separate SHT41 driver.
- STCC4 accuracy: ±100 ppm ±10% m.v. over 400–5000 ppm range
- SHT41 accuracy: ±0.2°C, ±1.8% RH
- STCC4 operating temperature: 10–40°C only (indoor use only)
- STCC4 average current: 950 µA; peak: 4.2 mA (note: Adafruit product page says "below 100 µA" — this appears to be idle/standby current; use Sensirion datasheet spec of 950 µA avg for power budgeting)
- Adafruit Arduino library available (covers both chips)

**STCC4 measurement strategy:**
The STCC4 must be initialized with `begin()` + `enableContinuousMeasurement(true)` once at power-on and then left running. Do NOT call `begin()` between readings — it sends a soft reset that tears down continuous mode, after which `readMeasurement()` returns a stale default value (390 ppm) for CO2 while T/H from the SHT41 still update correctly. Deep sleep and light sleep both cause C++ global object destructors/constructors to run, resetting the library's internal `i2c_dev` pointer and forcing a re-`begin()` on every wake. For this reason the sketch uses `delay()` instead of any sleep mode — this keeps the sensor fully initialized across the loop.

**ENS160 strategy:**
Do NOT power-cycle the ENS160 — doing so resets its resistance baseline and triggers a 3-minute warm-up. The DFRobot_ENS160 library's `begin()` calls `setPWRMode(ENS160_STANDARD_MODE)` internally; no separate mode-set call is needed.

**Why both ENS160 and STCC4:**
ENS160 eCO2 is *estimated* from TVOC gas resistance — useful as a relative indicator but unreliable for absolute CO2 levels and can be skewed by cooking smells, cleaning products, etc. STCC4 measures true CO2. Both together give complementary data.

## Power
Source-powered only (USB 5V). Not suitable for battery use in the current always-on configuration.

## Required Libraries

- **Adafruit STCC4** — CO2, temperature & humidity (SHT41 is internal to STCC4, not a separate driver); install from Arduino Library Manager
- **DFRobot_ENS160** — VOC/eCO2 sensor driver; install from Arduino Library Manager
- **PubSubClient** — MQTT
- **ArduinoJson v7** — `JsonDocument` API (no template size parameter)

## Build & Flash

```bash
cp secrets.h.example secrets.h  # then fill in credentials
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C6 indoor_air_monitor/
arduino-cli compile --upload --fqbn esp32:esp32:XIAO_ESP32C6 --port <port> indoor_air_monitor/
arduino-cli monitor --port <port> --config baudrate=115200
```

Requires ESP32 core 3.0.0+ (for ESP32-C6 support). Uses the globally installed core — no `sketch.yaml` pin needed.

## Configuration

Copy `secrets.h.example` to `secrets.h` and set WiFi + MQTT broker credentials.

## MQTT Topics

- Device ID: `"air_" + lower 4 bytes of EfuseMac as hex` (e.g. `air_a1b2c3d4`)
- State topic: `home/<deviceId>/state`
- Discovery topics: `homeassistant/<domain>/<deviceId>/<entity>/config` (retained)
- MQTT buffer size: 1024 bytes (set via `mqtt.setBufferSize(1024)` before connecting)

## HA Entities

All published to a single state topic as a flat JSON object; 5 entities auto-discovered:

| Entity | JSON key | Source | device_class | Unit |
|--------|----------|--------|-------------|------|
| Temperature | `temperature` | SHT41 (via Adafruit 6478) | `temperature` | °C |
| Humidity | `humidity` | SHT41 (via Adafruit 6478) | `humidity` | % |
| CO2 | `co2` | STCC4 (via Adafruit 6478) | `carbon_dioxide` | ppm |
| TVOC | `tvoc` | ENS160 | `volatile_organic_compounds_parts` | ppb |
| eCO2 | `eco2` | ENS160 | `carbon_dioxide` | ppm |

## Architecture

`setup()` initializes sensors once; `loop()` runs the read/publish/delay cycle continuously.

**setup():**
1. `Wire.begin()` → I2C scan
2. `stcc4.begin()` → `stcc4.enableContinuousMeasurement(true)` → `ens160.begin()`
3. `ens160.setTempAndHum(25.0, 50.0)` (placeholder) → `delay(1100)` for first measurement cycle
4. `mqtt.setServer()` / `mqtt.setBufferSize(1024)`

**loop():**
1. `readSTCC4()` — `readMeasurement()` returns CO2 + T/H from the continuously-running sensor
2. `readENS160()` — feeds STCC4 T/H to `setTempAndHum()`, then reads `getTVOC()` / `getECO2()`
3. `connectWiFi()` → 20s timeout; skip publish on failure
4. `connectMQTT()` → publish retained discovery configs
5. Publish single JSON state payload to `stateTopic`
6. `mqtt.loop()` 5× to flush, disconnect WiFi/MQTT
7. `delay(300000)` — 5-minute interval before next reading
