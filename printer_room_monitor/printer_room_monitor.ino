#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <Adafruit_SHT4x.h>
#include "secrets.h"

// ── Configuration ──────────────────────────────────────────────────────────────
const uint32_t POLL_INTERVAL_MS = 10000;  // 10 s between readings
const uint32_t WIFI_TIMEOUT_MS  = 20000;
const uint32_t MQTT_TIMEOUT_MS  = 10000;
const uint32_t SENSOR_WARMUP_MS = 30000;  // MEMS smoke sensor needs ~30 s to stabilize

// Smoke sensor on A0 (ADC1, GPIO2).
// IMPORTANT: Wire sensor VCC to 3V3, not 5V. Output is ratiometric to VCC;
// at 5V supply it can swing above 3.3V and damage the ADC input.
const int SMOKE_PIN = A0;

// ── Globals ────────────────────────────────────────────────────────────────────
String deviceId;
String stateTopic;

Adafruit_SHT4x sht4;

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

// ── Sensor reads ───────────────────────────────────────────────────────────────
bool readSHT40(float &temp, float &humid) {
  sensors_event_t humEvent, tempEvent;
  if (!sht4.getEvent(&humEvent, &tempEvent)) return false;
  temp  = round(tempEvent.temperature       * 10.0f) / 10.0f;
  humid = round(humEvent.relative_humidity  * 10.0f) / 10.0f;
  return true;
}

uint16_t readSmokeMv() {
  return (uint16_t)analogReadMilliVolts(SMOKE_PIN);
}

// ── WiFi ───────────────────────────────────────────────────────────────────────
bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (millis() - start < WIFI_TIMEOUT_MS) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(200);
  }
  return false;
}

// ── MQTT discovery ─────────────────────────────────────────────────────────────
static void publishSensorConfig(const char* entity, const char* name,
                                const char* device_class, const char* unit,
                                const char* value_key) {
  String topic = "homeassistant/sensor/" + deviceId + "/" + entity + "/config";
  JsonDocument doc;
  doc["name"]                = name;
  doc["state_topic"]         = stateTopic;
  doc["value_template"]      = String("{{ value_json.") + value_key + " }}";
  if (device_class[0] != '\0') doc["device_class"] = device_class;
  doc["unit_of_measurement"] = unit;
  doc["state_class"]         = "measurement";
  doc["unique_id"]           = deviceId + "_" + entity;
  doc["expire_after"]        = 60;
  JsonObject dev        = doc["device"].to<JsonObject>();
  dev["identifiers"][0] = deviceId;
  dev["name"]           = "Printer Room Monitor";
  dev["model"]          = "XIAO ESP32C6";
  dev["manufacturer"]   = "Seeed";
  String payload;
  serializeJson(doc, payload);
  mqtt.publish(topic.c_str(), payload.c_str(), true);
}

void publishDiscovery() {
  publishSensorConfig("temperature", "Temperature", "temperature", "°C",  "temperature");
  publishSensorConfig("humidity",    "Humidity",    "humidity",    "%",   "humidity");
  publishSensorConfig("smoke",       "Smoke",       "",            "mV",  "smoke_mv");
}

// ── MQTT connection ────────────────────────────────────────────────────────────
bool connectMQTT() {
  if (mqtt.connected()) return true;
  uint32_t start = millis();
  while (millis() - start < MQTT_TIMEOUT_MS) {
    if (mqtt.connect(deviceId.c_str(), MQTT_USER, MQTT_PASS)) {
      publishDiscovery();
      return true;
    }
    delay(1000);
  }
  return false;
}

// ── Setup — runs once on power-on ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(3000);
  Wire.begin();  // XIAO ESP32C6: SDA=D4(GPIO22), SCL=D5(GPIO23)

  Serial.println("Scanning I2C bus...");
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  0x%02X\n", addr);
      found++;
    }
  }
  if (!found) Serial.println("  no devices found");
  Serial.println("I2C scan done.");

  uint64_t mac = ESP.getEfuseMac();
  deviceId   = "smoke_" + String((uint32_t)(mac & 0xFFFFFFFF), HEX);
  stateTopic = "home/" + deviceId + "/state";
  Serial.print("Device ID: "); Serial.println(deviceId);

  if (!sht4.begin()) {
    Serial.println("SHT40 init failed — halting");
    while (1) delay(1000);
  }
  sht4.setPrecision(SHT4X_HIGH_PRECISION);
  sht4.setHeater(SHT4X_NO_HEATER);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);

  // Overlap WiFi/MQTT connect with MEMS sensor warmup
  Serial.println("Connecting WiFi...");
  if (connectWiFi()) {
    connectMQTT();
  } else {
    Serial.println("WiFi timed out — will retry in loop");
  }

  uint32_t elapsed = millis();
  if (elapsed < SENSOR_WARMUP_MS) {
    uint32_t remaining = SENSOR_WARMUP_MS - elapsed;
    Serial.printf("Smoke sensor warmup: %u ms remaining\n", remaining);
    delay(remaining);
  }
}

// ── Loop — read, publish, wait, repeat ────────────────────────────────────────
void loop() {
  float temp, humid;
  bool sht40ok  = readSHT40(temp, humid);
  uint16_t smokeMv = readSmokeMv();

  if (sht40ok) {
    Serial.printf("T=%.1f C  RH=%.1f %%  Smoke=%u mV\n", temp, humid, smokeMv);
  } else {
    Serial.println("SHT40 read failed");
  }

  if (!connectWiFi()) {
    Serial.println("WiFi unavailable");
  } else if (!connectMQTT()) {
    Serial.println("MQTT unavailable");
  } else if (sht40ok) {
    JsonDocument doc;
    doc["temperature"] = temp;
    doc["humidity"]    = humid;
    doc["smoke_mv"]    = smokeMv;
    String payload;
    serializeJson(doc, payload);
    mqtt.publish(stateTopic.c_str(), payload.c_str(), true);
    Serial.println("Published: " + payload);
  }

  mqtt.loop();
  delay(POLL_INTERVAL_MS);
}
