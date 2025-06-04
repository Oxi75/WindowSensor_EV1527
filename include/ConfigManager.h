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
  int32_t signal1 = -1;     // Push/Button1 signal
  int32_t signal2 = -1;     // Release/Button2 signal  
  int32_t signal3 = -1;     // Alarm/Button3 signal
  int32_t signal4 = -1;     // Battery/Button4 signal
  
  // Auto-release delays in seconds (double to support fractional seconds)
  double delay1 = NAN;      // Delay for signal1 (NaN = no auto-release)
  double delay2 = NAN;      // Delay for signal2 (NaN = no auto-release)
  double delay3 = NAN;      // Delay for signal3 (NaN = no auto-release)
  double delay4 = NAN;      // Delay for signal4 (NaN = no auto-release)

  // Legacy fields for backward compatibility (will be mapped to signal1-4)
  int32_t signalPushed = -1;
  int32_t autoReleaseDelay = -1;
  int32_t signalReleased = -1;
  int32_t signalAlarm = -1;
  int32_t autoAlarmOffDelay = -1;
  int32_t signalBattery = -1;
  int32_t autoBatteryOffDelay = -1;

  // Runtime data (not saved to config file)
  uint32_t signalPushed_TS = 0;      // Timestamp of last signalOn
  uint32_t signalReleased_TS = 0;   // Timestamp of last signalOff
  uint32_t signalAlarm_TS = 0;     // Timestamp of last signalAlarm
  uint32_t signalBattery_TS = 0;   // Timestamp of last signalBattery
  uint32_t signalAlarmCount = 0;   // Count of signalAlarm events
  uint32_t signalBatteryCount = 0; // Count of signalBattery events

  double valueState = 0.0;       // Value state for the sensor: released = 0; pushed = 1
  double valueAlarm = 0.0;       // Value for alarm state: no Alarm = 0; Alarm = 1
  double valueBattery = 0.0;     // Value for battery level
};

class ConfigManager {
public:
  bool cfgInSTA = false;
  bool cfgInStandardMode = false;     // neue Checkbox „Enable configuration in standard mode"

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