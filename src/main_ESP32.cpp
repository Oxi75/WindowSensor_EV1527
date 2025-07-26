/*
  EV1527 Window Sensor Monitor for homee
  
  This program monitors up to 32 window sensors based on the EV1527 chip
  and connects to the homee smart home system.
  
  Multiple operation modes:
  1. Configuration mode (AP mode for setup)
  2. Normal mode (sensor monitoring and homee integration)
  
  Hardware: MH-ET Live ESP32 DevKit
  
  Libraries:
  - RCSwitch: https://github.com/sui77/rc-switch/
  - homee-api-esp32: https://github.com/Oxi75/homee-api-esp32
*/

#include "global_defines.h"
#include <Arduino.h>
#include <Update.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include <LittleFS.h>
#include "ConfigManager.h"
#include <string>
#include "virtualHomee.hpp"

#define RECEIVER_CHECK_INTERVAL 350             // Interval in ms to check for received signals

String FW_VERSION_STR = String(FW_VERSION_ESP, 2);

#define CANodeProfileOneButtonRemote 20
#define CANodeProfileTwoButtonRemote 24
#define CANodeProfileThreeButtonRemote 25
#define CANodeProfileFourButtonRemote 26

#define CANodeProfileOpenCloseSensor 2000
#define CAAttributeTypeOpenClose 14
#define CAAttributeTypeAlarm 108
//#define CAAttributeTypeBatteryLevel 8
#define CAAttributeTypeBatteryLowAlarm 69
#define CAAttributeTypeOnOff 1
#define CAAttributeTypeFirmwareRevision 44
#define CAAttributeTypeBinaryInput 19
#define CAAttributeTypeButtonState 40
#define CAAttributeTypeNone 0

#define AttrID_SnsAddr    0b00100000  //one part of the Attribute ID of homee, the other one is the sensor number
#define AttrID_FwRev      0b01000000  //one part of the Attribute ID of homee, the other one is the sensor number
#define AttrID_Sig1       0b01100000  //one part of the Attribute ID of homee, the other one is the sensor number
#define AttrID_Sig2       0b10000000  //one part of the Attribute ID of homee, the other one is the sensor number
#define AttrID_Sig3       0b10100000  //one part of the Attribute ID of homee, the other one is the sensor number
#define AttrID_Sig4       0b11000000  //one part of the Attribute ID of homee, the other one is the sensor number


const char* AP_SSID = "EV1527 for homee";
const char* AP_PASS = "12345678";
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_SUBNET(255, 255, 255, 0);
bool isAPMode = false;

ConfigManager config;

// LED related functions
static unsigned long lastBlinkTime = 0;
static const unsigned long blinkInterval = 500; // 500ms Blink-Intervall
static bool ledState = false;

void LED_setup()
{
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH); // LED aus (meist ist LOW = an bei ESP8266)
}

void ledOn()
{
    digitalWrite(PIN_LED, LOW); // LED einschalten (LOW = an beim ESP8266)
    ledState = true; // LED-Status setzen
    lastBlinkTime = millis() - blinkInterval - 1; // Zeitstempel aktualisieren
}

void ledOff()
{
    digitalWrite(PIN_LED, HIGH); // LED ausschalten (HIGH = aus beim ESP8266)
    ledState = false; // LED-Status zurücksetzen
    lastBlinkTime = millis() - blinkInterval - 1; // Zeitstempel aktualisieren    
}

void ledToggle()
{
    ledState = !ledState;
    digitalWrite(PIN_LED, ledState ? LOW : HIGH); // LED umschalten
    lastBlinkTime = millis() - blinkInterval - 1; // Zeitstempel aktualisieren
}

void ledBlink()
{
    unsigned long currentMillis = millis();
    if (currentMillis - lastBlinkTime >= blinkInterval)
    {
        lastBlinkTime = currentMillis;
        ledState = !ledState;
        digitalWrite(PIN_LED, ledState ? LOW : HIGH); // LED umschalten
    }
}

//Sensor Daten
struct LastSignal
{
    String sensorAddress;
    String sensorData;
    String binary;
};

// Globale Variable mit dem letzten Signal
LastSignal lastSignal = {
    .sensorAddress = "-------",
    .sensorData = "-",
    .binary = "------------------------",
};
 

//WiFi realted functions
AsyncWebServer server(80);

unsigned long lastWifiCheckTime = 0;
const unsigned long wifiCheckInterval = 30000; // Alle 30 Sekunden WLAN prüfen
const unsigned long wifiMaxAttempts = 20; // 
static uint32_t wifiConnectAttempts = 0; // Anzahl der Versuche, sich mit dem WLAN zu verbinden
static bool WiFi_reconnect = false;  //system is in reconnection
String configBodyContent = "";  //variable to hold the HTML content of the config page

IPAddress IPAddressFromString(const String& str) {
  IPAddress ip;
  if (!ip.fromString(str)) {
    Serial.println("[IP] Invalid IP string: " + str);
    return IPAddress(0, 0, 0, 0);
  }
  return ip;
}

void startWiFi() 
{ 
  pinMode(PIN_SYSMODE, INPUT_PULLUP);
  isAPMode = digitalRead(PIN_SYSMODE) == LOW;

  if (isAPMode)
  {
    Serial.println("[BOOT] Starting in AccessPoint mode");
    WiFi.softAPConfig(AP_IP, AP_IP, AP_SUBNET);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.println("[AP] SSID: " + String(AP_SSID));
    Serial.println("[AP] Password: " + String(AP_PASS));
    Serial.println("[AP] IP: " + WiFi.softAPIP().toString());
  }
  else
  {
    Serial.println("[BOOT] Starting in Standard mode");
    WiFi.config(
      IPAddressFromString(config.clientIP),
      IPAddressFromString(config.gatewayIP),
      IPAddressFromString(config.subnet)
    );
    WiFi.begin(config.ssid.c_str(), config.password.c_str());

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 20)
    {
      ledToggle(); // LED blinken lassen, um den Verbindungsversuch anzuzeigen
      delay(500);
      Serial.print(".");
      retries++;
    }

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("\n[WIFI] Connection failed. Restarting...");
      ESP.restart();
    }

    Serial.println("\n[WIFI] Connected. IP: " + WiFi.localIP().toString());
  }

  ledOn(); // alten Zustand der LED wiederherstellen
}


