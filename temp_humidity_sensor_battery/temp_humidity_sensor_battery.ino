#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_SHTC3.h>
#include "esp_sleep.h"
#include "secrets.h"

// ── Configuration ──────────────────────────────────────────────────────────────
const uint32_t SLEEP_DURATION_SEC = 600;   // 10 minutes between readings
const uint32_t WIFI_TIMEOUT_MS    = 20000;
const uint32_t MQTT_TIMEOUT_MS    = 10000;

// ── Globals ────────────────────────────────────────────────────────────────────
String deviceId;
String stateTopic;
String discoveryTopicTemp;
String discoveryTopicHumid;

Adafruit_SHTC3 sensor;
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

// ── Sleep ──────────────────────────────────────────────────────────────────────
void goToSleep() {
  mqtt.disconnect();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_DURATION_SEC * 1000000ULL);
  esp_deep_sleep_start();
}

// ── Sensor read ────────────────────────────────────────────────────────────────
bool readSensor(float &temp, float &humid) {
  sensors_event_t humidEvent, tempEvent;
  sensor.getEvent(&humidEvent, &tempEvent);
  temp  = tempEvent.temperature;
  humid = humidEvent.relative_humidity;
  return !isnan(temp) && !isnan(humid);
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
void publishDiscovery() {
  String dev = "{\"identifiers\":[\"" + deviceId + "\"],"
               "\"name\":\"XIAO Sensor\","
               "\"model\":\"Seeed XIAO ESP32C6\","
               "\"manufacturer\":\"Seeed\"}";

  String tempCfg =
    "{\"name\":\"Temperature\","
    "\"device_class\":\"temperature\","
    "\"state_topic\":\"" + stateTopic + "\","
    "\"unit_of_measurement\":\"°C\","
    "\"value_template\":\"{{ value_json.temperature }}\","
    "\"unique_id\":\"" + deviceId + "_temp\","
    "\"device\":" + dev + "}";

  String humidCfg =
    "{\"name\":\"Humidity\","
    "\"device_class\":\"humidity\","
    "\"state_topic\":\"" + stateTopic + "\","
    "\"unit_of_measurement\":\"%\","
    "\"value_template\":\"{{ value_json.humidity }}\","
    "\"unique_id\":\"" + deviceId + "_humid\","
    "\"device\":" + dev + "}";

  mqtt.publish(discoveryTopicTemp.c_str(),  tempCfg.c_str(),  /*retain=*/true);
  mqtt.publish(discoveryTopicHumid.c_str(), humidCfg.c_str(), /*retain=*/true);
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

// ── Setup (runs once per wake cycle) ──────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Wire.begin();  // XIAO ESP32C6: SDA=D4, SCL=D5 (board variant defaults)

  // Read sensor before bringing up WiFi to avoid radio self-heating skew
  if (!sensor.begin()) {
    Serial.println("SHTC3 not found — check wiring.");
    goToSleep();
  }

  float temp, humid;
  if (!readSensor(temp, humid)) {
    Serial.println("Sensor read failed — returned NaN.");
    goToSleep();
  }

  Serial.printf("Sensor: %.1f °C  %.1f %%RH\n", temp, humid);

  // Device ID from lower 3 bytes of eFuse MAC
  uint64_t mac = ESP.getEfuseMac();
  deviceId            = "xiao_" + String((uint32_t)(mac & 0xFFFFFF), HEX);
  stateTopic          = "home/" + deviceId + "/state";
  discoveryTopicTemp  = "homeassistant/sensor/" + deviceId + "/temperature/config";
  discoveryTopicHumid = "homeassistant/sensor/" + deviceId + "/humidity/config";

  if (!connectWiFi()) {
    Serial.println("WiFi timed out — sleeping.");
    goToSleep();
  }
  Serial.println("WiFi connected: " + WiFi.localIP().toString());

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);  // discovery payloads exceed the 256-byte default

  if (!connectMQTT()) {
    Serial.println("MQTT timed out — sleeping.");
    goToSleep();
  }

  String payload = "{\"temperature\":" + String(temp, 1) +
                   ",\"humidity\":"    + String(humid, 1) + "}";
  Serial.println("Publishing: " + payload);
  mqtt.publish(stateTopic.c_str(), payload.c_str());

  goToSleep();
}

void loop() {}
