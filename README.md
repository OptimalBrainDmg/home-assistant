# Home Assistant Arduino Sketches

Arduino firmware for DIY devices that integrate with Home Assistant via MQTT auto-discovery. Each sketch targets a specific use case and publishes data or accepts commands so Home Assistant entities appear automatically.

## Sketches

| Sketch | Hardware | HA Entities |
|--------|----------|-------------|
| [`temp_humidity_sensor`](temp_humidity_sensor/) | LOLIN D1 Mini V4 + AM2320 or SHTC3 I2C sensor | Temperature (°C), Humidity (%) |
| [`temp_humidity_sensor_battery`](temp_humidity_sensor_battery/) | Seeed XIAO ESP32C6 + SHTC3 I2C sensor (battery-powered, deep sleep) | Temperature (°C), Humidity (%) |
| [`lightstrip`](lightstrip/) | Adafruit HUZZAH32 ESP32 Feather + WS2812B LED strip | Light (on/off, brightness, RGB color) per zone |
| [`fallout-terminal-pyportal`](fallout-terminal-pyportal/) | Adafruit PyPortal + ADT7410 | Temperature (°C); touch UI for light zones |
| [`epaper_weather`](epaper_weather/) | LILYGO T5 4.7" e-paper (ESP32-WROVER-E, battery-powered, deep sleep) | Weather dashboard: current conditions + 8-hour forecast from NWS via HA REST API |
| [`indoor_air_monitor`](indoor_air_monitor/) | Seeed XIAO ESP32C6 + Adafruit 6478 (STCC4+SHT41) + ENS160 (always-on) | Temperature (°C), Humidity (%), CO2 (ppm), TVOC (ppb), eCO2 (ppm) |
| [`printer_room_monitor`](printer_room_monitor/) | Seeed XIAO ESP32C6 + SHT40 + DFRobot Fermion MEMS Smoke sensor (always-on) | Temperature (°C), Humidity (%), Smoke (mV) |
| [`xmas`](xmas/) | Seeed XIAO ESP32C6 + SK6812 RGBW NeoPixel strip (89 LEDs, 14 letter zones) | Light (on/off, brightness, 5 animation effects) for MERRY CHRISTMAS sign |

## Setup

Each sketch folder contains hardware wiring diagrams, required library lists, and build/flash instructions specific to that sketch.

General steps for any sketch:

1. Copy `secrets.h.example` → `secrets.h` and fill in your credentials (or, for the PyPortal, edit `sdcard/config.jsn` and copy it to the SD card). MQTT-based sketches need WiFi + broker credentials; `epaper_weather` needs WiFi + a Home Assistant long-lived access token.
2. Follow the sketch-specific instructions to select your hardware variant and flash the firmware.
3. For MQTT-based sketches: install the **Mosquitto broker** add-on in Home Assistant and enable the **MQTT integration** — devices and entities will appear automatically. `epaper_weather` reads data directly from the HA REST API and does not use MQTT.
