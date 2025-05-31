#pragma once
#include <Arduino.h>

#define CONFIG_FILE "/config.json"
#define MAX_SENSORS 32
#define XOR_KEY 0xA5

struct SensorConfig {
  bool active = false;
  String name;
  uint16_t homeeID = 0;
  String type;
  uint32_t address = 0;
  uint32_t signalOn = 0;
  uint32_t signalOff = 0;
  uint32_t signalAlarm = 0;
  uint32_t signalBattery = 0;
};

class ConfigManager {
public:
  bool cfgInSTA = false;
  String ssid;
  String password;
  String clientIP;
  String gatewayIP;
  String subnet;

  SensorConfig sensors[MAX_SENSORS];

  bool load();
  bool save();

  static String encrypt(String raw);
  static String decrypt(String enc);
};