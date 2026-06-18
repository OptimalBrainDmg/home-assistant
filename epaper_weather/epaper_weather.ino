#ifndef BOARD_HAS_PSRAM
#error "Enable PSRAM: Arduino IDE -> tools -> PSRAM -> Enabled"
#endif

#include <Arduino.h>
#include "epd_driver.h"
#include "firasans.h"
#include "roboto-font/roboto.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include "secrets.h"
#include "mdi_icons.h"

// ── Wake interval ────────────────────────────────────────────────
#define SLEEP_US (15ULL * 60 * 1000000)

// ── Network ──────────────────────────────────────────────────────
#define WIFI_TIMEOUT_MS  20000
#define HTTP_TIMEOUT_MS  10000
#define MQTT_TIMEOUT_MS  10000

// ── Battery ──────────────────────────────────────────────────────
#define BATT_PIN 36

// ── Layout ───────────────────────────────────────────────────────
#define HEADER_H        51
#define MAIN_TOP        52
#define MAIN_BOT        339
#define HOURLY_TOP      340
// Panel dividers
#define DIV1_X          208
#define DIV2_X          702
// Compass
#define COMP_CX         831
#define COMP_CY         ((MAIN_TOP + MAIN_BOT) / 2)
#define COMP_R          90
#define COMP_INNER_R    60
// Current-conditions panel
#define COND_LEFT       221
// Current icon
#define ICON_CX         (DIV1_X / 2)
#define ICON_CY         (COMP_CY - 24)
#define ICON_SIZE       120
#define ICON_COND_Y     (ICON_CY + ICON_SIZE/2 + 28)
#define ICON_TEMP_Y     (COMP_CY + ICON_SIZE/2 + 43)
// Main body text baselines (5 rows, advance_y=50, bottom at 292+12=304 ≤ MAIN_BOT)
#define ROW1_Y          92
#define ROW2_Y          115
#define ROW3_Y          165
#define ROW4_Y          242
#define ROW5_Y          292
// Hourly section
#define FORECAST_COUNT  8
#define COL_W           (EPD_WIDTH / FORECAST_COUNT)
#define HTIME_Y         386
#define HICON_CY        428
#define HICON_SIZE      56
#define HTEMP_Y         485
#define HPRECIP_Y       520
// Sub-station boxes (bottom half of center panel)
#define SUB_MID_Y       210
#define SUB_MID_X       ((DIV1_X + DIV2_X) / 2)
#define SUB_LABEL_Y     (SUB_MID_Y + 36)
#define SUB_VAL_Y       (SUB_MID_Y + 95)
#define SUB_LEFT_CX     ((DIV1_X + SUB_MID_X) / 2)
#define SUB_RIGHT_CX    ((SUB_MID_X + DIV2_X) / 2)
#define SUB_BOX_W       (SUB_MID_X - DIV1_X)

// ── RTC-persistent cache ─────────────────────────────────────────
struct WeatherCache {
    char  condition[32];
    float temperature;
    float humidity;
    float pressure;
    float wind_speed;
    int16_t wind_bearing;
    bool  wind_calm;
};

struct ForecastSlot {
    char  time_label[8];
    char  condition[32];
    int16_t temperature;
    int16_t precip_pct;
};

struct StationSlot {
    char  label[16];
    float temp_f;    // NAN = no data
    float humidity;  // NAN = no data
};

RTC_DATA_ATTR WeatherCache r_current;
RTC_DATA_ATTR ForecastSlot r_forecast[FORECAST_COUNT];
RTC_DATA_ATTR bool  r_valid    = false;
RTC_DATA_ATTR char  r_date[20] = "";

// ── Globals ──────────────────────────────────────────────────────
uint8_t *framebuffer = nullptr;

enum StaleReason { FRESH, NO_WIFI, DATA_ERR };

// ── Battery ──────────────────────────────────────────────────────
static float battVolts() {
    uint32_t mv = analogReadMilliVolts(BATT_PIN);
    return mv * 2.0f / 1000.0f;
}

