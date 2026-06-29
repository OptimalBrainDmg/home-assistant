// MERRY CHRISTMAS LED sign controller.
// SK6812 RGBW NeoPixel strip, 89 LEDs, chain order C-H-R-I-S-T-M-A-S M-E-R-R-Y.
// HA entity: one JSON light with brightness + effect list (no color picker).
//
// Hardware: Seeed XIAO ESP32C6
// Data pin: D0 (change DATA_PIN below if wired differently)
// Power:    external 5V supply required; share GND with XIAO
//
// FQBN: esp32:esp32:XIAO_ESP32C6
// Build:  arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C6 xmas/
// Upload: arduino-cli upload  --fqbn esp32:esp32:XIAO_ESP32C6 --port <port> xmas/
// Serial: arduino-cli monitor --port <port> --config baudrate=115200

#include <WiFi.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include "secrets.h"

#define WDT_TIMEOUT_S 90
#define DATA_PIN      D0    // labeled D0 on XIAO ESP32C6; change if wired elsewhere
#define NUM_LEDS      89

Adafruit_NeoPixel strip(NUM_LEDS, DATA_PIN, NEO_GRBW + NEO_KHZ800);

// ── Letter map — C-H-R-I-S-T-M-A-S [space] M-E-R-R-Y ─────────────────────────
struct Letter { uint16_t start, count; };
static const Letter LETTERS[] = {
  { 0,  5},  // C
  { 5,  7},  // H
  {12,  7},  // R
  {19,  3},  // I
  {22,  6},  // S
  {28,  5},  // T
  {33,  9},  // M
  {42,  7},  // A
  {49,  6},  // S
  {55,  9},  // M
  {64,  6},  // E
  {70,  7},  // R
  {77,  7},  // R
  {84,  5},  // Y
};
static const uint8_t NUM_LETTERS = sizeof(LETTERS) / sizeof(LETTERS[0]);

// ── Christmas color palette — packed (w<<24)|(r<<16)|(g<<8)|b for GRBW ────────
static const uint32_t XMAS_COLORS[] = {
  0x00FF0000,  // Red
  0x0000B400,  // Green
  0xFF000000,  // Warm white (W channel)
  0x00FF8C00,  // Gold
  0x001E00FF,  // Ice blue
};
static const uint8_t  NUM_XMAS_COLORS = sizeof(XMAS_COLORS) / sizeof(XMAS_COLORS[0]);
static const uint32_t WARM_WHITE      = 0xFF281400;  // W + warm RGB tint

// ── Effects ────────────────────────────────────────────────────────────────────
static const char* EFFECT_NAMES[] = {"Warm White", "Twinkle", "Classic", "Sweep", "Chase"};
static const uint8_t NUM_EFFECTS   = sizeof(EFFECT_NAMES) / sizeof(EFFECT_NAMES[0]);
static const uint8_t FX_WARM_WHITE = 0;
static const uint8_t FX_TWINKLE   = 1;
static const uint8_t FX_CLASSIC   = 2;
static const uint8_t FX_SWEEP     = 3;
static const uint8_t FX_CHASE     = 4;

// ── Device state ───────────────────────────────────────────────────────────────
static bool    signOn         = false;
static uint8_t signBrightness = 200;
static uint8_t currentEffect  = FX_TWINKLE;

// ── Per-pixel twinkle state ────────────────────────────────────────────────────
static uint8_t  pixBri  [NUM_LEDS];
static int8_t   pixDelta[NUM_LEDS];
static uint32_t pixColor[NUM_LEDS];

// ── Per-letter classic state ───────────────────────────────────────────────────
static uint8_t letBri  [NUM_LETTERS];
static int8_t  letDelta[NUM_LETTERS];

// ── Sweep state ────────────────────────────────────────────────────────────────
static uint32_t sweepColors[NUM_LETTERS];
static uint8_t  sweepActive = 0;
static uint8_t  sweepTick   = 0;

