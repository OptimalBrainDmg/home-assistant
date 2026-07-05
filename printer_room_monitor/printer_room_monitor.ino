#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <Adafruit_SHT4x.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "secrets.h"

// ── Configuration ──────────────────────────────────────────────────────────────
const uint32_t POLL_INTERVAL_MS = 10000;  // target cycle time
const uint32_t WIFI_TIMEOUT_MS  = 20000;
const uint32_t MQTT_TIMEOUT_MS  = 10000;
const uint32_t SENSOR_WARMUP_MS = 30000;  // MEMS smoke sensor needs ~30 s to stabilize
const int      BLE_SCAN_SECS    = 6;      // blocking scan duration per cycle; 2× pvvx adv interval to survive WiFi coexistence gaps

// Smoke sensor on A0 (ADC1, GPIO2).
// IMPORTANT: Wire sensor VCC to 3V3, not 5V. Output is ratiometric to VCC;
// at 5V supply it can swing above 3.3V and damage the ADC input.
const int SMOKE_PIN = A0;

// ── BLE sensor state ───────────────────────────────────────────────────────────
struct BleSensorData {
  bool     valid;
  uint32_t lastSeen;  // millis() when last temp+humidity was parsed
  float    temperature;
  float    humidity;
  uint8_t  battery;
};

BleSensorData bleSensorData[BLE_SENSOR_COUNT] = {};

// ── Globals ────────────────────────────────────────────────────────────────────
String deviceId;
String stateTopic;

Adafruit_SHT4x sht4;
BLEScan*        bleScan;

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

// ── BTHome parser ──────────────────────────────────────────────────────────────
// Returns data byte length for known BTHome v2 object IDs, -1 if unknown.
static int bthomeObjLen(uint8_t obj) {
  if (obj == 0x00) return 1;               // packet id
  if (obj == 0x01) return 1;               // battery %
  if (obj == 0x02) return 2;               // temperature
  if (obj == 0x03) return 2;               // humidity
  if (obj == 0x04) return 3;               // pressure
  if (obj == 0x05) return 3;               // illuminance
  if (obj == 0x06 || obj == 0x07) return 2; // mass kg/lb
  if (obj == 0x08) return 2;               // dewpoint
  if (obj == 0x09) return 1;               // count uint8
  if (obj == 0x0A) return 3;               // energy
  if (obj == 0x0B) return 3;               // power W
  if (obj == 0x0C) return 2;               // voltage
  if (obj == 0x0D || obj == 0x0E) return 2; // PM2.5 / PM10
  if (obj >= 0x0F && obj <= 0x2E) return 1; // booleans + 8-bit humidity/moisture
  if (obj == 0x3C) return 2;               // dimmer event
  if (obj == 0x3D) return 2;               // count uint16
  if (obj == 0x3E) return 4;               // count uint32
  return -1;
}

// BTHome v2: first byte = device info/flags (bit 0 = encrypted).
// Remaining bytes are objects: [obj_id] [data...], length implied by obj_id.
// No per-object length byte.
static bool parseBTHome(const uint8_t* data, size_t len, BleSensorData& out) {
  if (len < 1) return false;
  if (data[0] & 0x01) {
    Serial.println("[BLE] BTHome payload is encrypted — set pvvx to unencrypted");
    return false;
  }
  bool gotTemp = false, gotHumid = false;
  size_t i = 1;
  while (i < len) {
    uint8_t obj = data[i++];
    switch (obj) {
      case 0x01:  // battery %, uint8
        if (i + 1 > len) return false;
        out.battery = data[i++];
        break;
      case 0x02:  // temperature, signed int16 little-endian, ×0.01 = °C
        if (i + 2 > len) return false;
        out.temperature = (int16_t)(data[i] | ((uint16_t)data[i + 1] << 8)) * 0.01f;
        i += 2;
        gotTemp = true;
        break;
      case 0x03:  // humidity, uint16 little-endian, ×0.01 = %RH
        if (i + 2 > len) return false;
        out.humidity = (uint16_t)(data[i] | ((uint16_t)data[i + 1] << 8)) * 0.01f;
        i += 2;
        gotHumid = true;
        break;
      default: {
        int dlen = bthomeObjLen(obj);
        if (dlen < 0 || i + (size_t)dlen > len) {
          Serial.printf("[BLE] unknown BTHome obj 0x%02X at offset %u — truncating\n", obj, (unsigned)(i - 1));
          return false;
        }
        i += dlen;  // skip known but unneeded object
        break;
      }
    }
  }
  if (!gotTemp || !gotHumid) return false;
  out.valid    = true;
  out.lastSeen = millis();
  return true;
}