static int battPercent(float v) {
    if (v >= 4.20f) return 100;
    if (v >= 4.10f) return 90 + (int)((v - 4.10f) / 0.10f * 10);
    if (v >= 4.00f) return 70 + (int)((v - 4.00f) / 0.10f * 20);
    if (v >= 3.80f) return 40 + (int)((v - 3.80f) / 0.20f * 30);
    if (v >= 3.60f) return  5 + (int)((v - 3.60f) / 0.20f * 35);
    if (v >= 3.50f) return 5;
    return 0;
}

// ── WiFi ─────────────────────────────────────────────────────────
static bool connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint32_t t = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t > WIFI_TIMEOUT_MS) return false;
        delay(100);
    }
    delay(500);  // let TCP stack settle after association
    return true;
}

// ── MQTT battery publish ─────────────────────────────────────────
static void publishBattery(int pct) {
    uint64_t mac  = ESP.getEfuseMac();
    String deviceId   = "weather_" + String((uint32_t)(mac & 0xFFFFFF), HEX);
    String stateTopic = "homeassistant/sensor/" + deviceId + "/battery/state";

    WiFiClient   wc;
    PubSubClient mqtt(wc);
    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setBufferSize(512);

    uint32_t start = millis();
    bool connected = false;
    while (millis() - start < MQTT_TIMEOUT_MS) {
        if (mqtt.connect(deviceId.c_str(), MQTT_USER, MQTT_PASS)) { connected = true; break; }
        delay(500);
    }
    if (!connected) { Serial.println("MQTT connect failed — battery not reported"); return; }

    // Retained discovery config
    String configTopic = "homeassistant/sensor/" + deviceId + "/battery/config";
    JsonDocument doc;
    doc["name"]                = "Battery";
    doc["state_topic"]         = stateTopic;
    doc["device_class"]        = "battery";
    doc["unit_of_measurement"] = "%";
    doc["state_class"]         = "measurement";
    doc["unique_id"]           = deviceId + "_battery";
    doc["expire_after"]        = (15 * 60) * 3;
    JsonObject dev        = doc["device"].to<JsonObject>();
    dev["identifiers"][0] = deviceId;
    dev["name"]           = "E-Paper Weather Station";
    dev["model"]          = "LILYGO T5 4.7\"";
    dev["manufacturer"]   = "LILYGO";
    String configPayload;
    serializeJson(doc, configPayload);
    mqtt.publish(configTopic.c_str(), configPayload.c_str(), true);

    // State: plain integer percentage
    mqtt.publish(stateTopic.c_str(), String(pct).c_str());

    for (int i = 0; i < 5; i++) { mqtt.loop(); delay(10); }
    mqtt.disconnect();
    Serial.printf("Battery reported via MQTT: %d%%\n", pct);
}

// ── Sub-station MQTT fetch ───────────────────────────────────────
// Subscribes to the two configured station topics and waits up to 5 s for
// retained messages (source devices must publish retained state).
static struct { StationSlot *slots; bool rx[2]; } s_st;

static void stationCallback(char *topic, byte *payload, unsigned int len) {
    const char *topics[2] = { STATION_0_TOPIC, STATION_1_TOPIC };
    JsonDocument doc;
    bool parsed = false;
    for (int i = 0; i < 2; i++) {
        if (strcmp(topic, topics[i]) != 0) continue;
        if (!parsed) {
            if (deserializeJson(doc, (const char *)payload, len)) return;
            parsed = true;
        }
        JsonVariant tv = doc["temperature"];
        float c = (tv.is<float>() || tv.is<int>()) ? tv.as<float>() : NAN;
        JsonVariant hv = doc["humidity"];
        s_st.slots[i].temp_f   = isnan(c) ? NAN : c * 9.0f / 5.0f + 32.0f;
        s_st.slots[i].humidity = (hv.is<float>() || hv.is<int>()) ? hv.as<float>() : NAN;
        s_st.rx[i] = true;
    }
}

