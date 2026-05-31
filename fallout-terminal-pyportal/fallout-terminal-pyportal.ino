// PyPortal Fallout-terminal dashboard for Home Assistant.
// Displays ADT7410 temp and ambient light readings, publishes them to HA via
// MQTT auto-discovery, and shows toggle buttons for light zones defined in
// /config.json on the SD card.

#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <TouchScreen.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <WiFiNINA.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_ADT7410.h>

// ── HARDWARE PINS (PyPortal original) ────────────────────────────────────────
#define TFT_D0        34
#define TFT_WR        26
#define TFT_DC        10
#define TFT_CS        11
#define TFT_RST       24
#define TFT_RD         9
#define TFT_BACKLIGHT 25
#define SD_CS         32

#define TOUCH_XP  A5
#define TOUCH_YP  A4
#define TOUCH_XM  A7
#define TOUCH_YM  A6

// Analog light sensor pin. Reads 0–1023 (uncalibrated ADC counts).
#define LIGHT_PIN A2

// Audio: DAC output on A0, amplifier enable on pin 50 (PA27).
#define SPEAKER_SHUTDOWN 50
#define AUDIO_OUT        A0
#define AUDIO_GAIN       2  // software gain multiplier (1 = unity, higher = louder, clips if WAVs near full scale)
#define SOUND_POWERON   "robco/sounds/poweron.wav"
#define SOUND_POWEROFF  "robco/sounds/poweroff.wav"
#define SOUND_BRI_DOWN  "robco/sounds/single4.wav"
#define SOUND_BRI_UP    "robco/sounds/single6.wav"
#define SOUND_ENTER     "robco/sounds/enter3.wav"
#define SOUND_EXIT      "robco/sounds/single1.wav"
#define SOUND_LIMIT     "robco/sounds/enter1.wav"

// ── DISPLAY ───────────────────────────────────────────────────────────────────
#define COLOR_BG     0x0000   // black
#define COLOR_GREEN  0x37E0   // terminal phosphor green
#define COLOR_DIM    0x1320   // dim green for inactive / secondary text
#define COLOR_TITLE  0x57E0   // bright green for header
#define COLOR_BTN_ON 0x0180   // dark green fill for active zone buttons

#define ROTATION 1            // landscape; USB connector at top
#define SCR_W    320
#define SCR_H    240
#define LINE_H    17          // pixels between text baselines (FreeMono9pt7b)

// ── TOUCH ─────────────────────────────────────────────────────────────────────
// Calibration for touchpaint_pyportal defaults. Adjust if touch feels off.
#define TOUCH_X_MIN   130
#define TOUCH_X_MAX   900
#define TOUCH_Y_MIN   111
#define TOUCH_Y_MAX   907
#define TOUCH_P_MIN   400
#define TOUCH_P_MAX  1200
#define TOUCH_DEBOUNCE_MS 400UL

// ── TIMING ────────────────────────────────────────────────────────────────────
#define SENSOR_MS       30000UL   // publish temp + light every 30 s
#define CLOCK_MS        60000UL   // redraw clock every 1 min (no seconds shown)
#define RECONN_MS        5000UL   // retry MQTT every 5 s
#define LIGHTING_TIMEOUT 60000UL  // return to main screen after 1 min idle

// ── CREDENTIALS (loaded from /config.json on SD card) ────────────────────────
static char cfgWifiSsid[64];
static char cfgWifiPass[64];
static char cfgMqttHost[64];
static int  cfgMqttPort = 1883;
static char cfgMqttUser[64];
static char cfgMqttPass[64];

// ── ZONES ─────────────────────────────────────────────────────────────────────
#define MAX_ZONES     4
#define ZONE_NAME_LEN 20
#define TOPIC_LEN     80

struct ZoneConfig {
  char name[ZONE_NAME_LEN];
  char stateTopic[TOPIC_LEN];
  char commandTopic[TOPIC_LEN];
  bool isOn;
  int  brightness;  // 0–100 %
};

static int        numZones      = 0;
static ZoneConfig zones[MAX_ZONES];
static int        tzOffsetHours = 0;
static char       cfgOutsideTempTopic[TOPIC_LEN];
static char       cfgWeatherTopic[TOPIC_LEN];
static char       cfgHumidityTopic[TOPIC_LEN];
static char       cfgTzTopic[TOPIC_LEN];
static int        tzDstOffset = 0;

