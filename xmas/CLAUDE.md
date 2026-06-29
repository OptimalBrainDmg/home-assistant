# xmas/CLAUDE.md

MERRY CHRISTMAS LED sign controller with HA MQTT integration. SK6812 RGBW
NeoPixel strip, 89 LEDs, 14 letter zones. Controlled from HA as a single
light entity with brightness and animation effect selection.

## Hardware

| Item | Detail |
|------|--------|
| MCU | Seeed XIAO ESP32C6 |
| Strip | Adafruit SK6812 RGBW NeoPixel (3-wire: 5V / GND / Din) |
| Data pin | D0 (change `DATA_PIN` in sketch if wired elsewhere) |
| Power | External 5V supply — share GND with XIAO; USB alone cannot drive the full sign |
| FQBN | `esp32:esp32:XIAO_ESP32C6` |

## Letter map

Chain order: **C-H-R-I-S-T-M-A-S [space] M-E-R-R-Y** (physical chain starts at C)

| Letter | Start | End | Count |
|--------|-------|-----|-------|
| C | 0 | 4 | 5 |
| H | 5 | 11 | 7 |
| R | 12 | 18 | 7 |
| I | 19 | 21 | 3 |
| S | 22 | 27 | 6 |
| T | 28 | 32 | 5 |
| M | 33 | 41 | 9 |
| A | 42 | 48 | 7 |
| S | 49 | 54 | 6 |
| M | 55 | 63 | 9 |
| E | 64 | 69 | 6 |
| R | 70 | 76 | 7 |
| R | 77 | 83 | 7 |
| Y | 84 | 88 | 5 |

**Total: 89 LEDs (indices 0–88)**

Known wiring splice at pixel ~80 (2nd R of MERRY, index 77–83) — repaired.
Inspect this joint if that section goes dark after transport or storage.

## Build / Flash / Monitor

```bash
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C6 xmas/
arduino-cli upload  --fqbn esp32:esp32:XIAO_ESP32C6 --port <port> xmas/
arduino-cli monitor --port <port> --config baudrate=115200
```

## Configuration

Copy `secrets.h.example` to `secrets.h` and fill in WiFi + MQTT credentials
before flashing.

## Home Assistant entity

Appears as a single **light** named "Xmas Sign" under the MQTT integration.
Controls: on/off, brightness (0–255), and effect mode.

| Effect | Description |
|--------|-------------|
| Warm White | Static W-channel warm white with slight orange tint |
| Twinkle | Individual pixels sparkle in Christmas colors (red/green/white/gold/blue) |
| Classic | Alternating red/green letters with independent per-letter flicker |
| Sweep | Colors change letter-by-letter in a rolling wave |
| Chase | Bright comet moves C→Y, cycling colors on each pass |

## Libraries required

- Adafruit NeoPixel
- PubSubClient
- ArduinoJson (v7 — uses `JsonDocument` API, not `StaticJsonDocument`)

## Notes

- MQTT buffer: 512 B (discovery payload fits comfortably)
- Device ID: lower 3 bytes of `ESP.getEfuseMac()` — consistent with other XIAO sketches
- LED type: `NEO_GRBW + NEO_KHZ800` (SK6812, confirmed by yellow phosphor on W die)
- The Warm White effect is static (no per-tick rerender); all other effects animate at 50 fps
- Brightness changes take effect on the next animation tick without resetting animation state
- Effect or power state changes reinitialize the animation