static void fetchStations(StationSlot slots[2]) {
    s_st.slots = slots;
    s_st.rx[0] = s_st.rx[1] = false;

    WiFiClient wc;
    PubSubClient mqttSub(wc);
    mqttSub.setServer(MQTT_HOST, MQTT_PORT);
    mqttSub.setBufferSize(512);
    mqttSub.setCallback(stationCallback);

    uint64_t mac = ESP.getEfuseMac();
    String cid = "weather_" + String((uint32_t)(mac & 0xFFFFFF), HEX) + "_sub";

    uint32_t t = millis();
    while (millis() - t < MQTT_TIMEOUT_MS) {
        if (mqttSub.connect(cid.c_str(), MQTT_USER, MQTT_PASS)) break;
        delay(500);
    }
    if (!mqttSub.connected()) { Serial.println("Stations: MQTT connect failed"); return; }

    mqttSub.subscribe(STATION_0_TOPIC);
    mqttSub.subscribe(STATION_1_TOPIC);

    uint32_t deadline = millis() + 5000;
    while (millis() < deadline && !(s_st.rx[0] && s_st.rx[1])) {
        mqttSub.loop();
        delay(10);
    }
    mqttSub.disconnect();
    Serial.printf("Stations: [%s] %.1f°F %.0f%%  [%s] %.1f°F %.0f%%\n",
        slots[0].label, slots[0].temp_f, slots[0].humidity,
        slots[1].label, slots[1].temp_f, slots[1].humidity);
}