void WiFi_check(bool restart = true)
{
    if ((millis() - lastWifiCheckTime >= wifiCheckInterval) || (WiFi_reconnect))
    {
        lastWifiCheckTime = millis();
    
        // Bei Verbindungsverlust erneut verbinden
        if (WiFi.status() != WL_CONNECTED) 
        {
            WiFi_reconnect = true;
            Serial.println("WiFi connection lost. Reconnecting...");
            ledToggle(); // LED blinken lassen, um den Verbindungsverlust anzuzeigen
            WiFi.reconnect();
            wifiConnectAttempts++;
            delay(500);
         }

        if (WiFi.status() == WL_CONNECTED)
        {
            WiFi_reconnect = false;
            wifiConnectAttempts = 0; // WLAN-Verbindung erfolgreich
            ledOff(); // LED ausschalten, wenn WLAN verbunden ist
        }

        if (wifiConnectAttempts >= wifiMaxAttempts) 
        {
            Serial.println("Failed to reconnect to WiFi. Restarting ESP...");
            ESP.restart(); // ESP32 zurücksetzen, wenn keine Verbindung hergestellt werden kann
        }    
    }
}


// Homee related functions
virtualHomee vhih;

void homee_setup()
{
  for (int sns = 0; sns < MAX_SENSORS; sns++)
  {
    if ((config.sensors[sns].name == "") || (!config.sensors[sns].active) || (config.sensors[sns].homeeID == 0))
    {
      Serial.printf("[HOMEE] Sensor %d is not active or has no name or invalid homeeID -> skipping.\n", sns);      
      continue;
    }

    if ((config.RTData[sns].btnCnt > 4) || (config.RTData[sns].btnCnt < 1))
    {
      Serial.printf("[HOMEE] Sensor %d has invalid button count %d or an invalid type '%s' -> skipping.\n", sns, config.RTData[sns].btnCnt, config.sensors[sns].type.c_str());
      continue;
    }
/*
    if ((config.sensors[sns].type != "OpenClose Sensor") 
     && (config.sensors[sns].type != "OneButton Remote") && (config.sensors[sns].type != "TwoButton Remote")
     && (config.sensors[sns].type != "ThreeButton Remote") && (config.sensors[sns].type != "FourButton Remote"))
    {
      Serial.printf("[CONFIG] Sensor %d has an invalid type '%s' -> skipping.\n", sns, config.sensors[sns].type.c_str());
      continue;
    }
*/

    Serial.printf("[HOMEE] setup sensor %s with address 0x%05x as %s at homee device with ID %d\n", config.sensors[sns].name.c_str(), config.sensors[sns].address, config.sensors[sns].type.c_str(), config.sensors[sns].homeeID);

    //all checks done, now new add the new sensor with all its attributes to homee    
    node *n;
    nodeAttributes *na, *naAddr, *naFW;

    naAddr = new nodeAttributes(CAAttributeTypeNone);  //
    naAddr->setName("Sensor Address");
    naAddr->setId(AttrID_SnsAddr | sns);
    naAddr->setUnit("0x" + String(config.sensors[sns].address, HEX));  //set unit to the address in hex format
    naAddr->setMinimumValue(0);
    naAddr->setMaximumValue(999999999999); 
    naAddr->setCurrentValue(config.sensors[sns].address);
    naAddr->setEditable(false);
    naAddr->setCallback(nullptr);

    naFW = new nodeAttributes(CAAttributeTypeFirmwareRevision); 
    naFW->setName("Firmware Version");
    naFW->setId(AttrID_FwRev | sns); //unique ID for each sensor
    naFW->setUnit("");  
    naFW->setMinimumValue(0.0);
    naFW->setMaximumValue(1000.0); 
    naFW->setCurrentValue(FW_VERSION_ESP);
    naFW->setEditable(false);
    naFW->setCallback(nullptr);

    if (config.RTData[sns].OCSensor)
    {
      n = new node(config.sensors[sns].homeeID, CANodeProfileOpenCloseSensor, config.sensors[sns].name);     
      n->AddAttributes(naAddr);     //set attribute Sensor Address to node
      n->AddAttributes(naFW);       //set attribute Firmware to node

      //Attribut Status
      na = new nodeAttributes(CAAttributeTypeOpenClose);  //open / closed state
      na->setName("Status");
      na->setId(AttrID_Sig1 | sns); //unique ID for each sensor        
      na->setMinimumValue(0);
      na->setMaximumValue(1); 
      na->setCurrentValue(0);
      na->setCallback(nullptr);
      na->setEditable(false);
      n->AddAttributes(na);       //set attribute to node

      //Attribut Alarm
      na = new nodeAttributes(CAAttributeTypeAlarm);  //CAAttributeTypeAlarm
      na->setName("Alarm");
      na->setId(AttrID_Sig3 | sns);   //unique ID for each sensor
      na->setUnit("");  
      na->setMinimumValue(0);
      na->setMaximumValue(1); 
      na->setCurrentValue(0);     //default value
      na->setEditable(false);
      na->setCallback(nullptr);
      n->AddAttributes(na);       //set attribute to node

      //Attribut Batterie
      na = new nodeAttributes(CAAttributeTypeBatteryLowAlarm);
      na->setName("Battery Status");
      na->setId(AttrID_Sig4 | sns); //unique ID for each sensor
      na->setUnit("");
      na->setMinimumValue(0);  //0 = Battery OK, 1 = Battery low
      na->setMaximumValue(1); 
      na->setCurrentValue(0);  //default value
      na->setEditable(false);
      na->setCallback(nullptr);
      n->AddAttributes(na);       //set attribute to node
    }
    else  //must be an x-button remote
    {
      uint32_t vhihProfile = 0;
      if (config.RTData[sns].btnCnt == 1)  vhihProfile = CANodeProfileOneButtonRemote;
      else if (config.RTData[sns].btnCnt == 2) vhihProfile = CANodeProfileTwoButtonRemote;
      else if (config.RTData[sns].btnCnt == 3) vhihProfile = CANodeProfileThreeButtonRemote;
      else if (config.RTData[sns].btnCnt == 4) vhihProfile = CANodeProfileFourButtonRemote;

      n = new node(config.sensors[sns].homeeID, vhihProfile, config.sensors[sns].name);
      n->AddAttributes(naAddr);     //set attribute Sensor Address to node
      n->AddAttributes(naFW);       //set attribute Firmware to node


      //Attribut for first Button
      na = new nodeAttributes(CAAttributeTypeButtonState);  //open / closed state
      na->setName("Status Button1");
      na->setId(AttrID_Sig1 | sns); //unique ID for each sensor        
      na->setMinimumValue(0);
      na->setMaximumValue(1); 
      na->setCurrentValue(0);
      na->setCallback(nullptr);
      na->setEditable(false);
      n->AddAttributes(na);       //set attribute to node

      if (config.RTData[sns].btnCnt >= 2)
      {
        //Attribut Status Button2
        na = new nodeAttributes(CAAttributeTypeButtonState);  //open / closed state
        na->setName("Status Button2");
        na->setId(AttrID_Sig2 | sns); //unique ID for each sensor        
        na->setMinimumValue(0);
        na->setMaximumValue(1); 
        na->setCurrentValue(0);
        na->setCallback(nullptr);
        na->setEditable(false);
        n->AddAttributes(na);       //set attribute to node
      }


      if (config.RTData[sns].btnCnt >= 3)
      {
        //Attribut Status Button3
        na = new nodeAttributes(CAAttributeTypeButtonState);  //open / closed state
        na->setName("Status Button3");
        na->setId(AttrID_Sig3 | sns); //unique ID for each sensor        
        na->setMinimumValue(0);
        na->setMaximumValue(1); 
        na->setCurrentValue(0);
        na->setCallback(nullptr);
        na->setEditable(false);
        n->AddAttributes(na);       //set attribute to node
      }

      if (config.RTData[sns].btnCnt == 4)
      {
        //Attribut Status Button4
        na = new nodeAttributes(CAAttributeTypeButtonState);  //open / closed state
        na->setName("Status Button4");
        na->setId(AttrID_Sig4 | sns); //unique ID for each sensor        
        na->setMinimumValue(0);
        na->setMaximumValue(1); 
        na->setCurrentValue(0);
        na->setCallback(nullptr);
        na->setEditable(false);
        n->AddAttributes(na);       //set attribute to node
      }
    }

    vhih.addNode(n); //add the new node to homee
  }

  vhih.start();  //start homee API

  Serial.println("Homee configured");
  delay(1000);  
}


