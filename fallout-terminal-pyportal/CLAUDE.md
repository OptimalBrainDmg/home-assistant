# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Arduino firmware for an Adafruit PyPortal (original) that renders a Fallout-terminal-style dashboard. Publishes ADT7410 temperature and ambient light sensor readings to Home Assistant via MQTT auto-discovery, and displays touch-toggleable buttons for MQTT light zones defined in `config.jsn` on the SD card.

## Hardware

| Component | Notes |
|-----------|-------|
| Adafruit PyPortal (original) | ATSAMD51J20A + ESP32 WiFi co-processor |
| 3.2" ILI9341 TFT display | 320×240, 8-bit parallel interface |
| Resistive touchscreen | 4-wire, read via A4/A5/A6/A7 |
| ADT7410 | I2C temperature sensor, address 0x48 (SDA=20, SCL=21) |
| Ambient light sensor | Analog, pin A2 (uncalibrated ADC 0–1023) |
| Micro-SD slot | CS = pin 32 |
| ESP32 WiFi co-processor | WiFiNINA library; pins auto-configured by "Adafruit PyPortal M4" board selection |

## Configuration Before Flashing

All configuration lives in `/config.jsn` on the SD card — no `secrets.h` needed.

**SD card filename constraint**: the Arduino SD library only supports 8.3 filenames (≤8 char name, ≤3 char extension). All files and directory names on the SD card must comply — this is why the config file uses `.jsn` instead of `.json`, and the sounds directory is `robco/` not `hyper-robco/`.

1. Edit `sdcard/config.jsn` (or copy `sdcard/config.jsn.example`): fill in WiFi credentials, MQTT credentials, timezone offset, and zone MQTT topics.
2. Copy the filled-in `config.jsn` to the root of a FAT32-formatted micro-SD card.
3. Insert the SD card into the PyPortal before powering on.

`sdcard/config.jsn` is gitignored because it contains credentials.

### Finding your lightstrip device ID

The lightstrip publishes to `home/<deviceId>/light/<i>/state`. You can see the device ID in MQTT Explorer, or check the lightstrip's serial output at boot — it prints the device ID.

## Required Libraries (install via Arduino Library Manager)

- **Adafruit ILI9341** by Adafruit
- **Adafruit GFX Library** by Adafruit
- **Adafruit TouchScreen** by Adafruit
- **ArduinoJson** by Benoit Blanchon (v7.x — `JsonDocument` API, no template size)
- **PubSubClient** by Nick O'Leary
- **NTPClient** by Fabrice Weinberg
- **WiFiNINA** by Arduino *(not installed — must add)*
- **Adafruit ADT7410 Library** by Adafruit *(not installed — must add)*
- **SD**, **Wire** (bundled with the SAMD board package)

## Board Package

Board: **Adafruit PyPortal M4** (in the *Adafruit SAMD Boards* package)

Add to Board Manager URLs:
```
https://adafruit.github.io/arduino-board-index/package_adafruit_index.json
```

The WiFiNINA library auto-configures the ESP32 co-processor SPI pins when the correct board is selected — no manual pin definitions needed for WiFi.

## Audio Setup

The sketch plays WAV files from `sdcard/robco/sounds/`. The MP3 originals need to be converted once. Use `afconvert` for MP3→WAV and `sox` for WAV→WAV resampling — `afconvert` WAV-to-WAV conversions add an "optimized" chunk that pushes audio data to offset 4096, which older versions of the sketch could not handle. The current `playSound` implementation scans RIFF sub-chunks to find the `data` chunk regardless of offset, so both formats now work.

```bash
cd sdcard/robco/sounds
# MP3 → WAV (afconvert is fine for this direction)
afconvert -f WAVE -d LEI16@22050 -c 1 poweron.mp3 poweron.wav
afconvert -f WAVE -d LEI16@22050 -c 1 poweroff.mp3 poweroff.wav
# WAV → WAV resample (use sox to avoid macOS offset-4096 optimization)
sox input.wav -r 22050 -b 16 -c 1 -e signed-integer output.wav
```

