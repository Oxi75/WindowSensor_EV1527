#pragma once
#include <Arduino.h>

#define CONFIG_FILE "/config.json"
#define MAX_SENSORS 32
#define XOR_KEY 0xA5

struct SensorConfig
{
  bool active = false;
  uint16_t homeeID = 0;
  String type;
  String name;
  uint32_t address = 0x0;

  int32_t signalOn = -1;
  int32_t autoOffDelay = -1;
  int32_t signalOff = -1;
  int32_t signalAlarm = -1;
  int32_t signalAlarmOffDelay = -1;
  int32_t signalBattery = -1;
  int32_t signalBatteryOffDelay = -1;
};

class ConfigManager {
public:
  bool cfgInSTA = false;
  bool cfgInStandardMode = false;     // neue Checkbox „Enable configuration in standard mode“

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