static double lastValue = 1;
void homee_updateValues(uint32_t snsNo)
{  
  Serial.printf("update homee values for Sensor No %d (%s)\n", snsNo, config.sensors[snsNo].type.c_str());

  nodeAttributes *na;
  
  if ((config.RTData[snsNo].btnCnt >= 4) && (config.RTData[snsNo].signal4))
  {
    na = vhih.getAttributeById(AttrID_Sig4 | snsNo);
    na->setCurrentValue(config.RTData[snsNo].signal4_val);
    config.RTData[snsNo].signal4 = false;
    if (na) vhih.updateAttributeValue(na, na->getCurrentValue());
    delay(200);
    yield();
  } 

  if ((config.RTData[snsNo].btnCnt >= 3) && (config.RTData[snsNo].signal3))
  {
    na = vhih.getAttributeById(AttrID_Sig3 | snsNo);
    na->setCurrentValue(config.RTData[snsNo].signal3_val);
    config.RTData[snsNo].signal3 = false;
    if (na) vhih.updateAttributeValue(na, na->getCurrentValue());
    delay(200);
    yield();
  } 

  if ((config.RTData[snsNo].btnCnt >= 2) && (config.RTData[snsNo].OCSensor == false) && (config.RTData[snsNo].signal2))
  {
    na = vhih.getAttributeById(AttrID_Sig2 | snsNo);
    na->setCurrentValue(config.RTData[snsNo].signal2_val);
    config.RTData[snsNo].signal2 = false;
    if (na) vhih.updateAttributeValue(na, na->getCurrentValue());
    delay(200);
    yield();
  } 

  if ((config.RTData[snsNo].btnCnt >= 1) && (config.RTData[snsNo].OCSensor == false) && (config.RTData[snsNo].signal1))
  {
    na = vhih.getAttributeById(AttrID_Sig1 | snsNo);
    na->setCurrentValue(config.RTData[snsNo].signal1_val);
    config.RTData[snsNo].signal1 = false;
    if (na) vhih.updateAttributeValue(na, na->getCurrentValue());
    delay(200);
    yield();
  } 

  if ((config.RTData[snsNo].OCSensor) && (config.RTData[snsNo].signal1))
  {
    na = vhih.getAttributeById(AttrID_Sig1 | snsNo);
    na->setCurrentValue(config.RTData[snsNo].signal1_val);
    config.RTData[snsNo].signal1 = false;
    if (na) vhih.updateAttributeValue(na, na->getCurrentValue());
    delay(200);
    yield();
  } 

} 