Copy the entire `sdcard/robco/` directory to the root of the SD card alongside `config.jsn`.

`sdcard/robco/` is a cloned external git repo — do not commit it or its `.git/` folder.

Audio events:
- Boot complete → `poweron.wav`
- Opening lighting control screen → `enter3.wav`
- Closing lighting control screen → `single1.wav`
- Zone toggled ON → `poweron.wav`
- Zone toggled OFF → `poweroff.wav`
- Brightness decreased (`[-]`) → `single4.wav`
- Brightness increased (`[+]`) → `single6.wav`
- Brightness already at limit (`[-]` at 5% or `[+]` at 100%) → `scroll.wav`

Playback is blocking (sketch pauses during sound). Typical sounds are < 1 s so this is acceptable.

Additional Fallout sound assets present in `sdcard/robco/sounds/` but not yet wired up: `enter1.wav`, `enter2.wav`, `scrolllp.wav`, `single2.wav`, `single3.wav`, `single5.wav`. These are usable for touch feedback by passing their paths to `playSound()`.

## Build & Flash

```bash
# Compile
arduino-cli compile --fqbn adafruit:samd:adafruit_pyportal_m4 .

# Upload (find port with: arduino-cli board list)
arduino-cli upload --fqbn adafruit:samd:adafruit_pyportal_m4 --port /dev/cu.usbmodem* .

# Monitor serial at 115200 baud
arduino-cli monitor --port /dev/cu.usbmodem* --config baudrate=115200
```

## Staged Bring-up

Flash and verify in order — each stage catches a different class of hardware problem before the next layer is added:

1. **Display + SD**: comment out WiFi/MQTT/sensor `#include`s; verify header renders and config.jsn loads
2. **Touch**: tap zone buttons and check Serial for `sx`/`sy` output (add temporary debug prints)
3. **Sensors**: verify ADT7410 found at boot and temperature reads appear on screen
4. **WiFi + NTP**: verify clock starts updating
5. **MQTT**: verify zone state subscriptions and sensor discovery appear in HA

## Architecture

Single-file sketch (`fallout-terminal-pyportal.ino`). Configuration block at the top.

