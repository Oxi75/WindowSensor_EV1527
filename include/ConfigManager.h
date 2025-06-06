#pragma once
#include <Arduino.h>

#define CONFIG_FILE "/config.json"
#define MAX_SENSORS 32
#define XOR_KEY 0xA5

/* old code snippet
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
*/


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