/// ****************************************************
/// @brief Check for received signals and update sensor data accordingly.
/// @param HomeeEnabled 
/// ****************************************************
void Receiver_setup()
{
  pinMode(PIN_RECEIVER_PWR, OUTPUT);    // set the receiver power pin as output
  digitalWrite(PIN_RECEIVER_PWR, LOW); // switch off the receiver power
  delay(500); // wait for the receiver to stabilize
  digitalWrite(PIN_RECEIVER_PWR, HIGH); // switch on the receiver power
  delay(2000); // wait for the receiver to stabilize

  Wire.begin(PIN_SDA, PIN_SCL);

  Serial.println("[Receiver] setup complete.");
}


// Signal related functions
uint32_t Receiver_getSignal()
{
  uint32_t sensorData = 0;

  Wire.requestFrom(I2C_SLAVE_ADDRESS, 4); //read 4 bytes from the I2C slave device

  if (Wire.available() >= 4)
  {
      sensorData  = Wire.read();
      sensorData |= Wire.read() << 8;
      sensorData |= Wire.read() << 16;
      sensorData |= Wire.read() << 24;        
  }
  else sensorData = DUMMY_CODE_E; // Rückgabe eines Dummy-Wertes, wenn nicht genügend Daten verfügbar sind

//  Serial.printf("Receiver_getSignal(): Received signal: 0x%08x\n", sensorData);

  return sensorData; 
}

