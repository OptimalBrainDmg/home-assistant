#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include "secrets.h"

// ══════════════════════════════════════════════════════════════════════════════
// CONFIGURATION — edit this section only
// ══════════════════════════════════════════════════════════════════════════════

// Uncomment exactly one mode:
// #define MODE_MULTI_STRIP   // independent strips, each on its own data pin
#define MODE_SEGMENTS         // one physical strip divided into named segments

// ── Multi-strip configuration (MODE_MULTI_STRIP) ───────────────────────────────
#if defined(MODE_MULTI_STRIP)

struct StripConfig { const char* name; uint8_t pin; uint16_t numLeds; };
static const StripConfig STRIPS[] = {
  { "Desk Strip",  21, 30 },
  { "Shelf Strip", 14, 15 },
};
#define NUM_ZONES (sizeof(STRIPS) / sizeof(STRIPS[0]))

// ── Segment configuration (MODE_SEGMENTS) ─────────────────────────────────────
#elif defined(MODE_SEGMENTS)

#define SEG_PIN      21
#define SEG_NUM_LEDS 130    // total LEDs in the physical strip

struct SegmentConfig { const char* name; uint16_t start; uint16_t count; };
static const SegmentConfig SEGMENTS[] = {
  { "Middle Shelf", 0,  65 },
  { "Bottom Shelf", 65, 65 },
};
#define NUM_ZONES (sizeof(SEGMENTS) / sizeof(SEGMENTS[0]))

#else
#  error "Select a mode: uncomment MODE_MULTI_STRIP or MODE_SEGMENTS above"
#endif

// ── Brightness cap (both modes) ────────────────────────────────────────────────
// Scales the full 0–255 brightness range to 0–(MAX_BRIGHTNESS_SCALE * 255).
// Useful for keeping peak current within the power supply rating.
// 1.0 = no cap, 0.9 = 90% max output, etc.
#define MAX_BRIGHTNESS_SCALE 1.0f

// ══════════════════════════════════════════════════════════════════════════════
// END OF CONFIGURATION
// ══════════════════════════════════════════════════════════════════════════════

// ── Per-zone LED state ─────────────────────────────────────────────────────────
struct ZoneState { bool on; uint8_t brightness, r, g, b; };
ZoneState zoneState[NUM_ZONES];

// ── NeoPixel instances ─────────────────────────────────────────────────────────
#if defined(MODE_MULTI_STRIP)
Adafruit_NeoPixel* pixelStrips[NUM_ZONES];
#else
Adafruit_NeoPixel  pixelStrip(SEG_NUM_LEDS, SEG_PIN, NEO_GRB + NEO_KHZ800);
#endif

// ── MQTT / WiFi ────────────────────────────────────────────────────────────────
String deviceId;
String commandTopics[NUM_ZONES];
String stateTopics[NUM_ZONES];
String discoveryTopics[NUM_ZONES];

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

// ── Apply LED state for zone i ─────────────────────────────────────────────────
void applyZone(int i) {
  ZoneState& s = zoneState[i];
  uint8_t bri = (uint8_t)(s.brightness * MAX_BRIGHTNESS_SCALE);
#if defined(MODE_MULTI_STRIP)
  Adafruit_NeoPixel& strip = *pixelStrips[i];
  if (!s.on) {
    strip.clear();
  } else {
    strip.fill(strip.Color(
      (uint16_t)s.r * bri / 255,
      (uint16_t)s.g * bri / 255,
      (uint16_t)s.b * bri / 255
    ));
  }
  strip.show();
#else
  uint32_t color = s.on
    ? pixelStrip.Color(
        (uint16_t)s.r * bri / 255,
        (uint16_t)s.g * bri / 255,
        (uint16_t)s.b * bri / 255)
    : 0;
  for (uint16_t j = SEGMENTS[i].start; j < SEGMENTS[i].start + SEGMENTS[i].count; j++)
    pixelStrip.setPixelColor(j, color);
  pixelStrip.show();
#endif
}

// ── Publish state for zone i ───────────────────────────────────────────────────
void publishState(int i) {
  ZoneState& s = zoneState[i];
  StaticJsonDocument<192> doc;
  doc["state"]      = s.on ? "ON" : "OFF";
  doc["color_mode"] = "rgb";
  doc["brightness"] = s.brightness;
  JsonObject color  = doc.createNestedObject("color");
  color["r"] = s.r;
  color["g"] = s.g;
  color["b"] = s.b;
  char buf[192];
  serializeJson(doc, buf);
  mqtt.publish(stateTopics[i].c_str(), buf, /*retain=*/true);
}