// ── Date parsing ─────────────────────────────────────────────────
// Extract human-readable hour label and date string from ISO-8601 datetime.
// e.g. "2025-05-31T11:00:00-04:00" → hour_label="11am", date="Sat May 31"
static void parseDatetime(const char *dt, char *hour_label, int hour_label_len,
                          char *date_buf, int date_buf_len) {
    const char *T = strchr(dt, 'T');
    if (!T) { strlcpy(hour_label, "?", hour_label_len); return; }

    int hour = atoi(T + 1);
    if (hour == 0)       snprintf(hour_label, hour_label_len, "12am");
    else if (hour < 12)  snprintf(hour_label, hour_label_len, "%dam",  hour);
    else if (hour == 12) snprintf(hour_label, hour_label_len, "12pm");
    else                 snprintf(hour_label, hour_label_len, "%dpm",  hour - 12);

    if (!date_buf || date_buf_len <= 0) return;
    int year = atoi(dt), month = atoi(dt + 5), day = atoi(dt + 8);
    if (month < 1 || month > 12) { strlcpy(date_buf, "---", date_buf_len); return; }

    // Tomohiko Sakamoto day-of-week
    static const int t_arr[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    int y = year;
    if (month < 3) y--;
    int dow = (y + y/4 - y/100 + y/400 + t_arr[month-1] + day) % 7;
    const char *days[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                             "Jul","Aug","Sep","Oct","Nov","Dec"};
    snprintf(date_buf, date_buf_len, "%s %s %d", days[dow], months[month-1], day);
}

// ── HTTP: current conditions ─────────────────────────────────────
static bool fetchCurrent(WeatherCache &out) {
    WiFiClient client;
    HTTPClient http;
    String url = "http://" HA_HOST ":" + String(HA_PORT) + "/api/states/" HA_ENTITY_ID;
    if (!http.begin(client, url)) return false;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("Authorization", "Bearer " HA_TOKEN);

    int cur_status = http.GET();
    Serial.printf("Current HTTP status: %d  url: %s\n", cur_status, url.c_str());
    if (cur_status != HTTP_CODE_OK) { http.end(); return false; }

    JsonDocument filter;
    filter["state"] = true;
    filter["attributes"]["temperature"]  = true;
    filter["attributes"]["humidity"]     = true;
    filter["attributes"]["pressure"]     = true;
    filter["attributes"]["wind_speed"]   = true;
    filter["attributes"]["wind_bearing"] = true;

    JsonDocument doc;
    auto err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (err) return false;

    strlcpy(out.condition, doc["state"] | "unknown", sizeof(out.condition));
    out.temperature = doc["attributes"]["temperature"] | 0.0f;
    out.humidity    = doc["attributes"]["humidity"]    | 0.0f;
    out.pressure    = doc["attributes"]["pressure"]    | 0.0f;
    out.wind_speed  = doc["attributes"]["wind_speed"]  | 0.0f;

    JsonVariant wb = doc["attributes"]["wind_bearing"];
    if (wb.isNull()) {
        out.wind_calm    = true;
        out.wind_bearing = 0;
    } else {
        out.wind_calm    = false;
        out.wind_bearing = wb.as<int16_t>();
    }
    return true;
}

// ── HTTP: hourly forecast ────────────────────────────────────────
static bool fetchForecast(ForecastSlot slots[], char *date_buf, int date_buf_len) {
    WiFiClient client;
    HTTPClient http;
    String url = "http://" HA_HOST ":" + String(HA_PORT) +
                 "/api/services/weather/get_forecasts?return_response=true";
    if (!http.begin(client, url)) return false;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("Authorization", "Bearer " HA_TOKEN);
    http.addHeader("Content-Type", "application/json");

    String body = "{\"entity_id\":\"" HA_ENTITY_ID "\",\"type\":\"hourly\"}";
    int fcast_status = http.POST(body);
    Serial.printf("Forecast HTTP status: %d\n", fcast_status);
    if (fcast_status != HTTP_CODE_OK) { http.end(); return false; }

    JsonDocument filter;
    filter["service_response"][HA_ENTITY_ID]["forecast"][0]["datetime"]                  = true;
    filter["service_response"][HA_ENTITY_ID]["forecast"][0]["condition"]                 = true;
    filter["service_response"][HA_ENTITY_ID]["forecast"][0]["temperature"]               = true;
    filter["service_response"][HA_ENTITY_ID]["forecast"][0]["precipitation_probability"] = true;

    JsonDocument doc;
    auto err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (err) {
        Serial.printf("Forecast JSON err: %s\n", err.c_str());
        return false;
    }

    JsonArray forecast = doc["service_response"][HA_ENTITY_ID]["forecast"].as<JsonArray>();
    int count = 0;
    bool date_written = false;
    bool skipped_current = false;
    for (JsonObject entry : forecast) {
        if (!skipped_current) { skipped_current = true; continue; }
        if (count >= FORECAST_COUNT) break;
        const char *dt = entry["datetime"] | "";
        parseDatetime(dt, slots[count].time_label, sizeof(slots[count].time_label),
                      date_written ? nullptr : date_buf,
                      date_written ? 0 : date_buf_len);
        date_written = true;
        strlcpy(slots[count].condition, entry["condition"] | "unknown", sizeof(slots[count].condition));
        slots[count].temperature = entry["temperature"] | 0;
        slots[count].precip_pct  = entry["precipitation_probability"] | 0;
        count++;
    }
    return count > 0;
}

// ── Wind bearing → compass label ─────────────────────────────────
static const char *bearingDir(int bearing) {
    static const char *dirs[] = {"N","NNE","NE","ENE","E","ESE","SE","SSE",
                                  "S","SSW","SW","WSW","W","WNW","NW","NNW"};
    return dirs[(int)((bearing + 11.25f) / 22.5f) % 16];
}

// ── Condition → display label ────────────────────────────────────
static const char *condLabel(const char *c) {
    if (strcmp(c, "sunny")             == 0) return "Sunny";
    if (strcmp(c, "clear-night")       == 0) return "Clear";
    if (strcmp(c, "cloudy")            == 0) return "Cloudy";
    if (strcmp(c, "partlycloudy")      == 0) return "Partly Cloudy";
    if (strcmp(c, "partlycloudy-night")== 0) return "Partly Cloudy";
    if (strcmp(c, "rainy")             == 0) return "Rainy";
    if (strcmp(c, "lightning-rainy")   == 0) return "Storms";
    if (strcmp(c, "lightning")         == 0) return "Thunderstorm";
    if (strcmp(c, "snowy")             == 0) return "Snowy";
    if (strcmp(c, "snowy-rainy")       == 0) return "Wintry Mix";
    if (strcmp(c, "fog")               == 0) return "Foggy";
    if (strcmp(c, "windy")             == 0) return "Windy";
    if (strcmp(c, "hail")              == 0) return "Hail";
    if (strcmp(c, "exceptional")       == 0) return "Exceptional";
    return c;
}

// ── Text helpers ─────────────────────────────────────────────────
// Draw str with baseline at (x, y), left-aligned. Returns new cursor_x.
static int32_t drawText(const char *str, int32_t x, int32_t y, uint8_t *fb) {
    write_mode((GFXfont *)&FiraSans, str, &x, &y, fb, BLACK_ON_WHITE, nullptr);
    return x;
}

// Draw str horizontally centered around cx, baseline at y.
static void drawTextCentered(const char *str, int32_t cx, int32_t y, uint8_t *fb) {
    int32_t px = 0, py = 0, x1, y1, w, h;
    get_text_bounds((GFXfont *)&FiraSans, str, &px, &py, &x1, &y1, &w, &h, nullptr);
    int32_t cursor_x = cx - x1 - w / 2;
    int32_t cursor_y = y;
    write_mode((GFXfont *)&FiraSans, str, &cursor_x, &cursor_y, fb, BLACK_ON_WHITE, nullptr);
}

static void drawTextCenteredRoboto(const char *str, int32_t cx, int32_t y, uint8_t *fb) {
    int32_t px = 0, py = 0, x1, y1, w, h;
    get_text_bounds((GFXfont *)&Roboto, str, &px, &py, &x1, &y1, &w, &h, nullptr);
    int32_t cursor_x = cx - x1 - w / 2;
    int32_t cursor_y = y;
    write_mode((GFXfont *)&Roboto, str, &cursor_x, &cursor_y, fb, BLACK_ON_WHITE, nullptr);
}

// ── Compass ──────────────────────────────────────────────────────
static void drawCompass(int32_t cx, int32_t cy, int32_t r,
                        int bearing, bool calm, float wind_speed, uint8_t *fb) {
    epd_draw_circle(cx, cy, r, 0x00, fb);
    epd_draw_circle(cx, cy, COMP_INNER_R, 0x00, fb);

    // Tick marks: 8px at cardinals, 4px at intercardinals — on both rings
    for (int a = 0; a < 360; a += 45) {
        float rad = a * (float)M_PI / 180.0f;
        int tick = (a % 90 == 0) ? 8 : 4;
        epd_draw_line(cx + (int)(cosf(rad)*(r-tick)), cy + (int)(sinf(rad)*(r-tick)),
                      cx + (int)(cosf(rad)*r),         cy + (int)(sinf(rad)*r),
                      0x00, fb);
        epd_draw_line(cx + (int)(cosf(rad)*COMP_INNER_R),
                      cy + (int)(sinf(rad)*COMP_INNER_R),
                      cx + (int)(cosf(rad)*(COMP_INNER_R-tick)),
                      cy + (int)(sinf(rad)*(COMP_INNER_R-tick)),
                      0x00, fb);
    }

    // Labels (Roboto) outside ring. lo = distance from center to label visual center.
    // Roboto ascender=24 → baseline = visual_center_y + 12.
    float lo = r + 22.0f;
    float d  = lo * 0.7071f;  // diagonal offset for intercardinals
    drawTextCenteredRoboto("N",  cx,           cy - (int)lo + 12,  fb);
    drawTextCenteredRoboto("S",  cx,           cy + (int)lo + 12,  fb);
    drawTextCenteredRoboto("E",  cx + (int)lo, cy + 12,            fb);
    drawTextCenteredRoboto("W",  cx - (int)lo, cy + 12,            fb);
    drawTextCenteredRoboto("NE", cx + (int)d,  cy - (int)d + 12,   fb);
    drawTextCenteredRoboto("NW", cx - (int)d,  cy - (int)d + 12,   fb);
    drawTextCenteredRoboto("SE", cx + (int)d,  cy + (int)d + 12,   fb);
    drawTextCenteredRoboto("SW", cx - (int)d,  cy + (int)d + 12,   fb);

    if (calm) {
        drawTextCentered("Calm", cx, cy, fb);
    } else {
        // Speed (FiraSans) and "mph" (Roboto) centered in inner circle
        char sbuf[8];
        snprintf(sbuf, sizeof(sbuf), "%.0f", wind_speed);
        drawTextCentered(sbuf, cx, cy, fb);
        drawTextCenteredRoboto("mph", cx, cy + 26, fb);

        // Arrow in annulus: tip near outer ring, base near inner ring
        float rad     = bearing * (float)M_PI / 180.0f;
        float dx      = sinf(rad), dy = -cosf(rad);   // unit vector toward bearing
        float perp_x  = -dy,  perp_y = dx;            // perpendicular

        float base_cx = cx + dx * (COMP_INNER_R + 5);
        float base_cy = cy + dy * (COMP_INNER_R + 5);
        int32_t tip_x = cx + (int)(dx * (r - 5));
        int32_t tip_y = cy + (int)(dy * (r - 5));
        int32_t b1x   = (int)(base_cx + perp_x * 10);
        int32_t b1y   = (int)(base_cy + perp_y * 10);
        int32_t b2x   = (int)(base_cx - perp_x * 10);
        int32_t b2y   = (int)(base_cy - perp_y * 10);
        epd_fill_triangle(tip_x, tip_y, b1x, b1y, b2x, b2y, 0x00, fb);
    }
}

// ── Battery bar ──────────────────────────────────────────────────
// right_x: right edge of the entire indicator (bar + text)
static void drawBatteryBar(int32_t right_x, int pct, float volts, uint8_t *fb) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%d%% %.1fv", pct, volts);

    // Text right-aligned at right_x, baseline at bottom of header
    int32_t px = 0, py = 0, x1, y1, tw, th;
    get_text_bounds((GFXfont *)&Roboto, buf, &px, &py, &x1, &y1, &tw, &th, nullptr);
    int32_t ty = HEADER_H - 9;
    int32_t tx = right_x - x1 - tw;
    int32_t wx = tx, wy = ty;
    write_mode((GFXfont *)&Roboto, buf, &wx, &wy, fb, BLACK_ON_WHITE, nullptr);

    // Bar in top-right corner, right-aligned at right_x
    int seg_w = 8, seg_h = 12, gap = 2;
    int bar_w = 5 * seg_w + 4 * gap;     // 48 px
    int32_t by = 4;
    int32_t bx = right_x - 3 - bar_w;    // nub sits at right_x

    epd_draw_rect(bx, by, bar_w, seg_h, 0x00, fb);
    epd_fill_rect(bx + bar_w, by + 4, 3, seg_h - 8, 0x00, fb);
    int segs = (pct + 9) / 20;
    for (int i = 0; i < segs; i++) {
        epd_fill_rect(bx + 2 + i * (seg_w + gap), by + 2, seg_w, seg_h - 4, 0x00, fb);
    }
}