// ── LAYOUT Y-POSITIONS (text baselines for FreeMono9pt7b) ────────────────────
#define Y_HDR1   13
#define Y_HDR2   30
#define Y_SEP1   40
#define Y_DT     54
#define Y_SEP2   64
#define Y_TEMP    78
#define Y_OUTSIDE  95
#define Y_WEATHER 112
#define Y_SEP3   125
#define Y_CTLBL  139
#define Y_ZONES  156   // first zone button; subsequent buttons at +LINE_H each
#define Y_SEP4   218   // separator above safe control section
#define Y_STAT   231   // safe control line

static int zoneBtnTop[MAX_ZONES];  // top-y of each touch target

// Declared here so Arduino's auto-forward-declaration pass sees Date before
// epochToDate's return type is emitted.
struct Date { int year, month, day; };  // month 0-based

// ── STATE ─────────────────────────────────────────────────────────────────────
enum Screen { SCREEN_MAIN, SCREEN_LIGHTING };
static Screen        currentScreen  = SCREEN_MAIN;
static int           lightingBtnH   = 0;

static char          deviceId[24];
static bool          hasTempSensor  = false;
static char          outsideTempStr[24] = "--";
static char          weatherStr[24]     = "--";
static char          humidityStr[16]    = "--";
static float         lastTempC      = 0;
static int           lastLightRaw   = 0;
static unsigned long lastSensorMs   = 0;
static unsigned long lastClockMs    = 0;
static unsigned long lastReconnMs   = 0;
static unsigned long lastTouchMs    = 0;

Adafruit_ILI9341 tft(tft8bitbus, TFT_D0, TFT_WR, TFT_DC, TFT_CS, TFT_RST, TFT_RD);
TouchScreen      ts(TOUCH_XP, TOUCH_YP, TOUCH_XM, TOUCH_YM, 300);
Adafruit_ADT7410 adt7410;
WiFiClient       wifiClient;
WiFiUDP          udpClient;
NTPClient        ntp(udpClient, "pool.ntp.org");
PubSubClient     mqtt(wifiClient);

// ── DISPLAY HELPERS ───────────────────────────────────────────────────────────

static void clearLine(int y) {
  tft.fillRect(0, y - 12, SCR_W, LINE_H, COLOR_BG);
}

static void drawSep(int y) {
  tft.drawFastHLine(0, y, SCR_W, COLOR_DIM);
}

// Clears the line then prints str at (4, y) with given font and color.
static void printLine(int y, const char* str, uint16_t color,
                      const GFXfont* font = &FreeMono9pt7b) {
  clearLine(y);
  tft.setFont(font);
  tft.setTextColor(color);
  tft.setCursor(4, y);
  tft.print(str);
}

// ── DATE MATH ─────────────────────────────────────────────────────────────────

static bool isLeap(int y) { return (y%4==0 && y%100!=0) || y%400==0; }

static Date epochToDate(unsigned long epoch) {
  static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  long days = epoch / 86400UL;
  Date d;
  d.year = 1970;
  while (true) {
    int diy = isLeap(d.year) ? 366 : 365;
    if (days < diy) break;
    days -= diy;
    d.year++;
  }
  for (d.month = 0; d.month < 12; d.month++) {
    int md = mdays[d.month] + (d.month == 1 && isLeap(d.year) ? 1 : 0);
    if (days < md) break;
    days -= md;
  }
  d.day = days + 1;
  return d;
}

// ── BOOT SCREEN ───────────────────────────────────────────────────────────────

static int bootLine = 0;

static void renderBootMsg(const char* msg) {
  if (bootLine == 0) {
    tft.fillScreen(COLOR_BG);
    tft.setFont(&FreeMonoBold9pt7b);
    tft.setTextColor(COLOR_TITLE);
    tft.setCursor(4, Y_HDR1);
    tft.print(F("ROBCO INDUSTRIES UNIFIED OS"));
    tft.setFont(&FreeMono9pt7b);
    tft.setTextColor(COLOR_DIM);
    tft.setCursor(4, Y_HDR2);
    tft.print(F("COPYRIGHT 2075-2077 ROBCO"));
    drawSep(Y_SEP1);
  }
  int y = 62 + bootLine * LINE_H;
  tft.setFont(&FreeMono9pt7b);
  tft.setTextColor(COLOR_GREEN);
  tft.setCursor(4, y);
  tft.print(F("> "));
  tft.print(msg);
  bootLine++;
}

// ── MAIN SCREEN ───────────────────────────────────────────────────────────────

