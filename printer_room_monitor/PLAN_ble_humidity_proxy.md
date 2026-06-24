# Plan: BLE Humidity Proxy for Filament Storage Boxes

## Goal

Add passive BLE scanning to the printer_room_monitor so it bridges
Xiaomi LYWSD03MMC sensors (flashed with ATC firmware) into HA via MQTT.
No second device needed — the XIAO ESP32-C6 runs BLE and WiFi concurrently.

## Hardware to acquire

- **Xiaomi LYWSD03MMC** sensors (~$4–5 each, AliExpress)
  - Verify listing shows SHT30 sensor (not a cheaper knockoff)
  - Buy 1–2 extra as spares given AliExpress quality variance

## One-time setup per sensor

1. Open https://pvvx.github.io/ATC_MiThermometer/TelinkMiFlasher.html in Chrome
2. Connect via WebBLE, flash ATC custom firmware
3. In firmware settings: set advertising format to **BTHome** (not ATC or Mi)
4. Note each sensor's MAC address for filtering in firmware

## Firmware changes

### BLE scanning

- Use `BLEScan` (Arduino ESP32 BLE library) in passive mode
- Scan continuously with a short window (e.g. 100 ms scan / 200 ms interval)
  to avoid starving the WiFi stack
- Filter advertisements by service UUID `0xFCD2` (BTHome)
- Further filter by known sensor MAC addresses (hardcode in `secrets.h`)

### BTHome payload parsing

Service data format (after the UUID and device info byte):
```
[0x01] [battery%]          -- 1 byte
[0x02] [temp_lo] [temp_hi] -- 2 bytes, value × 0.01 = °C
[0x03] [hum_lo]  [hum_hi]  -- 2 bytes, value × 0.01 = %RH
```
Object IDs may appear in any order; parse by iterating object ID + length.

### MQTT topics (per sensor, keyed by MAC suffix)

- State: `home/filament_<mac_suffix>/state`  → `{"temperature": 22.5, "humidity": 35.2, "battery": 87}`
- Discovery: `homeassistant/sensor/filament_<mac_suffix>/<entity>/config` (retained)

### HA entities per sensor

| Entity | JSON key | device_class | Unit |
|--------|----------|-------------|------|
| Temperature | `temperature` | `temperature` | °C |
| Humidity | `humidity` | `humidity` | % |
| Battery | `battery` | `battery` | % |

### Architecture notes

- BLE scan callback updates a small struct (keyed by MAC) whenever an
  advertisement arrives; the existing 10 s loop reads those structs and
  publishes alongside the smoke/temp/humidity data
- Publish only if a fresh advertisement was received since the last cycle
  (use a `lastSeen` timestamp); mark stale with `expire_after` in discovery
- `mqtt.setBufferSize()` may need to increase beyond 512 B once discovery
  payloads for multiple sensors are added — check at compile time

## Open questions

- How many sensors? (determines MAC list length and buffer size)
- Placement: inside the sealed filament boxes, or external? Internal is
  more accurate but requires periodic lid removal to re-flash/replace battery.