// ── Chase state ────────────────────────────────────────────────────────────────
static const uint8_t CHASE_TAIL = 15;
static uint16_t chaseHead      = 0;
static uint8_t  chaseColorIdx  = 0;
static uint8_t  chaseTick      = 0;

static unsigned long          lastAnimMs = 0;
static const unsigned long    ANIM_MS    = 20;  // 50 fps

// ── MQTT / WiFi ────────────────────────────────────────────────────────────────
static String       deviceId;
static String       cmdTopic, stateTopic, discovTopic;
static WiFiClient   wifiClient;
static PubSubClient mqtt(wifiClient);

// ── NVS ───────────────────────────────────────────────────────────────────────
static Preferences prefs;

static void saveState() {
  prefs.begin("xmas", false);
  prefs.putBool("on",   signOn);
  prefs.putUChar("bri", signBrightness);
  prefs.putUChar("fx",  currentEffect);
  prefs.end();
}

static void loadState() {
  prefs.begin("xmas", true);
  signOn         = prefs.getBool("on",   false);
  signBrightness = prefs.getUChar("bri", 200);
  currentEffect  = prefs.getUChar("fx",  FX_TWINKLE);
  prefs.end();
  if (currentEffect >= NUM_EFFECTS) currentEffect = FX_TWINKLE;
}

// ── Color helpers ──────────────────────────────────────────────────────────────
static uint32_t scaleColor(uint32_t c, uint8_t s) {
  if (!s) return 0;
  return strip.Color(
    ((c >> 16) & 0xFF) * s / 255,
    ((c >>  8) & 0xFF) * s / 255,
    ( c        & 0xFF) * s / 255,
    ((c >> 24) & 0xFF) * s / 255
  );
}

static uint32_t randXmas() { return XMAS_COLORS[random(NUM_XMAS_COLORS)]; }

// ── Effect: Warm White — static, re-rendered on state change only ──────────────
static void renderWarmWhite() {
  for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, WARM_WHITE);
  strip.show();
}

// ── Effect: Twinkle ───────────────────────────────────────────────────────────
static void initTwinkle() {
  for (int i = 0; i < NUM_LEDS; i++) {
    pixColor[i] = randXmas();
    pixBri[i]   = random(256);
    pixDelta[i] = random(2) ? random(2, 7) : -random(2, 7);
  }
}

static void tickTwinkle() {
  for (int i = 0; i < NUM_LEDS; i++) {
    int16_t b = (int16_t)pixBri[i] + pixDelta[i];
    if (b <= 0) {
      pixBri[i] = 0; pixDelta[i] = random(2, 7); pixColor[i] = randXmas();
    } else if (b >= 255) {
      pixBri[i] = 255; pixDelta[i] = -random(1, 5);
    } else {
      pixBri[i] = (uint8_t)b;
    }
    strip.setPixelColor(i, scaleColor(pixColor[i], pixBri[i]));
  }
  strip.show();
}

// ── Effect: Classic — alternating R/G letters with independent flicker ─────────
static void initClassic() {
  for (int li = 0; li < NUM_LETTERS; li++) {
    letBri[li]   = random(160, 255);
    letDelta[li] = random(2) ? random(1, 4) : -random(1, 4);
  }
}

static void tickClassic() {
  static const uint32_t RED   = 0x00FF0000;
  static const uint32_t GREEN = 0x0000B400;
  for (int li = 0; li < NUM_LETTERS; li++) {
    int16_t b = (int16_t)letBri[li] + letDelta[li];
    if      (b < 100) { letBri[li] = 100; letDelta[li] =  random(1, 4); }
    else if (b > 255) { letBri[li] = 255; letDelta[li] = -random(1, 4); }
    else              { letBri[li] = (uint8_t)b; }
    uint32_t c = scaleColor((li & 1) ? GREEN : RED, letBri[li]);
    for (int pi = LETTERS[li].start; pi < LETTERS[li].start + LETTERS[li].count; pi++)
      strip.setPixelColor(pi, c);
  }
  strip.show();
}