static void renderZoneButton(int i) {
  int y = Y_ZONES + i * LINE_H;
  zoneBtnTop[i] = y - 12;

  // Format: "[NAME..............  75%]" / "[NAME.............. OFF]"
  char line[28];
  char padded[17];
  char stateStr[5];
  int nlen = strlen(zones[i].name);
  if (nlen > 16) nlen = 16;
  memcpy(padded, zones[i].name, nlen);
  for (int j = nlen; j < 16; j++) padded[j] = '.';
  padded[16] = '\0';
  if (zones[i].isOn)
    snprintf(stateStr, sizeof(stateStr), "%3d%%", zones[i].brightness);
  else
    strlcpy(stateStr, " OFF", sizeof(stateStr));
  snprintf(line, sizeof(line), "[%s %s]", padded, stateStr);

  tft.fillRect(0, y - 12, SCR_W, LINE_H, COLOR_BG);
  tft.setFont(&FreeMono9pt7b);
  tft.setTextColor(zones[i].isOn ? COLOR_GREEN : COLOR_DIM);
  tft.setCursor(4, y);
  tft.print(line);
}

static void renderFullScreen() {
  tft.fillScreen(COLOR_BG);

  tft.setFont(&FreeMonoBold9pt7b);
  tft.setTextColor(COLOR_TITLE);
  tft.setCursor(4, Y_HDR1);
  tft.print(F("ROBCO INDUSTRIES UNIFIED OS"));

  tft.setFont(&FreeMono9pt7b);
  tft.setTextColor(COLOR_DIM);
  tft.setCursor(4, Y_HDR2);
  tft.print(F("COPYRIGHT 2075-2077 ROBCO"));

  drawSep(Y_SEP1);
  drawSep(Y_SEP2);
  drawSep(Y_SEP3);

  tft.setFont(&FreeMono9pt7b);
  tft.setTextColor(COLOR_DIM);
  tft.setCursor(4, Y_CTLBL);
  tft.print(F(">LIGHTING CONTROL"));

  // Placeholders until data arrives
  tft.setTextColor(COLOR_DIM);
  tft.setCursor(4, Y_DT);
  tft.print(F(">--:--"));
  tft.setCursor(4, Y_TEMP);
  tft.print(F(">TEMP: --.-C"));
  tft.setCursor(4, Y_OUTSIDE);
  tft.print(F(">OUT: --  HUM: --"));
  tft.setCursor(4, Y_WEATHER);
  tft.print(F(">WX:  --"));

  for (int i = 0; i < numZones; i++) renderZoneButton(i);

  drawSep(Y_SEP4);
  tft.fillRect(0, Y_STAT - 12, SCR_W, LINE_H, COLOR_BG);
  tft.setFont(&FreeMono9pt7b);
  tft.setTextColor(COLOR_DIM);
  tft.setCursor(4, Y_STAT);
  tft.print(F(">SAFE CONTROL"));
  tft.setCursor(228, Y_STAT);
  tft.print(F("[LOCKED]"));
}

// ── LIGHTING CONTROL SCREEN ───────────────────────────────────────────────────

#define LIGHTING_HDR_H  37   // pixels reserved for header; tap here to go back
#define LBTN_LABEL_END 220   // x of divider between label and [-] section
#define LBTN_MINUS_END 265   // x of divider between [-] and [+] section

static void renderLightingButton(int i) {
  if (lightingBtnH == 0) return;
  int y  = LIGHTING_HDR_H + i * lightingBtnH;
  int bh = lightingBtnH;
  uint16_t fg = zones[i].isOn ? COLOR_GREEN : COLOR_DIM;
  uint16_t bg = zones[i].isOn ? COLOR_BTN_ON : COLOR_BG;

  // Clear full button row, then fill label section with zone colour
  tft.fillRect(4, y + 3, SCR_W - 8, bh - 6, COLOR_BG);
  tft.fillRect(4, y + 3, LBTN_LABEL_END - 4, bh - 6, bg);

  // Outer border + two vertical dividers
  tft.drawRect(4, y + 3, SCR_W - 8, bh - 6, fg);
  tft.drawFastVLine(LBTN_LABEL_END, y + 4, bh - 8, fg);
  tft.drawFastVLine(LBTN_MINUS_END, y + 4, bh - 8, fg);

  // Two-line label: zone name on first line, state (% or OFF) on second
  char stateStr[5];
  if (zones[i].isOn)
    snprintf(stateStr, sizeof(stateStr), "%3d%%", zones[i].brightness);
  else
    strlcpy(stateStr, " OFF", sizeof(stateStr));

  int textY1 = y + bh / 3 + 4;
  int textY2 = y + 2 * bh / 3 + 4;

  tft.setFont(&FreeMonoBold9pt7b);
  tft.setTextColor(fg);
  tft.setCursor(8, textY1);
  tft.print(zones[i].name);
  tft.setCursor(8, textY2);
  tft.print(stateStr);

  // [-] and [+] centred vertically in the button
  int textYC = y + bh / 2 + 6;
  tft.setCursor(LBTN_LABEL_END + 6,  textYC);
  tft.print("[-]");
  tft.setCursor(LBTN_MINUS_END + 9, textYC);
  tft.print("[+]");
}