void Receiver_check(bool HomeeEnabled)
{
  static bool firstCall = true;
  if (firstCall)
  {
    Serial.print("Receiver_check() for the first time.");
    if (HomeeEnabled) Serial.println(" Homee is enabled.");
    else Serial.println(" Homee is disabled.");
    firstCall = false;
  }

  uint32_t snsAddr = 0;       
  uint32_t snsValue = 0;
  uint32_t now = millis();    // current timestamp
  bool validSignal = false;   // Flag to check if a valid signal was received

  uint32_t sensorData = Receiver_getSignal(); // Lese die Signaldaten von Arduino
  
//  if (sensorData == DUMMY_CODE_F) Serial.println("Receiver_check(): Watchdog-Signal received, no sensor data available.");
//  else if (sensorData == DUMMY_CODE_0) Serial.println("Receiver_check(): FiFo empty, no sensor data available.");
//  else Serial.printf("Receiver_check(): Received signal: 0x%08x\n", sensorData);
  now = millis();                                     // update the timestamp of the last received, valid signal

  if (((sensorData != DUMMY_CODE_0) && (sensorData != DUMMY_CODE_F) && (sensorData != DUMMY_CODE_E)))
  {
    ledOn();                                            // Switch on the LED to indicate signal reception
    validSignal = true;                                 // signal is valid, so set the flag to true

    lastSignal.binary = String(sensorData, BIN);        // convert the sensor data to binary string

    snsValue = sensorData & 0x000000F;                  // ectract the lower 4 bit as value
    lastSignal.sensorData = String(snsValue, DEC);      

    snsAddr = (sensorData >> 4) & 0xFFFFF;              // extract the sensor address from the upper 20 bits
    lastSignal.sensorAddress = String(snsAddr, HEX);    
    lastSignal.sensorAddress.toUpperCase();             

//    Serial.printf("Received signal: Address: 0x%05x, Value: %d, Binary: %s\n", snsAddr, snsValue, lastSignal.binary.c_str());
  }

  bool sensorIdentified = false; // Flag to check if the sensor address is identified
  for (uint32_t sns = 0; sns < MAX_SENSORS; sns++)    //loop over all sensors to check if an update is required - this can be based on the received signal or by the auto-release feature
  {
    // check if the sensorID is active and has a valid configuration; if not, skip it
    if ((config.sensors[sns].name == "") || (!config.sensors[sns].active) || (config.sensors[sns].homeeID == 0) || (config.sensors[sns].address == 0))
    {
      continue;
    }

    // if a valid signal was received and the sensor address matches the current sesnor configuration
    if ((validSignal) && (config.sensors[sns].address == snsAddr)) 
    {
//      Serial.printf("Sensor %s has send value %d\n", config.sensors[sns].name.c_str(), snsValue); // Debug-Ausgabe der Adresse und des Wertes
      sensorIdentified = true; // set the flag to true, if the sensor address is identified

      if (snsValue == config.sensors[sns].signal1)
      {
        if ((config.RTData[sns].OCSensor) || (!config.RTData[sns].OCSensor && !config.sensors[sns].toggle1))  //OC-Sensor supports only set to 1.0; Remote-Buttons without toggle support re-trigger
        {
          config.RTData[sns].signal1_val = 1.0;   //switch to on / pressed / high state
        }
        else  //Remote-Buttons with toggle will invert the value
        {
          config.RTData[sns].signal1_val = (config.RTData[sns].signal1_val == 0.0) ? 1.0 : 0.0;   //invert the value of signal1
        }

        config.RTData[sns].signal1 = true;      //signalizes that signal1 hast been changed
        config.RTData[sns].signal1_TS = now;    //update timestamp for signal1
      }

      if ((validSignal) && (snsValue == config.sensors[sns].signal2))
      {
        if (config.RTData[sns].OCSensor)       //if it is an open/close sensor, signal 2 is the reset of signal 1
        {
          config.RTData[sns].signal1_val = 0;  // switch signal1 back to off / released / low state
          config.RTData[sns].signal1 = true;   // signalizes that signal1 has been changed
        }
        else
        {
          if (config.sensors[sns].toggle2) config.RTData[sns].signal2_val = (config.RTData[sns].signal2_val == 0.0) ? 1.0 : 0.0;   //invert the value of signal2
          else config.RTData[sns].signal2_val = 1.0; // Remote-Buttons without toggle support will set signal2 to 1.0
          config.RTData[sns].signal2 = true;     //signalizes that signal2 has been changed
        }
        config.RTData[sns].signal2_TS = now;    //update timestamp for signal2
      } 

      if ((validSignal) && (snsValue == config.sensors[sns].signal3))
      {
        if ((config.RTData[sns].OCSensor) || (!config.RTData[sns].OCSensor && !config.sensors[sns].toggle3))  //OC-Sensor supports only set to 1.0; Remote-Buttons without toggle support re-trigger
        {
          config.RTData[sns].signal3_val = 1.0;   //switch to on / pressed / high state
        }
        else  //Remote-Buttons with toggle will invert the value
        {
          config.RTData[sns].signal3_val = (config.RTData[sns].signal3_val == 0.0) ? 1.0 : 0.0;   //invert the value of signal1
        }

        config.RTData[sns].signal3 = true;      //signalizes that signal1 hast been changed
        config.RTData[sns].signal3_TS = now;    //update timestamp for signal1
      }

      if ((validSignal) && (snsValue == config.sensors[sns].signal4))
      {
        if (config.RTData[sns].OCSensor) config.RTData[sns].signal4_val = 1;                            // set low battery alarm
        else 
        {
          if (!config.sensors[sns].toggle4) config.RTData[sns].signal4_val = 1.0;                       // switch to on / pressed / high state
          else  config.RTData[sns].signal4_val = (config.RTData[sns].signal4_val == 0.0) ? 1.0 : 0.0;   //invert the value of signal1
        }

        config.RTData[sns].signal4 = true;              // signalizes that signal4 hast been changed
        config.RTData[sns].signal4_TS = now;            //update timestamp for signal4
      }
    }

    // even if address does not match to the current transmission, the auto-release-feature might require to update the sensor state in homee   
    if ((config.RTData[sns].btnCnt >= 4) && (config.sensors[sns].delay4 > 0.4) && (now - config.RTData[sns].signal4_TS > (config.sensors[sns].delay4 * 1000)) && (config.RTData[sns].signal4_val != 0))
    {
      config.RTData[sns].signal4 = true;            //signal4 must be updated
      if (config.RTData[sns].OCSensor) config.RTData[sns].signal4_val = 66.0;        //set new battery level value which does not trigger a warning
      else config.RTData[sns].signal4_val = 0;       //button 4 is released
    }

    if ((config.RTData[sns].btnCnt >= 3) && (config.sensors[sns].delay3 > 0.4) && (now - config.RTData[sns].signal3_TS > (config.sensors[sns].delay3 * 1000)) && (config.RTData[sns].signal3_val != 0))
    {
      config.RTData[sns].signal3 = true;        //button3 must be updated
      config.RTData[sns].signal3_val = 0;       //button3 is released
    }

    if ((config.RTData[sns].btnCnt >= 2) && !(config.RTData[sns].OCSensor) && (config.sensors[sns].delay2 > 0.4) && (now - config.RTData[sns].signal2_TS > (config.sensors[sns].delay2 * 1000)) && (config.RTData[sns].signal2_val != 0))
    {
      config.RTData[sns].signal2 = true;        //button2 must be updated
      config.RTData[sns].signal2_val = 0;       //button2 is released
    }


    if ((config.RTData[sns].btnCnt >= 1) && !(config.RTData[sns].OCSensor) && (config.sensors[sns].delay1 > 0.4) && (now - config.RTData[sns].signal1_TS > (config.sensors[sns].delay1 * 1000)) && (config.RTData[sns].signal1_val != 0))
    {
      config.RTData[sns].signal1 = true;        //button2 must be updated
      config.RTData[sns].signal1_val = 0;       //button1 is released
    }

    //if any value has changed, we need to update the sensor data in homee
    if (config.RTData[sns].signal1 || config.RTData[sns].signal2 || config.RTData[sns].signal3 || config.RTData[sns].signal4)
    {
      Serial.printf("Sensor %s has changed: %.0f | %.0f | %.0f | %.0f\n", config.sensors[sns].name.c_str(), config.RTData[sns].signal1_val,
                     config.RTData[sns].signal2_val, config.RTData[sns].signal3_val, config.RTData[sns].signal4_val);

      if (HomeeEnabled) homee_updateValues(sns); // Update homee values for the sensor
      config.RTData[sns].signal1 = false; // Reset the signal after processing
      config.RTData[sns].signal2 = false;
      config.RTData[sns].signal3 = false;
      config.RTData[sns].signal4 = false;
    }
  } 

  if (!sensorIdentified && validSignal) // if no sensor was identified, but a valid signal was received
  {
    Serial.printf("Unknown sensor: Address: 0x%05x, Value: %d, Binary: %s\n", snsAddr, snsValue, lastSignal.binary.c_str());
  }
  ledOff();  // Switch off the LED after processing the signal

//  Serial.println("[Receiver] Signal processing complete.");
}