// ── Header ───────────────────────────────────────────────────────
static void renderHeader(const char *date_str, float batt_v, int batt_pct,
                         StaleReason stale, uint8_t *fb) {
    // Location name (left)
    drawText(LOCATION_NAME, 10, HEADER_H - 9, fb);

    // Date (center)
    drawTextCentered(date_str, EPD_WIDTH / 2, HEADER_H - 9, fb);

    // Stale badge after date (if applicable)
    if (stale != FRESH) {
        const char *badge = (stale == NO_WIFI) ? "  NO WIFI" : "  DATA ERR";
        int32_t px = 0, py = 0, x1, y1, dw, dh;
        get_text_bounds((GFXfont *)&FiraSans, date_str, &px, &py, &x1, &y1, &dw, &dh, nullptr);
        drawText(badge, EPD_WIDTH/2 + dw/2 + 4, HEADER_H - 9, fb);
    }

    drawBatteryBar(EPD_WIDTH - 8, batt_pct, batt_v, fb);

    // Separator
    epd_draw_hline(0, HEADER_H, EPD_WIDTH, 0x00, fb);
}

// ── Main body ────────────────────────────────────────────────────
static void renderMain(const WeatherCache &cur, uint8_t *fb) {
    // Vertical dividers
    epd_draw_vline(DIV1_X, MAIN_TOP, MAIN_BOT - MAIN_TOP, 0x00, fb);
    epd_draw_vline(DIV2_X, MAIN_TOP, MAIN_BOT - MAIN_TOP, 0x00, fb);

    // Compass (right panel)
    drawCompass(COMP_CX, COMP_CY, COMP_R, cur.wind_bearing, cur.wind_calm, cur.wind_speed, fb);

    // Temperature centered below icon (left panel)
    char buf[64];
    snprintf(buf, sizeof(buf), "%.0f\xC2\xB0""F", cur.temperature);
    drawTextCentered(buf, ICON_CX, ICON_TEMP_Y, fb);

    // Conditions (center panel), left-aligned at COND_LEFT
    snprintf(buf, sizeof(buf), "%.0f%% humidity", cur.humidity);
    drawText(buf, COND_LEFT, ROW2_Y, fb);

    snprintf(buf, sizeof(buf), "%.2f inHg", cur.pressure);
    drawText(buf, COND_LEFT, ROW3_Y, fb);

    // Weather icon + condition label + temperature (left panel)
    drawMdiIcon(cur.condition, ICON_CX, ICON_CY, ICON_SIZE, fb);
    drawTextCenteredRoboto(condLabel(cur.condition), ICON_CX, ICON_COND_Y, fb);
}