- **`deviceId`**: `"pyportal_" + last 3 bytes of WiFi MAC` — e.g. `pyportal_a1b2c3`. Used in all MQTT topic paths and HA `unique_id` fields.
- **`loadConfig()` is fatal**: if SD init fails, the file is missing, or JSON is unparseable, the sketch displays the error and halts in an infinite loop. WiFi credentials live on the SD card so there is nothing useful to do without it.
- **Config**: loaded at boot from `/config.jsn` on the SD card. Contains WiFi credentials, MQTT credentials, `tz_offset` (integer hours from UTC), optional `outside_temp_topic`, `weather_topic`, and `humidity_topic` (plain-text MQTT topics — whatever is published appears directly on screen), and up to 4 zones. Each zone needs `name`, `state_topic` (subscribe), and `command_topic` (publish). The state/command topics must match the lightstrip's MQTT topics exactly.
- **Zone state**: `ZoneConfig` tracks `isOn` (bool) and `brightness` (0–100%, always a multiple of 5). Brightness is snapped to the nearest 5% on receipt from MQTT to avoid round-trip drift (`0–255` lightstrip scale ↔ `0–100%` display scale). The `-` button floors at 5%; use the toggle to turn a zone off.
- **MQTT payloads**: state topic publishes `{"state":"ON","brightness":N}` / `{"state":"OFF","brightness":N}` where N is 0–255 (lightstrip native scale). Command topic sends `{"state":"ON","brightness":N}` (toggle on or brightness change) or `{"state":"OFF"}` (toggle off). Sending `brightness` without `color` does not clobber the lightstrip's current RGB color.
- **Two-screen navigation**: `SCREEN_MAIN` shows the dashboard (time, sensors, compact zone buttons showing `[NAME..  75%]` or `[NAME.. OFF]`); `SCREEN_LIGHTING` is a full-screen zone control panel. Any touch on the main screen switches to the lighting screen. Tapping the header area (`sy < LIGHTING_HDR_H = 37`) on the lighting screen returns to main. `handleTouch()` emits raw coordinates and zone hit-test info to Serial on every touch — intentional, always enabled.
- **Lighting screen button layout**: each zone row is divided into three horizontal sections by vertical lines at `LBTN_LABEL_END = 220` and `LBTN_MINUS_END = 265`. Label area (x < 220): zone name on upper line, state on lower line; tap to toggle ON/OFF. `[-]` section (220 ≤ x < 265): decrease brightness 5%. `[+]` section (x ≥ 265): increase brightness 5%. `FreeMonoBold9pt7b` is ~12 px/char — the 216 px label area safely holds ~17 chars.
- **Touch mapping**: `touchToScrX/Y()` maps raw resistive-touch ADC coordinates to screen pixels for `setRotation(1)` (landscape, USB at top). Both functions use `constrain()` to clamp out-of-range readings. Constants are empirically calibrated from serial output — to recalibrate, monitor serial while pressing known screen corners and back-calculate `TOUCH_X/Y_MIN/MAX` from the printed `raw=p.x,p.y` values. **X-axis note**: `touchToScrX` uses `p.y` (not `p.x`) because the touchscreen axes are swapped relative to the display in landscape rotation.
- **Partial redraws**: only `updateDateTime()`, `updateSensors()`, and `renderZoneButton(i)` / `renderLightingButton(i)` redraw during normal operation; static chrome is drawn once at screen transitions to avoid full-screen flicker.
- **MQTT reconnect**: non-blocking in `loop()` using `millis()`; re-subscribes and re-publishes discovery on reconnect.
- **ADT7410**: detected at runtime in `setup()`. If `adt7410.begin()` returns false (sensor not present or wrong address), `hasTempSensor` stays false and temperature displays as `NO SENSOR`. Temperature is still published if the sensor is found.
- **Light sensor**: raw 10-bit ADC value (0–1023), published as-is. HA will show it as `ADC` units. Calibration to lux requires knowing the sensor's specific response curve.
- **`mqtt.setBufferSize(512)`**: required — default 256 bytes is too small for the discovery payloads.
- **Audio**: DAC output on A0 (`AUDIO_OUT`, `DAC_Channel0`); speaker amplifier enabled via pin 50 (`SPEAKER_SHUTDOWN`, PA27). No external audio library — `playSound(path)` streams 16-bit mono PCM WAV from SD directly to the DAC using `analogWrite()` paced by `micros()`. Blocking. Silently no-ops if the SD file is missing. `playSound` scans RIFF sub-chunks to find the `data` chunk regardless of offset; it skips unknown chunks by reading-and-discarding rather than `f.seek()` because the Arduino SD library's seek silently fails for large forward jumps (macOS inserts a `FLLR` chunk of ~4044 bytes to align audio data to offset 4096).

## Fallout Terminal Aesthetic

- Background: `0x0000` (black)
- Primary text: `0x37E0` (phosphor green, ~RGB 57 255 0)
- Dim/inactive text: `0x1320` (~40% green)
- Header: `0x57E0` (brighter green)
- Active zone button fill: `0x0180` (dark green background, lighting screen only)
- Font: `FreeMono9pt7b` / `FreeMonoBold9pt7b` from Adafruit GFX (included with library)
- Main screen zone buttons: `[NAME..............  75%]` / `[NAME.............. OFF]` — 16-char dot-padded name, 4-char state field; color reflects zone state
- Lighting screen zone buttons: name on first line, state (`75%` / `OFF`) on second line; `[-]` and `[+]` sections to the right separated by vertical dividers; label area has zone fill color (`COLOR_BTN_ON` when on, black when off); control sections always black
