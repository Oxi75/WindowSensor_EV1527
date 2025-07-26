#include "ConfigManager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <base64.h>

bool ConfigManager::load() {
  if (!LittleFS.exists(CONFIG_FILE)) {
    Serial.println("[CONFIG] config.json fehlt – nutze Defaultwerte");
    return false;
  }

  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f) return false;

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.println("[CONFIG] Fehler beim Parsen – nutze Defaults");
    return false;
  }

  cfgInSTA = doc["system"]["cfgInSTA"] | false;
  ssid     = doc["wifi"]["ssid"] | "";
  password = decrypt(doc["wifi"]["pwEnc"] | "");
  vhih_ip  = doc["wifi"]["ip"] | "192.168.1.100";

  // Sensoren – optional
  JsonArray arr = doc["sensors"].as<JsonArray>();
  int i = 0;
  for (JsonObject s : arr) {
    if (i >= MAX_SENSORS) break;
    sensors[i].active = s["active"] | false;
    sensors[i].name = s["name"] | "";
    sensors[i].homeeID = s["homeeID"] | 0;
    sensors[i].type = s["type"] | "Window-Sensor";
    sensors[i].address = s["address"] | 0;
    sensors[i].signalOn = s["signalOn"] | 0;
    sensors[i].signalOff = s["signalOff"] | 0;
    sensors[i].signalAlarm = s["signalAlarm"] | 0;
    sensors[i].signalBattery = s["signalBattery"] | 0;
    i++;
  }

  Serial.println("[CONFIG] Konfiguration geladen.");
  return true;
}

bool ConfigManager::save() {
  DynamicJsonDocument doc(4096);

  doc["system"]["cfgInSTA"] = cfgInSTA;
  doc["wifi"]["ssid"] = ssid;
  doc["wifi"]["pwEnc"] = encrypt(password);
  doc["wifi"]["ip"] = vhih_ip;

  JsonArray arr = doc.createNestedArray("sensors");
  for (int i = 0; i < MAX_SENSORS; i++) {
    if (sensors[i].name == "") continue;
    JsonObject s = arr.createNestedObject();
    s["active"] = sensors[i].active;
    s["name"] = sensors[i].name;
    s["homeeID"] = sensors[i].homeeID;
    s["type"] = sensors[i].type;
    s["address"] = sensors[i].address;
    s["signalOn"] = sensors[i].signalOn;
    s["signalOff"] = sensors[i].signalOff;
    s["signalAlarm"] = sensors[i].signalAlarm;
    s["signalBattery"] = sensors[i].signalBattery;
  }

  File f = LittleFS.open(CONFIG_FILE, "w");
  if (!f) {
    Serial.println("[CONFIG] Fehler beim Öffnen zum Speichern.");
    return false;
  }
  serializeJsonPretty(doc, f);
  f.close();
  Serial.println("[CONFIG] Konfiguration gespeichert.");
  return true;
}

// XOR + base64
String ConfigManager::encrypt(String raw) {
  for (size_t i = 0; i < raw.length(); i++)
    raw[i] ^= XOR_KEY;
  return base64::encode(raw);
}

String ConfigManager::decrypt(String enc) {
  String decoded = base64::decode(enc);
  for (size_t i = 0; i < decoded.length(); i++)
    decoded[i] ^= XOR_KEY;
  return decoded;
}
