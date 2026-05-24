# Home Assistant Arduino Sensors

Arduino firmware for DIY sensors that integrate with Home Assistant via MQTT auto-discovery. Each sketch targets a specific sensor use case and publishes data so Home Assistant entities appear automatically.

## Sensors

| Sketch | Measures | HA Entities |
|--------|----------|-------------|
| [`temp_humidity_sensor`](temp_humidity_sensor/) | Temperature & humidity | Temperature (°C), Humidity (%) |

## Setup

Each sketch folder contains a `CLAUDE.md` (and optionally a README) with hardware wiring, required libraries, and build/flash instructions specific to that sketch.

General steps for any sketch:

1. Copy `secrets.h.example` → `secrets.h` and fill in your WiFi and MQTT credentials.
2. Follow the sketch-specific instructions to select your hardware variant and flash the firmware.
3. In Home Assistant, install the **Mosquitto broker** add-on and enable the **MQTT integration** — devices and entities will appear automatically.