void webserver_setup()
{
  if ((isAPMode) || (config.cfgInStandardMode))
  {
    Serial.println("[WEB] Starting web server in configuration mode");
    server.serveStatic("/config.html", LittleFS, "/config.html");
  }
  else
  {
    Serial.println("[WEB] Starting web server in normal mode");
    server.serveStatic("/index.html", LittleFS, "/index.html");
  }

server.on("/config", HTTP_GET, [](AsyncWebServerRequest* req) {
  DynamicJsonDocument doc(16384);
  doc["fw"] = FW_VERSION_STR;
  doc["system"]["cfgInSTA"] = config.cfgInSTA;
  doc["wifi"]["ssid"] = config.ssid;
  doc["wifi"]["pw"] = config.password;
  doc["wifi"]["ip"] = config.clientIP;
  doc["wifi"]["gw"] = config.gatewayIP;
  doc["wifi"]["mask"] = config.subnet;

  JsonArray arr = doc.createNestedArray("sensors");
  for (int i = 0; i < MAX_SENSORS; i++) {
    if (config.sensors[i].name == "") continue;
    JsonObject s = arr.createNestedObject();
    s["active"] = config.sensors[i].active;
    s["name"] = config.sensors[i].name;
    s["homeeID"] = config.sensors[i].homeeID;
    s["type"] = config.sensors[i].type;
    s["address"] = String(config.sensors[i].address, HEX);
    s["signal1"] = config.sensors[i].signal1;
    s["signal2"] = config.sensors[i].signal2;
    s["signal3"] = config.sensors[i].signal3;
    s["signal4"] = config.sensors[i].signal4;
    s["delay1"] = config.sensors[i].delay1;
    s["delay2"] = config.sensors[i].delay2;
    s["delay3"] = config.sensors[i].delay3;
    s["delay4"] = config.sensors[i].delay4;
    s["toggle1"] = config.sensors[i].toggle1;
    s["toggle2"] = config.sensors[i].toggle2;
    s["toggle3"] = config.sensors[i].toggle3;
    s["toggle4"] = config.sensors[i].toggle4;    
  }

  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
});


  server.on("/config.json", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(LittleFS, CONFIG_FILE, "application/json");
  });
  