// ── MQTT command handler ───────────────────────────────────────────────────────
void onMessage(char* topic, byte* payload, unsigned int len) {
  Serial.print("RX: "); Serial.print(topic);
  Serial.print(" "); Serial.write(payload, len); Serial.println();
  for (int i = 0; i < (int)NUM_ZONES; i++) {
    if (commandTopics[i] != topic) continue;

    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, payload, len)) return;

    ZoneState& s = zoneState[i];
    if (!doc["state"].isNull())      s.on         = doc["state"] == "ON";
    if (!doc["brightness"].isNull()) s.brightness = doc["brightness"].as<uint8_t>();
    if (!doc["color"]["r"].isNull()) {
      s.r = doc["color"]["r"].as<uint8_t>();
      s.g = doc["color"]["g"].as<uint8_t>();
      s.b = doc["color"]["b"].as<uint8_t>();
    }

    applyZone(i);
    publishState(i);
    return;
  }
}

// ── MQTT discovery for zone i ──────────────────────────────────────────────────
void publishDiscovery(int i) {
#if defined(MODE_MULTI_STRIP)
  const char* zoneName = STRIPS[i].name;
#else
  const char* zoneName = SEGMENTS[i].name;
#endif

  String dev = "{\"identifiers\":[\"" + deviceId + "\"],"
               "\"name\":\"HUZZAH32 LED Strip\","
               "\"model\":\"Adafruit HUZZAH32 ESP32 Feather\","
               "\"manufacturer\":\"Adafruit\"}";

  String cfg =
    "{\"name\":\"" + String(zoneName) + "\","
    "\"unique_id\":\"" + deviceId + "_zone" + i + "\","
    "\"schema\":\"json\","
    "\"command_topic\":\"" + commandTopics[i] + "\","
    "\"state_topic\":\"" + stateTopics[i] + "\","
    "\"supported_color_modes\":[\"rgb\"],"
    "\"device\":" + dev + "}";

  mqtt.publish(discoveryTopics[i].c_str(), cfg.c_str(), /*retain=*/true);
}

// ── WiFi ───────────────────────────────────────────────────────────────────────
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
}

// ── MQTT connection ────────────────────────────────────────────────────────────
void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Connecting to MQTT...");
    if (mqtt.connect(deviceId.c_str(), MQTT_USER, MQTT_PASS)) {
      Serial.println(" connected.");
      for (int i = 0; i < (int)NUM_ZONES; i++) {
        bool ok = mqtt.subscribe(commandTopics[i].c_str());
        Serial.println(String("sub ") + commandTopics[i] + (ok ? " OK" : " FAIL"));
        publishDiscovery(i);
        publishState(i);
      }
    } else {
      Serial.println(" failed (rc=" + String(mqtt.state()) + "), retry in 5s");
      delay(5000);
    }
  }
}

// ── Setup ──────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  deviceId = "huzzah32_" + String((uint32_t)(ESP.getEfuseMac() >> 24), HEX);

  for (int i = 0; i < (int)NUM_ZONES; i++) {
    zoneState[i]       = { false, 255, 255, 255, 255 };
    commandTopics[i]   = "home/" + deviceId + "/light/" + i + "/set";
    stateTopics[i]     = "home/" + deviceId + "/light/" + i + "/state";
    discoveryTopics[i] = "homeassistant/light/" + deviceId + "_zone" + i + "/config";
  }

#if defined(MODE_MULTI_STRIP)
  for (int i = 0; i < (int)NUM_ZONES; i++) {
    pixelStrips[i] = new Adafruit_NeoPixel(
      STRIPS[i].numLeds, STRIPS[i].pin, NEO_GRB + NEO_KHZ800);
    pixelStrips[i]->begin();
    pixelStrips[i]->clear();
    pixelStrips[i]->show();
  }
#else
  pixelStrip.begin();
  pixelStrip.clear();
  pixelStrip.show();
#endif

  connectWiFi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(1024);
  mqtt.setCallback(onMessage);
  connectMQTT();
}

// ── Loop ───────────────────────────────────────────────────────────────────────
void loop() {
  connectWiFi();
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();
}