static void renderLightingScreen() {
  tft.fillScreen(COLOR_BG);

  tft.setFont(&FreeMonoBold9pt7b);
  tft.setTextColor(COLOR_TITLE);
  tft.setCursor(4, 16);
  tft.print(F("LIGHTING CONTROL"));

  tft.setFont(&FreeMono9pt7b);
  tft.setTextColor(COLOR_DIM);
  tft.setCursor(4, 30);
  tft.print(F(">TAP HEADER TO GO BACK"));

  drawSep(LIGHTING_HDR_H);

  lightingBtnH = numZones > 0 ? (SCR_H - LIGHTING_HDR_H) / numZones : (SCR_H - LIGHTING_HDR_H);
  Serial.print("lighting screen: btnH="); Serial.print(lightingBtnH);
  Serial.print(" header=0-"); Serial.println(LIGHTING_HDR_H - 1);
  for (int i = 0; i < numZones; i++) {
    int btnY = LIGHTING_HDR_H + i * lightingBtnH;
    Serial.print("  zone "); Serial.print(i); Serial.print(" ("); Serial.print(zones[i].name);
    Serial.print("): sy="); Serial.print(btnY); Serial.print("-"); Serial.println(btnY + lightingBtnH - 1);
    renderLightingButton(i);
  }
}

static void updateZoneDisplay(int i) {
  if (currentScreen == SCREEN_MAIN)
    renderZoneButton(i);
  else
    renderLightingButton(i);
}

