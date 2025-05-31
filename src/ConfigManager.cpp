#include "ConfigManager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

bool ConfigManager::load()
{
  if (!LittleFS.exists(CONFIG_FILE)) {
    Serial.println("[CONFIG] config.json missing – using defaults");
    return false;
  }

  File f = LittleFS.open(CONFIG_FILE, "r");
  if (!f) return false;

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.println("[CONFIG] Parse error – using defaults");
    return false;
  }

  cfgInSTA = doc["system"]["cfgInSTA"] | false;
  ssid     = doc["wifi"]["ssid"] | "";
  password = decrypt(doc["wifi"]["pwX"] | "");
  clientIP  = doc["wifi"]["ip"] | "192.168.1.100";
  gatewayIP  = doc["wifi"]["gw"] | "192.168.1.1";
  subnet   = doc["wifi"]["mask"] | "255.255.255.0";

  JsonArray arr = doc["sensors"].as<JsonArray>();
  int i = 0;
  for (JsonObject s : arr)
  {
    if (i >= MAX_SENSORS) break;
    sensors[i].active        = s["active"] | false;
    sensors[i].name          = s["name"] | "";
    sensors[i].homeeID       = s["homeeID"] | 0;
    sensors[i].type          = s["type"] | "Window-Sensor";
    sensors[i].address       = s["address"] | 0;
    sensors[i].signalOn      = s["signalOn"] | 0;
    sensors[i].signalOff     = s["signalOff"] | 0;
    sensors[i].signalAlarm   = s["signalAlarm"] | 0;
    sensors[i].signalBattery = s["signalBattery"] | 0;
    i++;
  }

  if (i == 0)
  {
    Serial.println("[CONFIG] No sensors in config. Setting defaults.");
    sensors[0].active = true;
    sensors[0].name = "DefaultSensor";
    sensors[0].homeeID = 1;
    sensors[0].type = "Window-Sensor";
    sensors[0].address = 1234;
    sensors[0].signalOn = 1001;
    sensors[0].signalOff = 1002;
    sensors[0].signalAlarm = 1003;
    sensors[0].signalBattery = 1004;
  }
  else {
    Serial.printf("[CONFIG] %d sensor(s) loaded.\n", i);
  }

  Serial.println("[CONFIG] Configuration loaded.");
  return true;
}

bool ConfigManager::save() {
  DynamicJsonDocument doc(4096);

  doc["system"]["cfgInSTA"] = cfgInSTA;
  doc["wifi"]["ssid"] = ssid;
  doc["wifi"]["pwX"]  = encrypt(password);
  doc["wifi"]["ip"]   = clientIP;
  doc["wifi"]["gw"]   = gatewayIP;
  doc["wifi"]["mask"] = subnet;

  JsonArray arr = doc.createNestedArray("sensors");
  for (int i = 0; i < MAX_SENSORS; i++) {
    if (sensors[i].name == "") continue;
    JsonObject s = arr.createNestedObject();
    s["active"]        = sensors[i].active;
    s["name"]          = sensors[i].name;
    s["homeeID"]       = sensors[i].homeeID;
    s["type"]          = sensors[i].type;
    s["address"]       = sensors[i].address;
    s["signalOn"]      = sensors[i].signalOn;
    s["signalOff"]     = sensors[i].signalOff;
    s["signalAlarm"]   = sensors[i].signalAlarm;
    s["signalBattery"] = sensors[i].signalBattery;
  }

  File f = LittleFS.open(CONFIG_FILE, "w");
  if (!f) {
    Serial.println("[CONFIG] Failed to open file for writing.");
    return false;
  }

  serializeJsonPretty(doc, f);
  f.close();
  Serial.println("[CONFIG] Configuration saved.");
  return true;
}

String ConfigManager::encrypt(String raw) {
  String out = "";
  for (size_t i = 0; i < raw.length(); i++)
    out += char(raw[i] ^ XOR_KEY);
  return out;
}

String ConfigManager::decrypt(String enc) {
  return encrypt(enc); // XOR is symmetric
}
