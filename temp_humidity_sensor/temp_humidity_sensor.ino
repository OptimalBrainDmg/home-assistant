#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include "secrets.h"

// ── Sensor selection (uncomment one) ──────────────────────────────────────────
#define SENSOR_AM2320
// #define SENSOR_SHTC3

#if defined(SENSOR_AM2320)
  #include <Adafruit_AM2320.h>
  Adafruit_AM2320 sensor;
#elif defined(SENSOR_SHTC3)
  #include <Adafruit_SHTC3.h>
  Adafruit_SHTC3 sensor;
#else
  #error "No sensor defined — uncomment SENSOR_AM2320 or SENSOR_SHTC3"
#endif

// ── Configuration ──────────────────────────────────────────────────────────────
const unsigned long READ_INTERVAL = 30000;  // ms between sensor readings

// ── Globals ────────────────────────────────────────────────────────────────────
String deviceId;
String stateTopic;
String discoveryTopicTemp;
String discoveryTopicHumid;

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

unsigned long lastReadMs = 0;

// ── Sensor read ────────────────────────────────────────────────────────────────
bool readSensor(float &temp, float &humid) {
#if defined(SENSOR_AM2320)
  temp  = sensor.readTemperature();
  humid = sensor.readHumidity();
#elif defined(SENSOR_SHTC3)
  sensors_event_t humidEvent, tempEvent;
  sensor.getEvent(&humidEvent, &tempEvent);
  temp  = tempEvent.temperature;
  humid = humidEvent.relative_humidity;
#endif
  return !isnan(temp) && !isnan(humid);
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

// ── MQTT discovery ─────────────────────────────────────────────────────────────
void publishDiscovery() {
  String dev = "{\"identifiers\":[\"" + deviceId + "\"],"
               "\"name\":\"D1 Mini Sensor\","
               "\"model\":\"LOLIN D1 Mini V4\","
               "\"manufacturer\":\"LOLIN\"}";

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
void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Connecting to MQTT...");
    if (mqtt.connect(deviceId.c_str(), MQTT_USER, MQTT_PASS)) {
      Serial.println(" connected.");
      publishDiscovery();
    } else {
      Serial.println(" failed (rc=" + String(mqtt.state()) + "), retry in 5s");
      delay(5000);
    }
  }
}

// ── Setup ──────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Wire.begin();  // D1 Mini V4: SDA=D2 (GPIO4), SCL=D1 (GPIO5)

  if (!sensor.begin()) {
#if defined(SENSOR_AM2320)
    Serial.println("AM2320 not found — check wiring and pull-up resistors.");
#elif defined(SENSOR_SHTC3)
    Serial.println("SHTC3 not found — check wiring.");
#endif
  }

  deviceId            = "d1mini_" + String(ESP.getChipId(), HEX);
  stateTopic          = "home/" + deviceId + "/state";
  discoveryTopicTemp  = "homeassistant/sensor/" + deviceId + "/temperature/config";
  discoveryTopicHumid = "homeassistant/sensor/" + deviceId + "/humidity/config";

  connectWiFi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);  // discovery payloads exceed the 256-byte default
  connectMQTT();
}

// ── Loop ───────────────────────────────────────────────────────────────────────
void loop() {
  connectWiFi();
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  unsigned long now = millis();
  if (now - lastReadMs >= READ_INTERVAL) {
    lastReadMs = now;

    float temp, humid;
    if (!readSensor(temp, humid)) {
      Serial.println("Sensor read failed — returned NaN");
      return;
    }

    String payload = "{\"temperature\":" + String(temp, 1) +
                     ",\"humidity\":"    + String(humid, 1) + "}";
    Serial.println("Publishing: " + payload);
    mqtt.publish(stateTopic.c_str(), payload.c_str());
  }
}
