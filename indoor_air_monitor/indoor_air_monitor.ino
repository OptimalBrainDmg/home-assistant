#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <Adafruit_STCC4.h>
#include <DFRobot_ENS160.h>
#include "secrets.h"

// ── Configuration ──────────────────────────────────────────────────────────────
const uint32_t SLEEP_DURATION_SEC = 300;  // 5 minutes between readings
const uint32_t WIFI_TIMEOUT_MS    = 20000;
const uint32_t MQTT_TIMEOUT_MS    = 10000;

// ── Globals ────────────────────────────────────────────────────────────────────
String deviceId;
String stateTopic;

Adafruit_STCC4      stcc4;
DFRobot_ENS160_I2C  ens160(&Wire, 0x53);  // ADDR pin high on this breakout

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

// ── Sensor reads ───────────────────────────────────────────────────────────────
// SHT41 is wired only to the STCC4's internal I2C controller (not the main bus),
// so temperature and humidity come from readMeasurement() alongside CO2.
bool readSTCC4(uint16_t &co2, float &temp, float &humid) {
  uint16_t status;
  if (!stcc4.readMeasurement(&co2, &temp, &humid, &status)) return false;
  Serial.printf("STCC4 status: 0x%04X\n", status);
  temp  = round(temp  * 10.0f) / 10.0f;
  humid = round(humid * 10.0f) / 10.0f;
  return true;
}

bool readENS160(float temp, float humid, uint16_t &tvoc, uint16_t &eco2) {
  ens160.setTempAndHum(temp, humid);
  tvoc = ens160.getTVOC();
  eco2 = ens160.getECO2();
  return true;
}

// ── WiFi ───────────────────────────────────────────────────────────────────────
bool connectWiFi() {
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
  doc["device_class"]        = device_class;
  doc["unit_of_measurement"] = unit;
  doc["state_class"]         = "measurement";
  doc["unique_id"]           = deviceId + "_" + entity;
  doc["expire_after"]        = SLEEP_DURATION_SEC * 3;
  JsonObject dev         = doc["device"].to<JsonObject>();
  dev["identifiers"][0]  = deviceId;
  dev["name"]            = "Indoor Air Monitor";
  dev["model"]           = "XIAO ESP32C6";
  dev["manufacturer"]    = "Seeed";
  String payload;
  serializeJson(doc, payload);
  mqtt.publish(topic.c_str(), payload.c_str(), true);
}

void publishDiscovery() {
  publishSensorConfig("temperature", "Temperature", "temperature",                       "°C",  "temperature");
  publishSensorConfig("humidity",    "Humidity",    "humidity",                          "%",   "humidity");
  publishSensorConfig("co2",         "CO2",         "carbon_dioxide",                    "ppm", "co2");
  publishSensorConfig("tvoc",        "TVOC",        "volatile_organic_compounds_parts",  "ppb", "tvoc");
  publishSensorConfig("eco2",        "eCO2",        "carbon_dioxide",                    "ppm", "eco2");
}

// ── MQTT connection ────────────────────────────────────────────────────────────
bool connectMQTT() {
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
  delay(3000);  // time to attach serial monitor
  Wire.begin();  // XIAO ESP32C6: SDA=D4(GPIO22), SCL=D5(GPIO23)

  // I2C scan — helps diagnose wiring; remove once hardware is confirmed
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
  deviceId   = "air_" + String((uint32_t)(mac & 0xFFFFFF), HEX);
  stateTopic = "home/" + deviceId + "/state";
  Serial.print("Device ID: "); Serial.println(deviceId);

  if (!stcc4.begin()) {
    Serial.println("STCC4 init failed — halting");
    while (1) delay(1000);
  }
  if (!stcc4.enableContinuousMeasurement(true)) {
    Serial.println("STCC4 continuous mode failed — halting");
    while (1) delay(1000);
  }
  // ENS160 begin() sets STANDARD_MODE. Baseline accumulates while powered.
  if (ens160.begin() != NO_ERR) {
    Serial.println("ENS160 init failed — halting");
    while (1) delay(1000);
  }

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(1024);

  // Wait one full 1Hz cycle for both sensors to produce their first readings.
  ens160.setTempAndHum(25.0, 50.0);  // placeholder until STCC4 T/H available
  delay(1100);
}

// ── Loop — read, publish, wait, repeat ────────────────────────────────────────
void loop() {
  // Read sensors before WiFi to avoid radio self-heating skew on SHT41
  float temp, humid;
  uint16_t co2;
  if (!readSTCC4(co2, temp, humid)) {
    Serial.println("STCC4 read failed");
  } else {
    Serial.printf("T=%.1f C  RH=%.1f %%  CO2=%u ppm\n", temp, humid, co2);

    uint16_t tvoc, eco2;
    readENS160(temp, humid, tvoc, eco2);
    Serial.printf("TVOC=%u ppb  eCO2=%u ppm  (ENS160 status=%d)\n",
                  tvoc, eco2, ens160.getENS160Status());

    if (!connectWiFi()) {
      Serial.println("WiFi timed out");
    } else if (!connectMQTT()) {
      Serial.println("MQTT timed out");
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    } else {
      JsonDocument doc;
      doc["temperature"] = temp;
      doc["humidity"]    = humid;
      doc["co2"]         = co2;
      doc["tvoc"]        = tvoc;
      doc["eco2"]        = eco2;
      String payload;
      serializeJson(doc, payload);
      mqtt.publish(stateTopic.c_str(), payload.c_str(), true);
      Serial.println("Published: " + payload);
      for (int i = 0; i < 5; i++) { mqtt.loop(); delay(10); }
      mqtt.disconnect();
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    }
  }

  delay(SLEEP_DURATION_SEC * 1000);
}
