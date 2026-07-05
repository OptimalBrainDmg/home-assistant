# CLAUDE.md

Always-on (source-powered) printer room monitor. Reads all sensors every 10 seconds and publishes to MQTT via a persistent connection. WiFi and MQTT connections are kept alive between readings — do NOT change this to a connect/disconnect-per-cycle pattern; at a 10 s interval that would spend more time reconnecting than measuring.

## Hardware

**Seeed XIAO ESP32C6**
- FQBN: `esp32:esp32:XIAO_ESP32C6:PartitionScheme=huge_app` (required — BLE + WiFi together exceed the default 1.2 MB app partition)
- I2C: SDA=D4 (GPIO22), SCL=D5 (GPIO23) — board defaults

**Modules:**

| Module | Interface | What it measures | Pin / Address |
|--------|-----------|-----------------|---------------|
| Adafruit SHT40 breakout | I2C 0x44 | Temperature (°C), Humidity (%RH) | SDA/SCL |
| DFRobot Fermion MEMS Smoke Detection Sensor | Analog | Smoke / EtOH (raw mV) | A0 (GPIO2, ADC1_CH2) |
| Xiaomi LYWSD03MMC (×N) | BLE passive scan | Temperature, Humidity, Battery | BTHome service UUID 0xFCD2 |

**Smoke sensor wiring — CRITICAL:**
Wire the sensor VCC to the XIAO's **3V3 pin**, not 5V. The analog output is ratiometric to VCC; at 5V supply it can swing above 3.3V and will saturate or damage the ADC input. The sensor spec supports 3.3–5V, so 3.3V is safe and correct here.

The sensor is on ADC1, which coexists with WiFi on the ESP32-C6 — the classic "ADC2 unusable while WiFi is on" issue does not apply.

**Smoke sensor output:**
Raw millivolts are published as-is. Set alert thresholds in Home Assistant automations by observing baseline mV in clean air vs. elevated readings near the printer. Approximate PPM conversion requires knowing the onboard load resistor value (typically 10 kΩ) and a clean-air R0 calibration — not implemented; raw mV is sufficient for threshold-based alerting.

**Smoke sensor warmup:**
MEMS gas sensors need ~30 s from power-on before readings stabilize. `setup()` waits out any remaining warmup time after WiFi/MQTT connect (which partially overlaps). First readings after a cold start may read high; this is normal.

**BLE sensors (Xiaomi LYWSD03MMC):**
Flash each sensor with pvvx ATC custom firmware via https://pvvx.github.io/ATC_MiThermometer/TelinkMiFlasher.html (Chrome, WebBLE). Set advertising format to **BTHome**, unencrypted. Note: firmware v2.1.1_0159 requires Mi Home registration first to obtain a bind key before the WebBLE flasher can proceed. After flashing, add each sensor's MAC address to `secrets.h`.

## Power

Source-powered only (USB 5V). Not suitable for battery use — always-on is required for smoke monitoring.

## Required Libraries

- **Adafruit SHT4x** — temp/humidity; install from Arduino Library Manager
- **Adafruit Unified Sensor** — dependency of Adafruit SHT4x
- **PubSubClient** — MQTT
- **ArduinoJson v7** — `JsonDocument` API (no template size parameter)
- **BLE** — built into ESP32 Arduino core (no separate install needed)

## Build & Flash

```bash
cp secrets.h.example secrets.h  # fill in WiFi, MQTT credentials, and BLE sensor MACs
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C6:PartitionScheme=huge_app printer_room_monitor/
arduino-cli compile --upload --fqbn esp32:esp32:XIAO_ESP32C6:PartitionScheme=huge_app --port <port> printer_room_monitor/
arduino-cli monitor --port <port> --config baudrate=115200
```

Requires ESP32 core 3.0.0+ (for ESP32-C6 support).

## Configuration

Copy `secrets.h.example` to `secrets.h` and set:
- WiFi + MQTT broker credentials
- `BLE_SENSOR_MACS[]` — MAC address of each LYWSD03MMC, uppercase with colons
- `BLE_SENSOR_LABELS[]` — friendly name shown in HA (e.g. `"Filament S1"`)
- `BLE_SENSOR_COUNT` — number of sensors

## MQTT Topics

**Printer room monitor (smoke/temp/humidity):**
- Device ID: `"smoke_" + lower 4 bytes of EfuseMac as hex` (e.g. `smoke_a1b2c3d4`)
- State topic: `home/<deviceId>/state`
- Discovery: `homeassistant/sensor/<deviceId>/<entity>/config` (retained)

**BLE filament sensors:**
- Device ID: `"filament_" + last 3 MAC bytes lowercase no colons` (e.g. `filament_4432e7`)
- State topic: `home/<filamentId>/state`
- Discovery: `homeassistant/sensor/<filamentId>/<entity>/config` (retained)
- MQTT buffer size: 512 bytes (per-message; large enough for all payload types)

## HA Entities

**Printer room monitor** — single state topic, 3 entities:

| Entity | JSON key | device_class | Unit |
|--------|----------|-------------|------|
| Temperature | `temperature` | `temperature` | °C |
| Humidity | `humidity` | `humidity` | % |
| Smoke | `smoke_mv` | *(none)* | mV |

**Each BLE sensor** — own state topic, own HA device, 3 entities:

| Entity | JSON key | device_class | Unit |
|--------|----------|-------------|------|
| Temperature | `temperature` | `temperature` | °C |
| Humidity | `humidity` | `humidity` | % |
| Battery | `battery` | `battery` | % |

`expire_after` is set to 60 s on all entities — HA marks unavailable if no update arrives within that window.

## Architecture

`setup()` initializes sensors and establishes persistent WiFi + MQTT connections; `loop()` runs the read/publish cycle targeting 10 s total, maintaining connections across iterations.

**setup():**
1. `Wire.begin()` → I2C scan
2. `sht4.begin()` → `setPrecision(HIGH)` / `setHeater(NO_HEATER)`
3. `BLEDevice::init("")` → passive scan configured
4. `mqtt.setServer()` / `mqtt.setBufferSize(512)`
5. `connectWiFi()` → `connectMQTT()` → publish retained discovery configs (smoke + all BLE sensors)
6. `delay()` for any remaining MEMS warmup time

**loop():**
1. `readSHT40()` — `getEvent()` for T/H
2. `readSmokeMv()` — `analogReadMilliVolts(A0)`
3. `mqtt.loop()` — service keep-alive before blocking scan
4. `scanBle()` — 3 s blocking passive BLE scan; parses BTHome advertisements for known MACs; resets `valid` flag each cycle so stale readings are never re-published
5. `mqtt.loop()` — service keep-alive after blocking scan
6. `connectWiFi()` / `connectMQTT()` — no-ops if connected; reconnect + re-publish discovery otherwise
7. Publish smoke/temp/humidity state; publish BLE sensor states for any `valid` readings
8. `mqtt.loop()` — final keep-alive service
9. `delay()` for remainder of 10 s cycle

**BTHome parser (`parseBTHome`):**
BTHome v2 has no per-object length byte — length is implied by object ID. Byte 0 = device info/flags (bit 0 = encryption; must be 0). Objects: `0x01` battery uint8, `0x02` temperature signed int16 ×0.01 °C, `0x03` humidity uint16 ×0.01 %RH. Little-endian. Unknown object ID aborts parsing (no way to resync without length).

Serial prints raw hex of each BTHome advertisement — keep this in place until parsing is verified against a real device.
