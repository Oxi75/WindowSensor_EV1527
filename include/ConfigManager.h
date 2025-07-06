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

  // Signal values for the different sensor types
  int32_t signal1 = -1;     // Push Button 1 / Open
  int32_t signal2 = -1;     // Push Button 2 / Close
  int32_t signal3 = -1;     // Push Button 3 / Alarm
  int32_t signal4 = -1;     // Push Button 4 / Battery

  bool toggle1 = true; // Toggle for signal1 (true = toggle, false = re-trigger)
  bool toggle2 = true; // Toggle for signal2 (true = toggle, false = re-trigger)
  bool toggle3 = true; // Toggle for signal2 (true = toggle, false = re-trigger)
  bool toggle4 = true; // Toggle for signal2 (true = toggle, false = re-trigger)
  
  // Auto-release delays in seconds (double to support fractional seconds)
  double delay1 = NAN;      // Delay for signal1 (NaN = no auto-release)
  double delay2 = NAN;      // Delay for signal2 (NaN = no auto-release)
  double delay3 = NAN;      // Delay for signal3 (NaN = no auto-release)
  double delay4 = NAN;      // Delay for signal4 (NaN = no auto-release)
};


struct SensorRuntimeData
{
  uint8_t btnCnt = 0; // Number of buttons (1-4)
  bool OCSensor = false; // Is this an Open/Close Sensor?
  
  bool signal1 = false; // Push/Button1 signal
  bool signal2 = false; // Release/Button2 signal
  bool signal3 = false; // Alarm/Button3 signal
  bool signal4 = false; // Battery/Button4 signal

  double signal1_val = 0.0; // Push/Button1 value
  double signal2_val = 0.0; // Release/Button2 value
  double signal3_val = 0.0; // Alarm/Button3 value
  double signal4_val = 0.0; // Battery/Button4 value

  double signal1_TS = 0;      // Timestamp of last signal1
  double signal2_TS = 0;      // Timestamp of last signal2 
  double signal3_TS = 0;      // Timestamp of last signal3
  double signal4_TS = 0;      // Timestamp of last signal4
};


class ConfigManager
{
public:
  ConfigManager();

  bool cfgInSTA = false;
  bool cfgInStandardMode = false;     // neue Checkbox „Enable configuration in standard mode"

  String ssid;
  String password;
  String clientIP;
  String gatewayIP;
  String subnet;

  
  SensorConfig sensors[MAX_SENSORS];
  SensorRuntimeData RTData[MAX_SENSORS];

  bool load();
  bool save();

  static String encrypt(String raw);
  static String decrypt(String enc);
};