// ── Effect: Sweep — colors change letter-by-letter in a rolling wave ──────────
static void initSweep() {
  for (int li = 0; li < NUM_LETTERS; li++) sweepColors[li] = randXmas();
  sweepActive = sweepTick = 0;
}

static void tickSweep() {
  if (++sweepTick >= 15) {  // one letter changes every 300 ms
    sweepTick = 0;
    sweepColors[sweepActive] = randXmas();
    sweepActive = (sweepActive + 1) % NUM_LETTERS;
  }
  for (int li = 0; li < NUM_LETTERS; li++)
    for (int pi = LETTERS[li].start; pi < LETTERS[li].start + LETTERS[li].count; pi++)
      strip.setPixelColor(pi, sweepColors[li]);
  strip.show();
}

// ── Effect: Chase — comet moves C→Y, cycling colors on each pass ──────────────
static void initChase() {
  chaseHead = chaseColorIdx = chaseTick = 0;
  strip.clear();
  strip.show();
}

static void tickChase() {
  if (++chaseTick < 2) return;  // advance every other tick (~40 ms/step)
  chaseTick = 0;
  strip.clear();
  uint32_t c = XMAS_COLORS[chaseColorIdx];
  for (int t = 0; t < CHASE_TAIL; t++) {
    int pos = (int)chaseHead - t;
    if (pos < 0) break;
    strip.setPixelColor(pos, scaleColor(c, 255 - t * 255 / CHASE_TAIL));
  }
  strip.show();
  if (++chaseHead >= (uint16_t)(NUM_LEDS + CHASE_TAIL)) {
    chaseHead = 0;
    chaseColorIdx = (chaseColorIdx + 1) % NUM_XMAS_COLORS;
  }
}

// ── Effect dispatch ────────────────────────────────────────────────────────────
static void initEffect() {
  switch (currentEffect) {
    case FX_TWINKLE: initTwinkle(); break;
    case FX_CLASSIC: initClassic(); break;
    case FX_SWEEP:   initSweep();   break;
    case FX_CHASE:   initChase();   break;
    default: break;
  }
}

static void tickEffect() {
  switch (currentEffect) {
    case FX_TWINKLE: tickTwinkle(); break;
    case FX_CLASSIC: tickClassic(); break;
    case FX_SWEEP:   tickSweep();   break;
    case FX_CHASE:   tickChase();   break;
    default: break;
  }
}

// ── Apply state (called on any state change, including boot) ───────────────────
static void applyStrip(bool effectSwitched) {
  strip.setBrightness(signBrightness);
  if (!signOn) {
    strip.clear();
    strip.show();
    return;
  }
  if (currentEffect == FX_WARM_WHITE) {
    renderWarmWhite();
  } else if (effectSwitched) {
    initEffect();
    // First animated frame on next loop() tick
  }
  // Brightness-only change: animation continues without reset
}

// ── MQTT state ─────────────────────────────────────────────────────────────────
static void publishState() {
  JsonDocument doc;
  doc["state"]      = signOn ? "ON" : "OFF";
  doc["brightness"] = signBrightness;
  doc["effect"]     = EFFECT_NAMES[currentEffect];
  char buf[192];
  serializeJson(doc, buf);
  mqtt.publish(stateTopic.c_str(), buf, /*retain=*/true);
}