server.on("/config", HTTP_POST, 
  [](AsyncWebServerRequest* req) {
    // Dieser Handler wird aufgerufen, wenn alle Body-Daten empfangen wurden
    // Die eigentliche Verarbeitung passiert im Body-Handler unten
  }, 
  NULL,
  [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
    // Body-Handler - sammelt alle Chunks
    
    // Bei erstem Chunk: Buffer initialisieren
    if (index == 0) {
      configBodyContent = "";
      configBodyContent.reserve(total + 100); // Etwas extra Platz
      Serial.printf("[CONFIG] Starting to receive body, total size: %d bytes\n", total);
    }
    
    // Aktuellen Chunk hinzufügen
    for (size_t i = 0; i < len; i++) {
      configBodyContent += (char)data[i];
    }
    
    Serial.printf("[CONFIG] Received chunk: %d-%d of %d bytes\n", index, index + len - 1, total);
    
    // Wenn alle Daten empfangen wurden, JSON verarbeiten
    if (index + len == total) {
      Serial.printf("[CONFIG] Complete body received (%d bytes), processing JSON...\n", configBodyContent.length());
      
      DynamicJsonDocument doc(16384);
      DeserializationError error = deserializeJson(doc, configBodyContent);
      
      if (error) {
        Serial.println("[CONFIG] Failed to parse JSON.");
        Serial.print("[CONFIG] JSON Error: ");
        Serial.println(error.c_str());
        Serial.printf("[CONFIG] First 200 chars: %.200s\n", configBodyContent.c_str());
        Serial.printf("[CONFIG] Last 200 chars: %s\n", configBodyContent.substring(max(0, (int)configBodyContent.length() - 200)).c_str());
        req->send(400, "text/plain", "Invalid JSON");
        configBodyContent = ""; // Cleanup
        return;
      }

      // Debug: Print received JSON string
      //Serial.println("[CONFIG] Received JSON:");
      //serializeJsonPretty(doc, Serial);
      //Serial.println();

      // System configuration
      config.cfgInSTA = doc["system"]["cfgInSTA"] | false;
      config.cfgInStandardMode = doc["system"]["cfgInStandardMode"] | false;

      // WiFi configuration - handle both "pw" and "password" fields
      config.ssid = doc["wifi"]["ssid"].as<String>();
      if (doc["wifi"].containsKey("pw")) {
        config.password = doc["wifi"]["pw"].as<String>();
      } else if (doc["wifi"].containsKey("password")) {
        config.password = doc["wifi"]["password"].as<String>();
      }
      config.clientIP = doc["wifi"]["ip"].as<String>();
      config.gatewayIP = doc["wifi"]["gw"].as<String>();
      config.subnet = doc["wifi"]["mask"].as<String>();

      // Clear all sensors first
      for (int i = 0; i < MAX_SENSORS; i++) {
        config.sensors[i].name = "";
        config.sensors[i].active = false;
      }

      JsonArray arr = doc["sensors"].as<JsonArray>();
      uint16_t usedIDs[32] = {0};
      int valid = 0;

      for (int i = 0; i < arr.size() && i < MAX_SENSORS; i++)
      {
        JsonObject s = arr[i];
        bool deactivate = false;
        
        // Get and validate name
        const char* namePtr = s["name"].as<const char*>();
        String name;
        if (namePtr) {
          name = String(namePtr);
          name.trim();
        }
        if (!namePtr || name == "") {
          Serial.printf("[WARN] Sensor %d has invalid name, skipping.\n", i);
          deactivate = true;
        }

        // Get and validate homeeID
        uint16_t id = s["homeeID"] | 0;
        if (id == 0) {
          Serial.printf("[WARN] Sensor %d has invalid ID, skipping.\n", i);
          deactivate = true;
        }

        // Check for duplicate IDs
        bool duplicate = false;
        for (int k = 0; k < valid; k++) {
          if (usedIDs[k] == id) duplicate = true;
        }
        if (duplicate) {
          Serial.printf("[WARN] Duplicate homee-ID (%d), sensor %d skipped.\n", id, i);
          deactivate = true;
        }
        usedIDs[valid++] = id;

        // Configure sensor
        auto& sens = config.sensors[i];
        sens.active = s["active"] | true;
        sens.active = sens.active && !deactivate; // Deactivate if any validation failed
        sens.name = name;
        sens.homeeID = id;
        sens.type = s["type"].as<String>();
        
        // FIXED: Address handling - convert from number (already in hex format from frontend)
        sens.address = s["address"] | 0;
        
        // Signal configuration
        sens.signal1 = s["signal1"] | 0;
        sens.signal2 = s["signal2"] | 0;
        sens.signal3 = s["signal3"] | 0;
        sens.signal4 = s["signal4"] | 0;

        // toggle configuration
        sens.toggle1 = s["toggle1"] | false;
        sens.toggle2 = s["toggle2"] | false;
        sens.toggle3 = s["toggle3"] | false;
        sens.toggle4 = s["toggle4"] | false;        
        
        // FIXED: Delay handling - properly handle null/NaN values
        if (s.containsKey("delay1") && !s["delay1"].isNull()) {
          sens.delay1 = s["delay1"].as<double>();
        } else {
          sens.delay1 = NAN;
        }
        
        if (s.containsKey("delay2") && !s["delay2"].isNull()) {
          sens.delay2 = s["delay2"].as<double>();
        } else {
          sens.delay2 = NAN;
        }
        
        if (s.containsKey("delay3") && !s["delay3"].isNull()) {
          sens.delay3 = s["delay3"].as<double>();
        } else {
          sens.delay3 = NAN;
        }
        
        if (s.containsKey("delay4") && !s["delay4"].isNull()) {
          sens.delay4 = s["delay4"].as<double>();
        } else {
          sens.delay4 = NAN;
        }

        // Set up runtime data
        config.RTData[i].OCSensor = false;
        if (sens.type == "OpenClose Sensor") {
          config.RTData[i].OCSensor = true;
          config.RTData[i].btnCnt = 4;
        } else if (sens.type == "FourButton Remote") {
          config.RTData[i].btnCnt = 4;
        } else if (sens.type == "ThreeButton Remote") {
          config.RTData[i].btnCnt = 3;
        } else if (sens.type == "TwoButton Remote") {
          config.RTData[i].btnCnt = 2;
        } else if (sens.type == "OneButton Remote") {
          config.RTData[i].btnCnt = 1;
        } else {
          config.RTData[i].btnCnt = 0;
        }

        Serial.printf("[CONFIG] Configured sensor %d: %s (ID: %d, Type: %s, Addr: 0x%X)\n", 
                      i, sens.name.c_str(), sens.homeeID, sens.type.c_str(), sens.address);
        Serial.printf("  Signals: %d, %d, %d, %d\n", 
                      sens.signal1, sens.signal2, sens.signal3, sens.signal4);
        Serial.printf("  Delays: %.1f, %.1f, %.1f, %.1f\n", 
                      sens.delay1, sens.delay2, sens.delay3, sens.delay4);
        Serial.printf("  Toggles: %d, %d, %d, %d\n", 
                      sens.toggle1, sens.toggle2, sens.toggle3, sens.toggle4);
      }

      // Check if at least one sensor is active
      int activeCount = 0;
      for (int i = 0; i < MAX_SENSORS; i++) {
        if (config.sensors[i].active && config.sensors[i].name != "") {
          activeCount++;
        }
      }
      
      if (activeCount == 0) {
        Serial.println("[CONFIG] At least one sensor must remain configured.");
        req->send(400, "text/plain", "At least one sensor must be configured");
        configBodyContent = ""; // Cleanup
        return;
      }

      // Save configuration
      if (config.save()) {
        Serial.printf("[CONFIG] Configuration saved successfully with %d sensors.\n", activeCount);
        req->send(200, "text/plain", "Configuration updated successfully.");
      } else {
        Serial.println("[CONFIG] Failed to save configuration.");
        req->send(500, "text/plain", "Failed to save configuration");
      }
      
      // Cleanup
      configBodyContent = "";
    }
  }
);

  server.on("/saveFlag", HTTP_GET, [](AsyncWebServerRequest *request)
  {
      if (request->hasParam("enabled"))
      {
          bool enabled = request->getParam("enabled")->value() == "true";
          config.cfgInStandardMode = enabled;
          config.save();  // bestehende Funktion zur Speicherung
          request->send(200, "text/plain", "OK");
          Serial.printf("[CONFIG] cfgInStandardMode set to %s\n", enabled ? "true" : "false");
      }
      else
      {
          request->send(400, "text/plain", "Missing parameter");
      }
  });

  server.on("/restart", HTTP_POST, [](AsyncWebServerRequest *request)
  {
      request->send(200, "text/plain", "Restarting...");
      Serial.println("[RESTART] Restarting ESP32 in 100ms...");
      delay(100);
      ESP.restart();
  });

// Server-Handler für die Anzeige im Webinterface
  server.on("/lastsignal", HTTP_GET, [](AsyncWebServerRequest* request)
  {
      AsyncResponseStream* response = request->beginResponseStream("application/json");
      StaticJsonDocument<256> doc;

      doc["sensorAddress"] = lastSignal.sensorAddress;
      doc["sensorData"]    = lastSignal.sensorData;
      doc["binary"]        = lastSignal.binary;

      serializeJson(doc, *response);
      request->send(response);
  });

  if ((isAPMode) || (config.cfgInStandardMode))
  {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
      req->redirect("/config.html");
    });
  }
  else
  {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
      req->redirect("/index.html");
    });
  }