static void updateDateTime() {
  if (!ntp.isTimeSet()) return;
  unsigned long epoch = ntp.getEpochTime();
  Date d = epochToDate(epoch);
  static const char* mnames[12] =
    {"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
  static const char* dnames[7] =
    {"SUN","MON","TUE","WED","THU","FRI","SAT"};
  int dow = (epoch / 86400UL + 4) % 7;  // epoch day 0 = Thu

  char buf[48];
  snprintf(buf, sizeof(buf), ">%s %s %02d %04d  %02d:%02d",
           dnames[dow], mnames[d.month], d.day, d.year,
           ntp.getHours(), ntp.getMinutes());
  printLine(Y_DT, buf, COLOR_GREEN);
}

static void updateSensors() {
  if (hasTempSensor) lastTempC = adt7410.readTempC();
  lastLightRaw = analogRead(LIGHT_PIN);

  if (currentScreen != SCREEN_MAIN) return;

  char buf[32];
  if (hasTempSensor) {
    float f = lastTempC * 9.0f / 5.0f + 32.0f;
    snprintf(buf, sizeof(buf), ">TEMP: %.1fC (%.1fF)", lastTempC, f);
  } else {
    snprintf(buf, sizeof(buf), ">TEMP: NO SENSOR");
  }
  printLine(Y_TEMP, buf, COLOR_GREEN);
}

static void updateOutsideDisplay() {
  if (currentScreen != SCREEN_MAIN) return;
  char buf[40];
  snprintf(buf, sizeof(buf), ">OUT: %s  HUM: %s", outsideTempStr, humidityStr);
  printLine(Y_OUTSIDE, buf, COLOR_GREEN);
  snprintf(buf, sizeof(buf), ">WX:  %s", weatherStr);
  printLine(Y_WEATHER, buf, COLOR_GREEN);
}

// ── SD CONFIG ─────────────────────────────────────────────────────────────────

static bool loadConfig() {
  if (!SD.begin(SD_CS)) {
    renderBootMsg("SD INIT FAILED- CHECK CARD");
    return false;
  }
  File f = SD.open("config.jsn");
  if (!f) {
    renderBootMsg("CONFIG.JSON NOT FOUND ON SD");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    renderBootMsg("CONFIG.JSON PARSE ERROR");
    return false;
  }

  strlcpy(cfgWifiSsid,  doc["wifi_ssid"]  | "", sizeof(cfgWifiSsid));
  strlcpy(cfgWifiPass,  doc["wifi_pass"]  | "", sizeof(cfgWifiPass));
  strlcpy(cfgMqttHost,  doc["mqtt_host"]  | "homeassistant.local", sizeof(cfgMqttHost));
  cfgMqttPort = doc["mqtt_port"]  | 1883;
  strlcpy(cfgMqttUser,  doc["mqtt_user"]  | "", sizeof(cfgMqttUser));
  strlcpy(cfgMqttPass,  doc["mqtt_pass"]  | "", sizeof(cfgMqttPass));
  tzOffsetHours = doc["tz_offset"] | 0;
  strlcpy(cfgOutsideTempTopic, doc["outside_temp_topic"] | "", sizeof(cfgOutsideTempTopic));
  strlcpy(cfgWeatherTopic,     doc["weather_topic"]       | "", sizeof(cfgWeatherTopic));
  strlcpy(cfgHumidityTopic,    doc["humidity_topic"]      | "", sizeof(cfgHumidityTopic));
  strlcpy(cfgTzTopic,         doc["tz_topic"]             | "", sizeof(cfgTzTopic));

  JsonArray arr = doc["zones"].as<JsonArray>();
  numZones = 0;
  for (JsonObject z : arr) {
    if (numZones >= MAX_ZONES) break;
    strlcpy(zones[numZones].name,         z["name"]          | "ZONE", ZONE_NAME_LEN);
    strlcpy(zones[numZones].stateTopic,   z["state_topic"]   | "",     TOPIC_LEN);
    strlcpy(zones[numZones].commandTopic, z["command_topic"] | "",     TOPIC_LEN);
    zones[numZones].isOn = false;
    zones[numZones].brightness = 100;
    numZones++;
  }
  return true;
}

// ── WIFI ──────────────────────────────────────────────────────────────────────

static void connectWiFi() {
  renderBootMsg("CONNECTING TO WIFI...");
  int status = WL_IDLE_STATUS;
  while (status != WL_CONNECTED) {
    status = WiFi.begin(cfgWifiSsid, cfgWifiPass);
    if (status != WL_CONNECTED) delay(3000);
  }
  byte mac[6];
  WiFi.macAddress(mac);
  snprintf(deviceId, sizeof(deviceId), "pyportal_%02x%02x%02x",
           mac[3], mac[4], mac[5]);
}

// ── MQTT ──────────────────────────────────────────────────────────────────────

static void publishDiscovery() {
  char topic[80], payload[512];
  char stateTopicTemp[64], stateTopicLight[64];
  snprintf(stateTopicTemp,  sizeof(stateTopicTemp),  "home/%s/sensor/temperature", deviceId);
  snprintf(stateTopicLight, sizeof(stateTopicLight), "home/%s/sensor/light",       deviceId);

  // Temperature sensor
  snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_temperature/config", deviceId);
  snprintf(payload, sizeof(payload),
    "{\"name\":\"Temperature\","
    "\"device_class\":\"temperature\","
    "\"state_topic\":\"%s\","
    "\"unit_of_measurement\":\"\\u00b0C\","
    "\"value_template\":\"{{value}}\","
    "\"unique_id\":\"%s_temperature\","
    "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"Fallout Terminal\","
    "\"model\":\"Adafruit PyPortal\"}}",
    stateTopicTemp, deviceId, deviceId);
  mqtt.publish(topic, payload, true);

  // Light level sensor
  snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_light/config", deviceId);
  snprintf(payload, sizeof(payload),
    "{\"name\":\"Light Level\","
    "\"state_topic\":\"%s\","
    "\"unit_of_measurement\":\"ADC\","
    "\"value_template\":\"{{value}}\","
    "\"unique_id\":\"%s_light\","
    "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"Fallout Terminal\","
    "\"model\":\"Adafruit PyPortal\"}}",
    stateTopicLight, deviceId, deviceId);
  mqtt.publish(topic, payload, true);
}

static void publishSensors() {
  char topic[64], buf[16];

  if (hasTempSensor) {
    snprintf(topic, sizeof(topic), "home/%s/sensor/temperature", deviceId);
    snprintf(buf, sizeof(buf), "%.1f", lastTempC);
    mqtt.publish(topic, buf);
  }

  snprintf(topic, sizeof(topic), "home/%s/sensor/light", deviceId);
  snprintf(buf, sizeof(buf), "%d", lastLightRaw);
  mqtt.publish(topic, buf);
}

static void mqttCallback(char* topic, byte* payload, unsigned int len) {
  auto copyStr = [](char* dst, size_t dstSize, byte* src, unsigned int n) {
    unsigned int copy = n < dstSize - 1 ? n : dstSize - 1;
    memcpy(dst, src, copy);
    dst[copy] = '\0';
  };

  if (strlen(cfgTzTopic) && strcmp(topic, cfgTzTopic) == 0) {
    char tmp[8] = {};
    unsigned int copy = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;
    memcpy(tmp, payload, copy);
    tzDstOffset = atoi(tmp);
    ntp.setTimeOffset((tzOffsetHours + tzDstOffset) * 3600L);
    return;
  }

  if (strlen(cfgOutsideTempTopic) && strcmp(topic, cfgOutsideTempTopic) == 0) {
    copyStr(outsideTempStr, sizeof(outsideTempStr), payload, len);
    updateOutsideDisplay();
    return;
  }
  if (strlen(cfgWeatherTopic) && strcmp(topic, cfgWeatherTopic) == 0) {
    copyStr(weatherStr, sizeof(weatherStr), payload, len);
    updateOutsideDisplay();
    return;
  }
  if (strlen(cfgHumidityTopic) && strcmp(topic, cfgHumidityTopic) == 0) {
    copyStr(humidityStr, sizeof(humidityStr), payload, len);
    updateOutsideDisplay();
    return;
  }

  for (int i = 0; i < numZones; i++) {
    if (strcmp(topic, zones[i].stateTopic) != 0) continue;
    JsonDocument doc;
    if (deserializeJson(doc, payload, len) != DeserializationError::Ok) break;
    const char* state = doc["state"] | "";
    bool nowOn = (strcmp(state, "ON") == 0);
    int rawBri = doc["brightness"] | -1;
    int newBri = zones[i].brightness;
    if (rawBri >= 0) {
      newBri = rawBri * 100 / 255;
      newBri = ((newBri + 2) / 5) * 5;  // snap to nearest 5%
      if (newBri < 5)   newBri = 5;
      if (newBri > 100) newBri = 100;
    }
    if (nowOn != zones[i].isOn || newBri != zones[i].brightness) {
      zones[i].isOn = nowOn;
      zones[i].brightness = newBri;
      updateZoneDisplay(i);
    }
    break;
  }
}


// ── AUDIO ─────────────────────────────────────────────────────────────────────

// Plays a 16-bit mono PCM WAV from the SD card by streaming samples directly
// to the SAMD51 DAC (A0). Blocks until playback completes. No-op if the file
// cannot be opened. Uses micros() to pace output at the file's own sample rate
// and resyncs after SD read stalls so timing drift stays bounded.
static void playSound(const char* path) {
  File f = SD.open(path);
  if (!f) return;

  // Parse RIFF/WAVE header: read fixed 12-byte RIFF chunk, then scan sub-chunks
  // for "fmt " (to get sample rate) and "data" (to find where PCM starts).
  // This handles macOS-optimized WAVs where data can start well past byte 44.
  uint8_t riff[12];
  if (f.read(riff, 12) < 12 ||
      riff[0] != 'R' || riff[1] != 'I' || riff[2] != 'F' || riff[3] != 'F' ||
      riff[8] != 'W' || riff[9] != 'A' || riff[10] != 'V' || riff[11] != 'E') {
    f.close(); return;
  }

  uint32_t sampleRate = 0;
  bool foundData = false;

  // Skip n bytes by reading; f.seek() silently fails for large forward jumps
  // on the Arduino SD library, so read-and-discard is more reliable.
  auto skipBytes = [&f](uint32_t n) -> bool {
    uint8_t tmp[64];
    while (n > 0) {
      uint32_t r = n < sizeof(tmp) ? n : sizeof(tmp);
      if ((uint32_t)f.read(tmp, r) < r) return false;
      n -= r;
    }
    return true;
  };

  while (f.available() >= 8) {
    uint8_t chunkHdr[8];
    if (f.read(chunkHdr, 8) < 8) break;
    uint32_t chunkSize = chunkHdr[4] | ((uint32_t)chunkHdr[5] << 8)
                       | ((uint32_t)chunkHdr[6] << 16) | ((uint32_t)chunkHdr[7] << 24);
    if (chunkHdr[0]=='f' && chunkHdr[1]=='m' && chunkHdr[2]=='t' && chunkHdr[3]==' ') {
      uint8_t fmt[16];
      uint32_t toRead = chunkSize < 16 ? chunkSize : 16;
      if (f.read(fmt, toRead) < (int)toRead) break;
      sampleRate = fmt[4] | ((uint32_t)fmt[5] << 8)
                 | ((uint32_t)fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
      if (chunkSize > toRead && !skipBytes(chunkSize - toRead)) break;
    } else if (chunkHdr[0]=='d' && chunkHdr[1]=='a' && chunkHdr[2]=='t' && chunkHdr[3]=='a') {
      foundData = true;
      break;  // file position is now at first PCM sample
    } else {
      if (!skipBytes(chunkSize)) break;
    }
  }

  if (!foundData || sampleRate == 0) { f.close(); return; }
  uint32_t periodUs = 1000000UL / sampleRate;

  analogWrite(AUDIO_OUT, 2048);           // ensure DAC is centred before amp on
  digitalWrite(SPEAKER_SHUTDOWN, HIGH);   // enable amplifier
  delayMicroseconds(500);                 // let amp stabilise

  static uint8_t buf[512];
  uint32_t nextUs = micros();

  while (f.available()) {
    int n = f.read(buf, sizeof(buf));
    for (int i = 0; i + 1 < n; i += 2) {
      int16_t s = (int16_t)(buf[i] | (buf[i + 1] << 8));
      int32_t g = (int32_t)s * AUDIO_GAIN;
      if (g > 32767) g = 32767;
      if (g < -32768) g = -32768;
      analogWrite(AUDIO_OUT, (uint16_t)((g + 32768) >> 4));
      nextUs += periodUs;
      uint32_t now = micros();
      if (nextUs > now)
        delayMicroseconds(nextUs - now);
      else
        nextUs = now;  // resync after SD stall
    }
  }

  analogWrite(AUDIO_OUT, 2048);         // return DAC to silence before amp off
  delayMicroseconds(500);
  digitalWrite(SPEAKER_SHUTDOWN, LOW);  // disable amplifier
  f.close();
}

// ── TOUCH ─────────────────────────────────────────────────────────────────────

// Maps raw touch coordinates to screen pixels for ROTATION 1 (landscape, USB top).
// If touch is inverted or swapped, adjust the map() arguments here.
static int touchToScrX(const TSPoint& p) {
  return constrain(map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, SCR_W - 1, 0), 0, SCR_W - 1);
}
static int touchToScrY(const TSPoint& p) {
  return constrain(map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, SCR_H - 1, 0), 0, SCR_H - 1);
}