// ── Hourly forecast ───────────────────────────────────────────────
static void renderHourly(const ForecastSlot fcast[], uint8_t *fb) {
    epd_draw_hline(0, HOURLY_TOP - 1, EPD_WIDTH, 0x00, fb);

    for (int i = 0; i < FORECAST_COUNT; i++) {
        int32_t col_cx = i * COL_W + COL_W / 2;

        drawTextCentered(fcast[i].time_label, col_cx, HTIME_Y, fb);
        drawMdiIcon(fcast[i].condition, col_cx, HICON_CY, HICON_SIZE, fb);

        char buf[16];
        snprintf(buf, sizeof(buf), "%d\xC2\xB0""F", fcast[i].temperature);
        drawTextCenteredRoboto(buf, col_cx, HTEMP_Y, fb);

        snprintf(buf, sizeof(buf), "%d%%", fcast[i].precip_pct);
        drawTextCenteredRoboto(buf, col_cx, HPRECIP_Y, fb);
    }
}

// ── Sub-station boxes ────────────────────────────────────────────
static void renderSubBoxes(const StationSlot stations[2], uint8_t *fb) {
    epd_draw_hline(DIV1_X, SUB_MID_Y, DIV2_X - DIV1_X, 0x00, fb);
    epd_draw_vline(SUB_MID_X, SUB_MID_Y, MAIN_BOT - SUB_MID_Y, 0x00, fb);

    const int32_t label_cx[2] = { SUB_LEFT_CX,              SUB_RIGHT_CX             };
    const int32_t temp_cx[2]  = { SUB_LEFT_CX  - SUB_BOX_W / 4,
                                   SUB_RIGHT_CX - SUB_BOX_W / 4 };
    const int32_t hum_cx[2]   = { SUB_LEFT_CX  + SUB_BOX_W / 4,
                                   SUB_RIGHT_CX + SUB_BOX_W / 4 };

    for (int i = 0; i < 2; i++) {
        drawTextCenteredRoboto(stations[i].label, label_cx[i], SUB_LABEL_Y, fb);

        char buf[16];
        if (!isnan(stations[i].temp_f))
            snprintf(buf, sizeof(buf), "%.0f\xC2\xB0""F", stations[i].temp_f);
        else
            strlcpy(buf, "--", sizeof(buf));
        drawTextCentered(buf, temp_cx[i], SUB_VAL_Y, fb);

        if (!isnan(stations[i].humidity))
            snprintf(buf, sizeof(buf), "%.0f%%", stations[i].humidity);
        else
            strlcpy(buf, "--", sizeof(buf));
        drawTextCentered(buf, hum_cx[i], SUB_VAL_Y, fb);
    }
}