// ── MQTT discovery ─────────────────────────────────────────────────────────────
static void publishDiscovery() {
  String fxList = "[";
  for (uint8_t i = 0; i < NUM_EFFECTS; i++) {
    if (i) fxList += ",";
    fxList += "\""; fxList += EFFECT_NAMES[i]; fxList += "\"";
  }
  fxList += "]";

  String dev = "{\"identifiers\":[\"" + deviceId + "\"],"
               "\"name\":\"Xmas Sign\","
               "\"model\":\"Seeed XIAO ESP32C6\","
               "\"manufacturer\":\"Adafruit\"}";

  String cfg =
    "{\"name\":\"Xmas Sign\","
    "\"unique_id\":\"" + deviceId + "_xmas\","
    "\"schema\":\"json\","
    "\"command_topic\":\"" + cmdTopic + "\","
    "\"state_topic\":\"" + stateTopic + "\","
    "\"supported_color_modes\":[\"brightness\"],"
    "\"effect\":true,"
    "\"effect_list\":" + fxList + ","
    "\"device\":" + dev + "}";

  mqtt.publish(discovTopic.c_str(), cfg.c_str(), /*retain=*/true);
}

// ── MQTT command handler ───────────────────────────────────────────────────────
static void onMessage(char* topic, byte* payload, unsigned int len) {
  if (cmdTopic != topic) return;

  JsonDocument doc;
  if (deserializeJson(doc, payload, len)) return;

  bool changed        = false;
  bool effectSwitched = false;

  if (!doc["state"].isNull()) {
    bool on = doc["state"] == "ON";
    if (on != signOn) { signOn = on; changed = true; effectSwitched = true; }
  }
  if (!doc["brightness"].isNull()) {
    uint8_t b = doc["brightness"].as<uint8_t>();
    if (b != signBrightness) { signBrightness = b; changed = true; }
  }
  if (!doc["effect"].isNull()) {
    const char* fx = doc["effect"];
    for (uint8_t i = 0; i < NUM_EFFECTS; i++) {
      if (strcmp(fx, EFFECT_NAMES[i]) == 0 && i != currentEffect) {
        currentEffect = i; changed = true; effectSwitched = true; break;
      }
    }
  }

  if (changed) { applyStrip(effectSwitched); saveState(); publishState(); }
}

// ── WiFi ───────────────────────────────────────────────────────────────────────
static void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.print("WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED) {
    esp_task_wdt_reset();
    delay(500); Serial.print(".");
    if (millis() - t > 30000) { Serial.println(" timeout — restarting"); ESP.restart(); }
  }
  WiFi.setSleep(false);
  Serial.println(" " + WiFi.localIP().toString());
}

// ── MQTT connect ───────────────────────────────────────────────────────────────
static void connectMQTT() {
  int fails = 0;
  while (!mqtt.connected()) {
    esp_task_wdt_reset();
    Serial.print("MQTT...");
    if (mqtt.connect(deviceId.c_str(), MQTT_USER, MQTT_PASS)) {
      Serial.println(" ok");
      mqtt.subscribe(cmdTopic.c_str());
      publishDiscovery();
      publishState();
    } else {
      Serial.println(" fail rc=" + String(mqtt.state()));
      if (++fails >= 5) { Serial.println("max retries — restarting"); ESP.restart(); }
      delay(5000);
    }
  }
}

// ── Setup ──────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  deviceId    = "xmas_" + String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFF), HEX);
  cmdTopic    = "home/" + deviceId + "/xmas/set";
  stateTopic  = "home/" + deviceId + "/xmas/state";
  discovTopic = "homeassistant/light/" + deviceId + "_xmas/config";

  loadState();

  strip.begin();
  applyStrip(/*effectSwitched=*/true);

  const esp_task_wdt_config_t wdtCfg = { WDT_TIMEOUT_S * 1000u, 0, true };
  esp_task_wdt_init(&wdtCfg);
  esp_task_wdt_add(NULL);

  connectWiFi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(15);
  mqtt.setSocketTimeout(10);
  mqtt.setCallback(onMessage);
  connectMQTT();
}

// ── Loop ───────────────────────────────────────────────────────────────────────
void loop() {
  esp_task_wdt_reset();
  connectWiFi();
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  if (signOn && currentEffect != FX_WARM_WHITE) {
    unsigned long now = millis();
    if (now - lastAnimMs >= ANIM_MS) {
      lastAnimMs = now;
      tickEffect();
    }
  }
}