static void handleTouch() {
  TSPoint p = ts.getPoint();
  // Restore driven pins that TouchScreen repurposes during a read
  pinMode(TOUCH_XM, OUTPUT); pinMode(TOUCH_YP, OUTPUT);

  if (p.z < TOUCH_P_MIN || p.z > TOUCH_P_MAX) return;

  int sx = touchToScrX(p);
  int sy = touchToScrY(p);
  Serial.print("touch raw="); Serial.print(p.x); Serial.print(","); Serial.print(p.y);
  Serial.print(" z="); Serial.print(p.z);
  Serial.print(" screen="); Serial.print(sx); Serial.print(","); Serial.println(sy);

  if (millis() - lastTouchMs < TOUCH_DEBOUNCE_MS) {
    Serial.println("touch debounced");
    return;
  }
  lastTouchMs = millis();

  if (currentScreen == SCREEN_MAIN) {
    Serial.println("main -> lighting screen");
    currentScreen = SCREEN_LIGHTING;
    renderLightingScreen();
    playSound(SOUND_ENTER);
    return;
  }

  // SCREEN_LIGHTING: header tap goes back, zone tap toggles
  if (sy < LIGHTING_HDR_H) {
    Serial.println("lighting -> main screen");
    currentScreen = SCREEN_MAIN;
    renderFullScreen();
    updateOutsideDisplay();
    updateDateTime();
    updateSensors();
    playSound(SOUND_EXIT);
    return;
  }

  for (int i = 0; i < numZones; i++) {
    int btnY = LIGHTING_HDR_H + i * lightingBtnH;
    Serial.print("zone "); Serial.print(i);
    Serial.print(" top="); Serial.print(btnY);
    Serial.print(" bottom="); Serial.println(btnY + lightingBtnH);
    if (sy >= btnY && sy < btnY + lightingBtnH) {
      char buf[48];
      if (sx < LBTN_LABEL_END) {
        // Toggle ON / OFF
        bool turningOn = !zones[i].isOn;
        Serial.print("zone "); Serial.print(i); Serial.print(" -> ");
        Serial.println(turningOn ? "ON" : "OFF");
        if (turningOn) {
          snprintf(buf, sizeof(buf), "{\"state\":\"ON\",\"brightness\":%d}",
                   zones[i].brightness * 255 / 100);
        } else {
          snprintf(buf, sizeof(buf), "{\"state\":\"OFF\"}");
        }
        mqtt.publish(zones[i].commandTopic, buf);
        zones[i].isOn = turningOn;
        renderLightingButton(i);
        playSound(turningOn ? SOUND_POWERON : SOUND_POWEROFF);
      } else {
        // Brightness -5% or +5%; minimum 5% (use toggle to turn off)
        int delta  = (sx < LBTN_MINUS_END) ? -5 : 5;
        int newBri = constrain(zones[i].brightness + delta, 5, 100);
        Serial.print("zone "); Serial.print(i); Serial.print(" brightness -> ");
        Serial.println(newBri);
        if (zones[i].isOn && newBri == zones[i].brightness) {
          playSound(SOUND_LIMIT);
        } else {
          zones[i].brightness = newBri;
          zones[i].isOn = true;
          snprintf(buf, sizeof(buf), "{\"state\":\"ON\",\"brightness\":%d}",
                   newBri * 255 / 100);
          mqtt.publish(zones[i].commandTopic, buf);
          renderLightingButton(i);
          playSound(delta < 0 ? SOUND_BRI_DOWN : SOUND_BRI_UP);
        }
      }
      break;
    }
  }
}