// ── Full frame render ─────────────────────────────────────────────
static void renderFrame(const WeatherCache &cur, const ForecastSlot fcast[],
                        float batt_v, int batt_pct, const char *date_str,
                        StaleReason stale, const StationSlot stations[]) {
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
    renderHeader(date_str, batt_v, batt_pct, stale, framebuffer);
    renderMain(cur, framebuffer);
    renderHourly(fcast, framebuffer);
    renderSubBoxes(stations, framebuffer);
}

// ── Setup (full wake cycle) ───────────────────────────────────────
void setup() {
    Serial.begin(115200);

    epd_init();

    framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
    if (!framebuffer) {
        Serial.println("PSRAM alloc failed — sleeping");
        esp_sleep_enable_timer_wakeup(SLEEP_US);
        esp_deep_sleep_start();
    }
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

    // Power on display rail; also enables BATT_PIN ADC via POWER_EN
    epd_poweron();
    delay(10);
    float batt_v   = battVolts();
    int   batt_pct = battPercent(batt_v);
    Serial.printf("Battery: %.2fV  %d%%\n", batt_v, batt_pct);

    // WiFi
    StaleReason stale = FRESH;
    if (!connectWiFi()) {
        Serial.println("WiFi timeout");
        stale = NO_WIFI;
    } else {
        Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
    }

    WeatherCache current = {};
    ForecastSlot forecast[FORECAST_COUNT] = {};
    char date_str[20] = "---";
    StationSlot stations[2] = {};
    strlcpy(stations[0].label, STATION_0_LABEL, sizeof(stations[0].label));
    stations[0].temp_f = stations[0].humidity = NAN;
    strlcpy(stations[1].label, STATION_1_LABEL, sizeof(stations[1].label));
    stations[1].temp_f = stations[1].humidity = NAN;

    if (stale == FRESH) {
        bool cur_ok   = fetchCurrent(current);
        bool fcast_ok = fetchForecast(forecast, date_str, sizeof(date_str));
        if (!cur_ok || !fcast_ok) {
            delay(1000);
            if (!cur_ok)   cur_ok   = fetchCurrent(current);
            if (!fcast_ok) fcast_ok = fetchForecast(forecast, date_str, sizeof(date_str));
        }
        Serial.printf("current=%d forecast=%d\n", cur_ok, fcast_ok);

        if (cur_ok && fcast_ok) {
            r_current = current;
            memcpy(r_forecast, forecast, sizeof(r_forecast));
            strlcpy(r_date, date_str, sizeof(r_date));
            r_valid = true;
        } else {
            stale = DATA_ERR;
        }

        fetchStations(stations);
        publishBattery(batt_pct);
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }

    if (stale != FRESH && r_valid) {
        current = r_current;
        memcpy(forecast, r_forecast, sizeof(forecast));
        strlcpy(date_str, r_date, sizeof(date_str));
        Serial.println("Using cached data");
    }

    renderFrame(current, forecast, batt_v, batt_pct, date_str, stale, stations);

    epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), framebuffer);
    epd_poweroff_all();

    Serial.printf("Sleeping %llu s\n", SLEEP_US / 1000000ULL);
    esp_sleep_enable_timer_wakeup(SLEEP_US);
    esp_deep_sleep_start();
}

void loop() {}