// OTA Update Handler
server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
  // Response nach Upload-Completion
  bool shouldReboot = !Update.hasError();
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", 
    shouldReboot ? "Upload successful! Rebooting..." : "Upload failed!");
  response->addHeader("Connection", "close");
  request->send(response);
  
  if (shouldReboot) {
    Serial.println("[OTA] Update successful, rebooting in 2 seconds...");
    delay(2000);
    ESP.restart();
  }
}, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  // Upload Handler
  if (!index) {
    Serial.printf("[OTA] Starting update: %s\n", filename.c_str());
    
    // Bestimme Update-Typ basierend auf Dateiname
    int cmd = (filename.indexOf("spiffs") > -1) ? U_SPIFFS : U_FLASH;
    
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
      Serial.println("[OTA] Update.begin failed");
      Update.printError(Serial);
      return;
    }
  }
  
  // Schreibe Daten
  if (Update.write(data, len) != len) {
    Serial.println("[OTA] Update.write failed");
    Update.printError(Serial);
    return;
  }
  
  if (final) {
    if (Update.end(true)) {
      Serial.printf("[OTA] Update completed: %u bytes\n", index + len);
    } else {
      Serial.println("[OTA] Update.end failed");
      Update.printError(Serial);
    }
  }
});

  // Progress Handler für Fortschrittsanzeige
  server.on("/update_progress", HTTP_GET, [](AsyncWebServerRequest *request) {
    String json = "{";
    json += "\"progress\":" + String(Update.progress());
    json += ",\"size\":" + String(Update.size());
    json += ",\"hasError\":" + String(Update.hasError() ? "true" : "false");
    if (Update.hasError()) {
      json += ",\"error\":" + String(Update.getError());
    }
    json += "}";
    request->send(200, "application/json", json);
  });


server.on("/uploadweb", HTTP_POST, [](AsyncWebServerRequest* request) {
  request->send(200, "text/plain", "Use POST with multipart/form-data");
}, [](AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
  static File uploadFile;
  static bool uploadSuccess = false;
  
  if (index == 0) {
    // Validate filename
    if (filename != "config.html" && filename != "index.html") {
      request->send(400, "text/plain", "Only config.html and index.html are allowed");
      return;
    }
    
    // Open file for writing
    String path = "/" + filename;
    uploadFile = LittleFS.open(path, "w");
    uploadSuccess = uploadFile ? true : false;
    
    if (!uploadSuccess) {
      request->send(500, "text/plain", "Failed to open file for writing");
      return;
    }
    
    Serial.printf("Uploading: %s\n", filename.c_str());
  }
  
  if (uploadSuccess && uploadFile) {
    uploadFile.write(data, len);
  }
  
  if (final) {
    if (uploadFile) {
      uploadFile.close();
    }
    
    if (uploadSuccess) {
      Serial.printf("Upload completed: %s\n", filename.c_str());
      request->send(200, "text/plain", "File uploaded successfully");
    } else {
      request->send(500, "text/plain", "Upload failed");
    }
  }
});


  server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request){
    if(LittleFS.exists("/index.html")){
      request->send(LittleFS, "/index.html", "text/html");
    } else {
      request->send(404, "text/plain", "File not found");
    }
  });


  server.begin();
  Serial.println("[SETUP] Web server started");

  return; 
}


void setup() 
{
  Serial.begin(115200);
  delay(500);
  Serial.println("***********************************************");
  Serial.println("***  433Mhz Sensor Central for homee V" + FW_VERSION_STR + "  ***");
  Serial.println("***********************************************");

  LED_setup();
  ledOn(); // LED einschalten, um den Start anzuzeigen
  Receiver_setup();

  if (!LittleFS.begin()) {
    Serial.println("[ERROR] Failed to start LittleFS!");
    return;
  }

  if (!config.load())
  {
    config.cfgInSTA = false;
    config.ssid = "YourSSID";
    config.password = "YourPassword";
    config.clientIP = "192.168.0.123";
    config.gatewayIP = "192.168.0.1";
    config.subnet = "255.255.255.0";
    config.sensors[0].active = true;
    config.sensors[0].name = "TestSensor";
    config.sensors[0].homeeID = 1;
    config.sensors[0].type = "OneButton Remote";
    config.sensors[0].address = 0x0;
    config.sensors[0].signal1 = 0;
    config.sensors[0].signal2 = 0;
    config.sensors[0].signal3 = 0;
    config.sensors[0].signal4 = 0;
    config.sensors[0].delay1 = 0;
    config.sensors[0].delay2 = 0;
    config.sensors[0].delay3 = 0;
    config.sensors[0].delay4 = 0;
    config.sensors[0].toggle1 = true;
    config.sensors[0].toggle2 = true;
    config.sensors[0].toggle3 = true;
    config.sensors[0].toggle4 = true;
    config.save();
  }

  Serial.println("hallo");

  startWiFi();

  webserver_setup(); // Webserver setup

  homee_setup(); // Homee setup
  ledOff(); // LED ausschalten, wenn Setup abgeschlossen ist
}


uint32_t lastReceiverCheck = 0;

void loop()
{ 
  static bool firstCall = true;
  if (firstCall)
  {
    Serial.println("Enter control loop for the first time.");
    firstCall = false;
  }

  if (!isAPMode) WiFi_check();

  if ( millis() - lastReceiverCheck >= RECEIVER_CHECK_INTERVAL) // Check receiver every 100ms
  {
//    Receiver_check(!isAPMode); // Check for received signals and update sensor data
    Receiver_check(true); // Check for received signals and update sensor data
    lastReceiverCheck = millis(); // Update the last check time
  }

  yield();  //delay is not allowed here, because homee connection would become unstable
}