// ── SETUP ─────────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);

  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, LOW);  // keep off until display is cleared

  tft.begin();
  tft.setRotation(ROTATION);
  tft.fillScreen(COLOR_BG);          // clear default white before backlight on
  digitalWrite(TFT_BACKLIGHT, HIGH);

  // Amplifier starts off; only enabled during playSound() to prevent buzzing
  pinMode(SPEAKER_SHUTDOWN, OUTPUT);
  digitalWrite(SPEAKER_SHUTDOWN, LOW);
  analogWriteResolution(12);
  analogWrite(AUDIO_OUT, 2048);  // DAC to midpoint / silence

  // ADT7410 (I2C, address 0x48)
  Wire.begin();
  hasTempSensor = adt7410.begin();

  renderBootMsg("HARDWARE INITIALIZED");

  renderBootMsg("LOADING SD CONFIG...");
  if (!loadConfig()) {
    renderBootMsg("* FIX SD CARD AND RESET *");
    while (true) {}
  }
  playSound(SOUND_POWERON);

  connectWiFi();

  renderBootMsg("SYNCING REAL-TIME CLOCK...");
  ntp.setTimeOffset((tzOffsetHours + tzDstOffset) * 3600L);
  ntp.begin();
  ntp.update();

  mqtt.setServer(cfgMqttHost, cfgMqttPort);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(512);

  renderFullScreen();

  // Initial sensor read + publish
  updateSensors();
  publishSensors();
  lastSensorMs = millis();
}

