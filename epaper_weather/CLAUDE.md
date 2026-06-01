# epaper_weather

Battery-powered e-paper weather station. Wakes every 15 minutes, fetches current conditions and 8-hour hourly forecast from Home Assistant REST API, renders a full 960×540 dashboard, then deep-sleeps.

## Hardware

**LILYGO T5 4.7" e-paper** — ESP32-WROVER-E + ED047TC1 panel
- FQBN: `esp32:esp32:esp32wrover`
- Display: 960×540 px, 4-bit grayscale, parallel interface
- PSRAM: 4 MB (required for 259,200-byte framebuffer)
- Battery ADC: GPIO 36 via 2:1 voltage divider; `epd_poweron()` must be called first (POWER_EN controls the ADC rail)

## Required Libraries

- **LilyGo-EPD47** — install manually from `https://github.com/Xinyuan-LilyGO/LilyGo-EPD47`
  - API: `epd_init()`, `epd_poweron()`, `epd_poweroff_all()`, `epd_clear()`, `epd_draw_grayscale_image()`, drawing primitives in `epd_driver.h`
  - Bundled font: `FiraSans` — `advance_y=50`, `ascender=39`, `descender=-12`; used for all main conditions text
- **Roboto** — `roboto-font/roboto.h`, generated at 12pt/150dpi; `advance_y=29`, `ascender=24`, `descender=-7`; used for battery indicator text and hourly temp/precip lines. OFL 1.1 license in `roboto-font/LICENSE`.
- **ArduinoJson v7** — `JsonDocument` API (no template size parameter)
- **HTTPClient + WiFi** — bundled with ESP32 Arduino core

## Build & Flash

The sketch pins to ESP32 Arduino core **2.0.17** (IDF 4.x) via `sketch.yaml` — LilyGo-EPD47 v1.0.1 uses the I2S/LCD clock source API from IDF 4.x which broke in IDF 5.x (core 3.x). Core 3.x is still used for all other ESP32 sketches in this repo.

```bash
cp secrets.h.example secrets.h  # then fill in credentials
arduino-cli compile --profile default epaper_weather/
arduino-cli compile --upload --profile default --port <port> epaper_weather/
arduino-cli monitor --port <port> --config baudrate=115200
```

`--profile default` reads `sketch.yaml`, downloads core 2.0.17 + all libraries into an isolated cache, and compiles/uploads without touching the globally installed core 3.x.

## Configuration

Copy `secrets.h.example` to `secrets.h` and set:
- `WIFI_SSID` / `WIFI_PASSWORD`
- `HA_HOST` — hostname or IP of Home Assistant (e.g. `homeassistant.local`)
- `HA_PORT` — default `8123`
- `HA_TOKEN` — long-lived access token from HA profile page
- `HA_ENTITY_ID` — the NWS weather entity (e.g. `weather.nws_43_77_...`)
- `LOCATION_NAME` — display label shown in header

## Architecture

Everything runs in `setup()`; `loop()` is empty. Wake cycle:

1. `epd_init()` + allocate 259,200-byte PSRAM framebuffer via `ps_calloc()`
2. `epd_poweron()` + 10ms settle → `analogReadMilliVolts(BATT_PIN)` for battery
3. `connectWiFi()` — 20s millis-bounded; on timeout → `NO_WIFI` stale mode
4. `GET /api/states/<entity>` — current conditions (filtered ArduinoJson)
5. `POST /api/services/weather/get_forecasts?return_response=true` — hourly forecast, first 8 entries
6. On any HTTP failure → `DATA_ERR` stale mode
7. `renderFrame()` → writes full dashboard to PSRAM framebuffer
8. `epd_clear()` + `epd_draw_grayscale_image(epd_full_screen(), framebuffer)`
9. `epd_poweroff_all()` + `WiFi.mode(WIFI_OFF)` + `esp_deep_sleep_start(15 min)`

**Stale data cache** (`RTC_DATA_ATTR`): last-good `WeatherCache` and `ForecastSlot[8]` survive deep sleep. On WiFi or HTTP failure the display re-renders from cache with a "NO WIFI" or "DATA ERR" badge in the header.

## Display Layout (960×540)

```
y   0– 51  Header: location | date | [battery bar top-right] [battery text bottom-right]
y  52–307  Main: [120px icon 0–244] | [conditions 246–709] | [compass 711–959]
y 308–539  Hourly: 8 columns × 120px (time / icon / temp / precip%)
```

- **Compass**: programmatic circle + cardinal ticks + filled arrow at `wind_bearing`; "CALM" if null. `DIV2_X=710`, `COMP_CX=840` (hardcoded independently so the divider line can be adjusted without shifting the compass).
- **Icons**: programmatic in `icons.h` using `epd_*` primitives; two sizes (120px current, 56px hourly)
- **Battery indicator**: bar (`seg_w=8`, `seg_h=12`, 5 segments) pinned to top-right at `by=4`; Roboto text right-aligned below it at `HEADER_H - 9`
- **Text baselines** (center panel, 5 rows): y = 92, 142, 192, 242, 292
- **Hourly baselines**: time y=386, temp y=485, precip y=520; icon center y=428

## Data Source

HA NWS integration. Units: °F, inHg, mph, mi. `wind_bearing` is nullable (null → calm wind). Forecast `datetime` is ISO-8601 with local UTC offset — no separate timezone config needed; local time is derived directly from the datetime string.