// ── BLE scanning ──────────────────────────────────────────────────────────────
static String bleSensorId(int i) {
  // "A4:C1:38:44:32:E7" -> "4432e7" (last 3 bytes, lower case, no colons)
  String mac = String(BLE_SENSOR_MACS[i]);
  mac.replace(":", "");
  mac.toLowerCase();
  return "filament_" + mac.substring(mac.length() - 6);
}

static void scanBle() {
  // Expire stale readings (5 s before HA's expire_after so we stop publishing before HA marks unavailable)
  uint32_t now = millis();
  for (int i = 0; i < BLE_SENSOR_COUNT; i++) {
    if (bleSensorData[i].valid && now - bleSensorData[i].lastSeen > 55000) {
      bleSensorData[i].valid = false;
    }
  }

  BLEScanResults* results = bleScan->start(BLE_SCAN_SECS, false);
  int count = results->getCount();

  Serial.printf("[BLE] scan found %d device(s)\n", count);
  for (int r = 0; r < count; r++) {
    BLEAdvertisedDevice dev = results->getDevice(r);
    String addr = String(dev.getAddress().toString().c_str());
    addr.toUpperCase();
    int sdCount = dev.getServiceDataCount();
    Serial.printf("[BLE] %s  svcData=%d", addr.c_str(), sdCount);
    for (int d = 0; d < sdCount; d++)
      Serial.printf("  uuid[%d]=%s", d, dev.getServiceDataUUID(d).toString().c_str());
    Serial.println();

    for (int s = 0; s < BLE_SENSOR_COUNT; s++) {
      if (addr != String(BLE_SENSOR_MACS[s])) continue;

      int dataCount = dev.getServiceDataCount();
      for (int d = 0; d < dataCount; d++) {
        if (!dev.getServiceDataUUID(d).equals(BLEUUID((uint16_t)0xFCD2))) continue;

        String raw = dev.getServiceData(d);
        // Raw hex dump — keep until BTHome parsing is verified against real device
        Serial.printf("[BLE] %s raw (%u bytes):", BLE_SENSOR_MACS[s], (unsigned)raw.length());
        for (size_t b = 0; b < (size_t)raw.length(); b++) Serial.printf(" %02X", (uint8_t)raw[b]);
        Serial.println();

        bool parsed = parseBTHome((const uint8_t*)raw.c_str(), raw.length(), bleSensorData[s]);
        if (!parsed && bleSensorData[s].valid) {
          bleSensorData[s].lastSeen = millis();  // voltage-only: sensor alive, keep existing T/H
        }
      }
    }
  }
  bleScan->clearResults();
}

// ── Sensor reads ───────────────────────────────────────────────────────────────
static bool readSHT40(float& temp, float& humid) {
  sensors_event_t humEvent, tempEvent;
  if (!sht4.getEvent(&humEvent, &tempEvent)) return false;
  temp  = round(tempEvent.temperature      * 10.0f) / 10.0f;
  humid = round(humEvent.relative_humidity * 10.0f) / 10.0f;
  return true;
}

static uint16_t readSmokeMv() {
  return (uint16_t)analogReadMilliVolts(SMOKE_PIN);
}

