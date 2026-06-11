# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Battery-powered indoor air quality station. Wakes every 5 minutes, reads all sensors, publishes to MQTT, then deep-sleeps. ENS160 remains always-on in STANDARD_MODE during sleep to preserve its baseline and avoid the 3-minute warm-up penalty.


## Hardware

**Seeed XIAO ESP32C6**
- FQBN: `esp32:esp32:XIAO_ESP32C6`
- I2C: SDA=D4, SCL=D5 (board defaults)
- Both modules connect via STEMMA QT / Qwiic (JST SH, same connector as XIAO)

**Modules:**

| Module | Chips | What it measures | I2C address(es) | Always-on during sleep? |
|--------|-------|-----------------|-----------------|------------------------|
| Adafruit 6478 | STCC4 + SHT41 | CO2 (ppm), Temperature (°C), Humidity (%RH) | STCC4: 0x64 | No — single-shot, 500ms |
| ENS160 breakout | ENS160 | TVOC (ppb), eCO2 (ppm, estimated) | 0x53 (ADDR high) | **Yes — STANDARD_MODE** |

**Adafruit 6478 notes:**
- The STCC4 and SHT41 are co-located on the PCB and wired together — STCC4's dedicated I2C controller (SDA_C/SCL_C) reads the SHT41 directly for its internal temp/humidity compensation. This is the intended use case; no manual wiring of the compensation connection required.
- SHT41 is wired only to the STCC4's internal I2C controller — it does **not** appear on the main I2C bus. Temperature and humidity are obtained via `readMeasurement()` on the STCC4, not through a separate SHT41 driver.
- STCC4 accuracy: ±100 ppm ±10% m.v. over 400–5000 ppm range
- SHT41 accuracy: ±0.2°C, ±1.8% RH
- STCC4 operating temperature: 10–40°C only (indoor use only)
- STCC4 average current: 950 µA; peak: 4.2 mA (note: Adafruit product page says "below 100 µA" — this appears to be idle/standby current; use Sensirion datasheet spec of 950 µA avg for power budgeting)
- Adafruit Arduino library available (covers both chips)

**ENS160 power strategy:**
The XIAO 3.3V rail stays powered during ESP32-C6 deep sleep, so the ENS160 continues running in STANDARD_MODE. Do NOT power-cycle the ENS160 between wake cycles — doing so resets its resistance baseline and triggers a 3-minute warm-up. The DFRobot_ENS160 library's `begin()` calls `setPWRMode(ENS160_STANDARD_MODE)` internally, so no separate mode-set call is needed. `RTC_DATA_ATTR bool firstBoot` is used only as a log marker to distinguish first boot from warm wake — it does not gate initialization behavior.

**Why both ENS160 and STCC4:**
ENS160 eCO2 is *estimated* from TVOC gas resistance — useful as a relative indicator but unreliable for absolute CO2 levels and can be skewed by cooking smells, cleaning products, etc. STCC4 measures true CO2. Both together give complementary data.

## Power Budget (2000 mAh LiPo, 5-min cycle)

| Source | Current | Notes |
|--------|---------|-------|
| ENS160 STANDARD_MODE | ~7–25 mA | Dominates sleep budget; wide range depends on measurement phase |
| XIAO board (LDO + LED) | ~1–3 mA | Remove power LED for battery builds |
| STCC4 (sleep) | ~950 µA | Negligible vs ENS160 |
| ESP32-C6 deep sleep | ~20 µA | Negligible |
| WiFi wake (~15s active) | ~4.5 mAh/hr amortized | 12 wakes/hour |
| **Estimated runtime** | **~3–7 days** | ENS160 STANDARD_MODE current dominates; remove LED and reduce sleep interval to improve |

> **Note:** Original estimate assumed "LP mode" (~3 mA) that does not exist in the DFRobot_ENS160 library. STANDARD_MODE is the only active measurement mode available.

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

- Device ID: `"air_" + lower 3 bytes of EfuseMac as hex` (e.g. `air_a1b2c3`)
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

Everything in `setup()`; `loop()` is empty. Wake cycle:
1. `Wire.begin()` → init STCC4 and ENS160 (`begin()` sets STANDARD_MODE automatically)
2. Read all sensors before WiFi (avoids radio self-heating skew on SHT41):
   - `readSTCC4(co2, temp, humid)` — `sleepMode(false)` → `measureSingleShot()` → `delay(500)` → `readMeasurement()` (returns CO2 + T/H from internal SHT41) → `sleepMode(true)`
   - `readENS160()` — feeds STCC4 temp/humid to `setTempAndHum()`, then reads `getTVOC()` / `getECO2()`
3. `connectWiFi()` → 20s timeout; `goToSleep()` on failure
4. `connectMQTT()` → publish retained discovery configs; `goToSleep()` on failure
5. Publish single JSON state payload to `stateTopic`
6. Call `mqtt.loop()` 5× to flush outbound messages, then `goToSleep()`

`goToSleep()` disconnects MQTT and WiFi, arms the timer wakeup, then calls `esp_deep_sleep_start()`. ENS160 is left in STANDARD_MODE — 3.3V rail stays on.
