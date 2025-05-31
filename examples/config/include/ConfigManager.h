#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#define CONFIG_FILE "/config.json"
#define MAX_SENSORS 32
#define XOR_KEY 0x5A

struct SensorConfig {
  bool active;
  String name;
  uint16_t homeeID;
  String type;
  uint32_t address;
  uint32_t signalOn;
  uint32_t signalOff;
  uint32_t signalAlarm;
  uint32_t signalBattery;
};

class ConfigManager {
public:
  bool cfgInSTA = false;
  String ssid = "";
  String password = "";
  String vhih_ip = "192.168.1.100";
  SensorConfig sensors[MAX_SENSORS];

  bool load();
  bool save();
private:
  String decrypt(String enc);
  String encrypt(String raw);
};