// ── LOOP ──────────────────────────────────────────────────────────────────────

void loop() {
  // Maintain MQTT
  if (!mqtt.connected()) {
    unsigned long now = millis();
    if (now - lastReconnMs >= RECONN_MS) {
      lastReconnMs = now;
      if (WiFi.status() != WL_CONNECTED) WiFi.begin(cfgWifiSsid, cfgWifiPass);
      if (mqtt.connect(deviceId, cfgMqttUser, cfgMqttPass)) {
        for (int i = 0; i < numZones; i++) {
          if (strlen(zones[i].stateTopic)) mqtt.subscribe(zones[i].stateTopic);
        }
        if (strlen(cfgOutsideTempTopic)) mqtt.subscribe(cfgOutsideTempTopic);
        if (strlen(cfgWeatherTopic))     mqtt.subscribe(cfgWeatherTopic);
        if (strlen(cfgHumidityTopic))    mqtt.subscribe(cfgHumidityTopic);
        if (strlen(cfgTzTopic))         mqtt.subscribe(cfgTzTopic);
        publishDiscovery();
      }
    }
  }
  mqtt.loop();

  // Clock
  ntp.update();
  unsigned long now = millis();
  if (now - lastClockMs >= CLOCK_MS) {
    lastClockMs = now;
    if (currentScreen == SCREEN_MAIN) updateDateTime();
  }

  // Sensors
  if (now - lastSensorMs >= SENSOR_MS) {
    lastSensorMs = now;
    updateSensors();
    publishSensors();
  }

  handleTouch();

  // Return to main screen after 1 minute of inactivity on the lighting screen
  if (currentScreen == SCREEN_LIGHTING &&
      millis() - lastTouchMs >= LIGHTING_TIMEOUT) {
    Serial.println("lighting timeout -> main screen");
    currentScreen = SCREEN_MAIN;
    renderFullScreen();
    updateOutsideDisplay();
    updateDateTime();
    updateSensors();
    playSound(SOUND_EXIT);
  }
}