// ── WiFi ───────────────────────────────────────────────────────────────────────
static bool connectWiFi() {
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
static void publishSensorConfig(const String& sensorId, const String& stateTp,
                                const String& deviceName, const char* deviceModel,
                                const char* deviceMfr,
                                const char* entity, const char* name,
                                const char* device_class, const char* unit,
                                const char* value_key) {
  String topic = "homeassistant/sensor/" + sensorId + "/" + entity + "/config";
  JsonDocument doc;
  doc["name"]                = name;
  doc["state_topic"]         = stateTp;
  doc["value_template"]      = String("{{ value_json.") + value_key + " }}";
  if (device_class[0] != '\0') doc["device_class"] = device_class;
  doc["unit_of_measurement"] = unit;
  doc["state_class"]         = "measurement";
  doc["unique_id"]           = sensorId + "_" + entity;
  doc["expire_after"]        = 120;
  JsonObject dev        = doc["device"].to<JsonObject>();
  dev["identifiers"][0] = sensorId;
  dev["name"]           = deviceName;
  dev["model"]          = deviceModel;
  dev["manufacturer"]   = deviceMfr;
  String payload;
  serializeJson(doc, payload);
  mqtt.publish(topic.c_str(), payload.c_str(), true);
}

static void publishDiscovery() {
  // Printer room monitor sensors
  publishSensorConfig(deviceId, stateTopic, "Printer Room Monitor",
                      "XIAO ESP32C6", "Seeed",
                      "temperature", "Temperature", "temperature", "°C",  "temperature");
  publishSensorConfig(deviceId, stateTopic, "Printer Room Monitor",
                      "XIAO ESP32C6", "Seeed",
                      "humidity",    "Humidity",    "humidity",    "%",   "humidity");
  publishSensorConfig(deviceId, stateTopic, "Printer Room Monitor",
                      "XIAO ESP32C6", "Seeed",
                      "smoke",       "Smoke",       "",            "mV",  "smoke_mv");

  // BLE filament sensor discovery
  for (int i = 0; i < BLE_SENSOR_COUNT; i++) {
    String sId    = bleSensorId(i);
    String sState = "home/" + sId + "/state";
    String label  = String(BLE_SENSOR_LABELS[i]);
    publishSensorConfig(sId, sState, label, "LYWSD03MMC", "Xiaomi",
                        "temperature", "Temperature",
                        "temperature", "°C", "temperature");
    publishSensorConfig(sId, sState, label, "LYWSD03MMC", "Xiaomi",
                        "humidity",    "Humidity",
                        "humidity",    "%",  "humidity");
    publishSensorConfig(sId, sState, label, "LYWSD03MMC", "Xiaomi",
                        "battery",     "Battery",
                        "battery",     "%",  "battery");
  }
}

// ── MQTT connection ────────────────────────────────────────────────────────────
static bool connectMQTT() {
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

// ── Setup ─────────────────────────────────────────────────────────────────────
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

  BLEDevice::init("");
  bleScan = BLEDevice::getScan();
  bleScan->setActiveScan(false);
  Serial.println("BLE init OK");

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

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  uint32_t cycleStart = millis();

  float temp, humid;
  bool  sht40ok = readSHT40(temp, humid);
  uint16_t smokeMv = readSmokeMv();

  if (sht40ok) {
    Serial.printf("T=%.1f C  RH=%.1f %%  Smoke=%u mV\n", temp, humid, smokeMv);
  } else {
    Serial.println("SHT40 read failed");
  }

  // Service MQTT keep-alive before blocking BLE scan
  mqtt.loop();
  scanBle();
  mqtt.loop();

  if (!connectWiFi()) {
    Serial.println("WiFi unavailable");
  } else if (!connectMQTT()) {
    Serial.println("MQTT unavailable");
  } else {
    if (sht40ok) {
      JsonDocument doc;
      doc["temperature"] = temp;
      doc["humidity"]    = humid;
      doc["smoke_mv"]    = smokeMv;
      String payload;
      serializeJson(doc, payload);
      mqtt.publish(stateTopic.c_str(), payload.c_str(), true);
      Serial.println("Published: " + payload);
    }

    for (int i = 0; i < BLE_SENSOR_COUNT; i++) {
      if (!bleSensorData[i].valid) continue;
      String sId    = bleSensorId(i);
      String sState = "home/" + sId + "/state";
      JsonDocument doc;
      doc["temperature"] = roundf(bleSensorData[i].temperature * 100.0f) / 100.0f;
      doc["humidity"]    = roundf(bleSensorData[i].humidity    * 100.0f) / 100.0f;
      doc["battery"]     = bleSensorData[i].battery;
      String payload;
      serializeJson(doc, payload);
      mqtt.publish(sState.c_str(), payload.c_str(), true);
      Serial.printf("[BLE] %s published: %s\n", BLE_SENSOR_LABELS[i], payload.c_str());
    }
  }

  mqtt.loop();

  uint32_t elapsed = millis() - cycleStart;
  if (elapsed < POLL_INTERVAL_MS) delay(POLL_INTERVAL_MS - elapsed);
}
