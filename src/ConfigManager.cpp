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
  cfgInStandardMode = doc["system"]["cfgInStandardMode"] | false;  
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
    sensors[i].active   = s["active"] | false;
    sensors[i].name     = s["name"] | "";
    sensors[i].homeeID  = s["homeeID"] | 0;
    sensors[i].type     = s["type"] | "OpenClose";
    sensors[i].address  = s["address"] | 0;
    
    // Load new signal configuration
    sensors[i].signal1  = s["signal1"] | 0;
    sensors[i].signal2  = s["signal2"] | 0;
    sensors[i].signal3  = s["signal3"] | 0;
    sensors[i].signal4  = s["signal4"] | 0;
    
    // Load delay configuration - handle NaN values properly
    if (s.containsKey("delay1") && !s["delay1"].isNull()) {
      sensors[i].delay1 = s["delay1"];
    } else {
      sensors[i].delay1 = NAN;
    }
    if (s.containsKey("delay2") && !s["delay2"].isNull()) {
      sensors[i].delay2 = s["delay2"];
    } else {
      sensors[i].delay2 = NAN;
    }
    if (s.containsKey("delay3") && !s["delay3"].isNull()) {
      sensors[i].delay3 = s["delay3"];
    } else {
      sensors[i].delay3 = NAN;
    }
    if (s.containsKey("delay4") && !s["delay4"].isNull()) {
      sensors[i].delay4 = s["delay4"];
    } else {
      sensors[i].delay4 = NAN;
    }   
    
    RTData[i].OCSensor = false;
    if (sensors[i].type == "OpenClose Sensor")
    {
      RTData[i].OCSensor = true; // Mark as Open/Close Sensor
      RTData[i].btnCnt = 4;
    }     
    else if (sensors[i].type == "FourButton Remote") RTData[i].btnCnt = 4; // Four buttons
    else if (sensors[i].type == "ThreeButton Remote") RTData[i].btnCnt = 3; // Three buttons
    else if (sensors[i].type == "TwoButton Remote") RTData[i].btnCnt = 2; // Two buttons
    else if (sensors[i].type == "OneButton Remote") RTData[i].btnCnt = 1; // One button
    else RTData[i].btnCnt = 0; // Unknown type, no buttons

    i++;
  }

  if (i == 0)
  {
    Serial.println("[CONFIG] No sensors in config. Setting defaults.");
    sensors[0].active = true;
    sensors[0].name = "DefaultSensor";
    sensors[0].homeeID = 1;
    sensors[0].type = "OpenClose";
    sensors[0].address = 0;
    sensors[0].signal1 = 9999;
    sensors[0].signal2 = 9999;
    sensors[0].signal3 = 9999;
    sensors[0].signal4 = 9999;
    sensors[0].delay1 = NAN;
    sensors[0].delay2 = NAN;
    sensors[0].delay3 = NAN;
    sensors[0].delay4 = NAN;    
  }
  else {
    Serial.printf("[CONFIG] %d sensor(s) loaded.\n", i);
  }

  Serial.println("[CONFIG] Configuration loaded.");
  return true;
}

bool ConfigManager::save()
{
  DynamicJsonDocument doc(4096);

  Serial.println("[CONFIG] Saving configuration...\n");

   
  doc["system"]["cfgInSTA"] = cfgInSTA;
  doc["system"]["cfgInStandardMode"] = cfgInStandardMode;  
  doc["wifi"]["ssid"] = ssid;
  doc["wifi"]["pwX"]  = encrypt(password);
  doc["wifi"]["ip"]   = clientIP;
  doc["wifi"]["gw"]   = gatewayIP;
  doc["wifi"]["mask"] = subnet;

  JsonArray arr = doc.createNestedArray("sensors");
  for (int i = 0; i < MAX_SENSORS; i++)
  {
    if (sensors[i].name == "") continue;

    Serial.printf("[CONFIG] Saving sensor %d: %s (ID: %d)", i, sensors[i].name.c_str(), sensors[i].homeeID);
    Serial.printf(", Type: %s, Address: 0x%02X", sensors[i].type.c_str(), sensors[i].address);
    Serial.printf(",  Signals - 1: %d, 2: %d, 3: %d, 4: %d\n",
                   sensors[i].signal1, sensors[i].signal2, sensors[i].signal3, sensors[i].signal4);
    
    JsonObject s = arr.createNestedObject();
    s["active"]   = sensors[i].active;
    s["name"]     = sensors[i].name;
    s["homeeID"]  = sensors[i].homeeID;
    s["type"]     = sensors[i].type;
    s["address"]  = sensors[i].address;
    
    // Save new signal configuration
    s["signal1"]  = sensors[i].signal1;
    s["signal2"]  = sensors[i].signal2;
    s["signal3"]  = sensors[i].signal3;
    s["signal4"]  = sensors[i].signal4;
    
    // Save delay configuration - handle NaN values properly
    if (!isnan(sensors[i].delay1)) {
      s["delay1"] = sensors[i].delay1;
    }
    if (!isnan(sensors[i].delay2)) {
      s["delay2"] = sensors[i].delay2;
    }
    if (!isnan(sensors[i].delay3)) {
      s["delay3"] = sensors[i].delay3;
    }
    if (!isnan(sensors[i].delay4)) {
      s["delay4"] = sensors[i].delay4;
    }    
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