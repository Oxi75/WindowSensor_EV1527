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

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include <LittleFS.h>
#include "ConfigManager.h"
#include <string>
#include "virtualHomee.hpp"
#include "global_defines.h"

#define PIN_SYSMODE       GPIO_NUM_34           // GPIO pin for system mode selection (AP or STA)
#define PIN_LED           GPIO_NUM_2            // GPIO pin for the LED
#define PIN_RECEIVER_PWR  GPIO_NUM_12  	        // GPIO pin for the receiver power (optional, can be used to power the receiver)
#define PIN_RECEIVER      GPIO_NUM_4  	        // GPIO pin for the receiver power (optional, can be used to power the receiver)

const double FW_VERSION = 0.08;
String FW_VERSION_STR = String(FW_VERSION, 2);

#define CANodeProfileOneButtonRemote 20
#define CANodeProfileTwoButtonRemote 24
#define CANodeProfileThreeButtonRemote 25
#define CANodeProfileFourButtonRemote 26

#define CANodeProfileOpenCloseSensor 2000
#define CAAttributeTypeOpenClose 14
#define CAAttributeTypeAlarm 108
#define CAAttributeTypeBatteryLevel 8
#define CAAttributeTypeOnOff 1
#define CAAttributeTypeFirmwareRevision 44
#define CAAttributeTypeBinaryInput 19
#define CAAttributeTypeButtonState 40

#define AttrID_FwRev      0b00100000  //one part of the Attribute ID of homee, the other one is the sensor number
#define AttrID_Sig1       0b01000000  //one part of the Attribute ID of homee, the other one is the sensor number
#define AttrID_Sig2       0b01100000  //one part of the Attribute ID of homee, the other one is the sensor number
#define AttrID_Sig3       0b10000000  //one part of the Attribute ID of homee, the other one is the sensor number
#define AttrID_Sig4       0b10100000  //one part of the Attribute ID of homee, the other one is the sensor number


#define REPEATED_TRANSMISSION_DELAY 250 // Zeit in ms, die zwischen wiederholten Übertragungen gewartet wird

const char* AP_SSID = "vhih";
const char* AP_PASS = "12345678";
const IPAddress AP_IP(192, 168, 42, 1);
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
    uint8_t bitLength;
    String binary;
    String protocol;
};

// Globale Variable mit dem letzten Signal
LastSignal lastSignal = {
    .sensorAddress = "-------",
    .sensorData = "-",
    .bitLength = 0,
    .binary = "------------------------",
    .protocol = "-"
};

   

//WiFi realted functions
AsyncWebServer server(80);

unsigned long lastWifiCheckTime = 0;
const unsigned long wifiCheckInterval = 30000; // Alle 30 Sekunden WLAN prüfen
const unsigned long wifiMaxAttempts = 20; // 
static uint32_t wifiConnectAttempts = 0; // Anzahl der Versuche, sich mit dem WLAN zu verbinden
static bool WiFi_reconnect = false;  //system is in reconnection

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

  if (isAPMode) {
    Serial.println("[BOOT] Starting in AccessPoint mode");
    WiFi.softAPConfig(AP_IP, AP_IP, AP_SUBNET);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.println("[AP] SSID: " + String(AP_SSID));
    Serial.println("[AP] Password: " + String(AP_PASS));
    Serial.println("[AP] IP: " + WiFi.softAPIP().toString());
  } else {
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
    nodeAttributes *na;
    na = new nodeAttributes(CAAttributeTypeFirmwareRevision); 
    na->setName("Firmware Version");
    na->setId(AttrID_FwRev | sns); //unique ID for each sensor
    na->setUnit("");  
    na->setMinimumValue(0.0);
    na->setMaximumValue(1000.0); 
    na->setCurrentValue(FW_VERSION);
    na->setEditable(false);
    na->setCallback(nullptr);

    if (config.RTData[sns].OCSensor)
    {
      n = new node(config.sensors[sns].homeeID, CANodeProfileOpenCloseSensor, config.sensors[sns].name);     
      n->AddAttributes(na);       //set attribute Firmware to node

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
      na->setId(AttrID_Sig3);   //unique ID for each sensor
      na->setUnit("");  
      na->setMinimumValue(0);
      na->setMaximumValue(1); 
      na->setCurrentValue(0);  //default value
      na->setEditable(false);
      na->setCallback(nullptr);
      n->AddAttributes(na);       //set attribute to node

      //Attribut Batterie
      na = new nodeAttributes(CAAttributeTypeBatteryLevel);
      na->setName("Battery Level");
      na->setId(AttrID_Sig4 | sns << 1); //unique ID for each sensor
      na->setUnit("%");
      na->setMinimumValue(0);
      na->setMaximumValue(100); 
      na->setCurrentValue(99);  //default value
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
      n->AddAttributes(na);       //set attribute Firmware to node


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
  Serial.printf("update homee values for Sensor No %d (%s)\n", snsNo, config.sensors[snsNo].type);

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

  Serial.println("[Receiver] setup complete.");
}


// Signal related functions
uint32_t Receiver_getSignal()
{
    Wire.requestFrom(I2C_SLAVE_ADDRESS, 4); // Nur sensorData wird übertragen

    if (Wire.available() >= 4)
    {
        uint32_t sensorData = 0;
        sensorData  = Wire.read();
        sensorData |= Wire.read() << 8;
        sensorData |= Wire.read() << 16;
        sensorData |= Wire.read() << 24;

        return sensorData;
    }

    else return 0x0000000; // Rückgabe eines Dummy-Wertes, wenn nicht genügend Daten verfügbar sind    
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

  uint32_t snsAddr = 0; // Initialisiere den Wert
  uint32_t snsValue = 0;
  uint32_t protocol = 0; // Protokoll initialisieren
  uint32_t now = millis() - 10000; // Zeitstempel des letzten empfangenen Signals (long time ago)

  uint32_t sensorData = Receiver_getSignal(); // Lese die Signaldaten von Arduino
  if ((sensorData != DUMMY_CODE_0) && (sensorData != DUMMY_CODE_F))
  {
    ledOn();        // Switch on the LED to indicate signal reception
    now = millis(); // Aktualisiere den Zeitstempel des letzten empfangenen Signals

    lastSignal.binary = String(sensorData, BIN);

    snsValue = sensorData & 0x000000F; // Extrahiere die unteren 4 Bit für den Signalwert
    lastSignal.sensorData = String(snsValue, DEC); // Extrahiere die unteren 4 Bit für den Signalwert

    snsAddr = sensorData >> 4; // Extrahiere die oberen 20 Bit für die Sensoradresse    
    lastSignal.sensorAddress = String(snsAddr, HEX); // Adresse aus den oberen 20 Bit extrahieren
    lastSignal.sensorAddress.toUpperCase(); // Adresse in Großbuchstaben umwandeln

    Serial.printf("[Receiver] Received signal: Address: 0x%05x, Value: %d, Binary: %s\n", snsAddr, snsValue, lastSignal.binary.c_str());
  }

  for (uint32_t sns = 0; sns < MAX_SENSORS; sns++)  // Durchlaufe alle konfigurierten Sensoren bis Sensor gefunden oder alle Sensoren geprüft wurden
  {
    // Prüfen, ob der Sensor aktiv ist und eine gültige Konfiguration hat
    if ((config.sensors[sns].name == "") || (!config.sensors[sns].active) || (config.sensors[sns].homeeID == 0) || (config.sensors[sns].address == 0))
    {
      // Wenn der Sensor nicht aktiv ist oder keine gültige Konfiguration hat, überspringe ihn
      //Serial.printf("[CONFIG] Sensor %d is not active or has no name or invalid homeeID -> skipping.\n", sns);      
      continue;
    }

    if (config.sensors[sns].address == snsAddr) // Überprüfen, ob die Adresse übereinstimmt 
    {
      // Sensor gefunden
      Serial.printf("Sensor %s has send value %d\n", config.sensors[sns].name.c_str(), snsValue); // Debug-Ausgabe der Adresse und des Wertes

      if (snsValue == config.sensors[sns].signal1)
      {
        if ((now - config.RTData[sns].signal1_TS) > REPEATED_TRANSMISSION_DELAY)
        {
          config.RTData[sns].signal1_val = 1.0; //switch to on / pressed / high state
          config.RTData[sns].signal1 = true;    //signalizes that signal1 hast been changed
        }

        config.RTData[sns].signal1_TS = now;    //update timestamp for signal1
      } 

      if (snsValue == config.sensors[sns].signal2)
      {
        if ((now - config.RTData[sns].signal2_TS) > REPEATED_TRANSMISSION_DELAY)
        {
          if (!config.RTData[sns].OCSensor) 
          {
            config.RTData[sns].signal2_val = 1.0;  //switch to on / pressed / high state
            config.RTData[sns].signal2 = true;     // signalizes that signal2 hast been changed
          }
          else
          {
            config.RTData[sns].signal1_val = 0;  // switch signal1 back to off / released / low state
            config.RTData[sns].signal1 = true;   // signalizes that signal1 hast been changed
          }
        }
        config.RTData[sns].signal2_TS = now;    //update timestamp for signal2
      } 

      if (snsValue == config.sensors[sns].signal3)
      {
        if ((now - config.RTData[sns].signal3_TS) > REPEATED_TRANSMISSION_DELAY)
        {
          config.RTData[sns].signal3_val = 1.0; //switch to on / pressed / high state
          config.RTData[sns].signal3 = true;    // signalizes that signal3 hast been changed
        }
        config.RTData[sns].signal3_TS = now;    //update timestamp for signal3
      }

      if (snsValue == config.sensors[sns].signal4)
      {
        if ((now - config.RTData[sns].signal4_TS) > REPEATED_TRANSMISSION_DELAY)
        {
          if (config.RTData[sns].OCSensor) config.RTData[sns].signal4_val = 10; // signalizes a low battery state
          else config.RTData[sns].signal4_val = 1.0;                            // switch to on / pressed / high state
          config.RTData[sns].signal4 = true;                                    // signalizes that signal4 hast been changed
        }
        config.RTData[sns].signal4_TS = now;    //update timestamp for signal4
      }
    }

    // even if address does not match to the current transmission, the auto-release-feature might require to update the sensor state in homee   
    if ((config.RTData[sns].btnCnt >= 4) && (config.sensors[sns].delay4 > 0.2) && (now - config.RTData[sns].signal4_TS > (config.sensors[sns].delay4 * 1000)) && (config.RTData[sns].signal4_val != 0))
    {
      config.RTData[sns].signal4 = true;            //signal4 must be updated
      if (config.RTData[sns].OCSensor) config.RTData[sns].signal4_val = 66.0;        //set new battery level value which does not trigger a warning
      else config.RTData[sns].signal4_val = 0;       //button 4 is released
    }

    if ((config.RTData[sns].btnCnt >= 3) && (config.sensors[sns].delay3 > 0.2) && (now - config.RTData[sns].signal3_TS > (config.sensors[sns].delay3 * 1000)) && (config.RTData[sns].signal3_val != 0))
    {
      config.RTData[sns].signal3 = true;        //button3 must be updated
      config.RTData[sns].signal3_val = 0;       //button3 is released
    }

    if ((config.RTData[sns].btnCnt >= 2) && !(config.RTData[sns].OCSensor) && (config.sensors[sns].delay2 > 0.2) && (now - config.RTData[sns].signal2_TS > (config.sensors[sns].delay2 * 1000)) && (config.RTData[sns].signal2_val != 0))
    {
      config.RTData[sns].signal2 = true;        //button2 must be updated
      config.RTData[sns].signal2_val = 0;       //button2 is released
    }


    if ((config.RTData[sns].btnCnt >= 1) && !(config.RTData[sns].OCSensor) && (config.sensors[sns].delay1 > 0.2) && (now - config.RTData[sns].signal1_TS > (config.sensors[sns].delay1 * 1000)) && (config.RTData[sns].signal1_val != 0))
    {
      config.RTData[sns].signal1 = true;        //button2 must be updated
      config.RTData[sns].signal1_val = 0;       //button1 is released
    }

    if (config.RTData[sns].signal1 || config.RTData[sns].signal2 || config.RTData[sns].signal3 || config.RTData[sns].signal4)
    {
      Serial.printf("Sensor %s has changed: %d | %d | %d | %d\n", config.sensors[sns].name.c_str(), config.RTData[sns].signal1_val,
                     config.RTData[sns].signal2_val, config.RTData[sns].signal3_val, config.RTData[sns].signal4_val);

      if (HomeeEnabled) homee_updateValues(sns); // Update homee values for the sensor
      config.RTData[sns].signal1 = false; // Reset the signal after processing
      config.RTData[sns].signal2 = false;
      config.RTData[sns].signal3 = false;
      config.RTData[sns].signal4 = false;
    }
  } 

  ledOff();  // Switch off the LED after processing the signal
}



void setup() 
{
  Serial.begin(115200);
  delay(500);
  Serial.println("[SETUP] Starting setup...");

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
    config.save();
  }

  startWiFi();

  server.serveStatic("/config.html", LittleFS, "/config.html");

server.on("/config", HTTP_GET, [](AsyncWebServerRequest* req) {
  DynamicJsonDocument doc(4096);
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
  }

  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
});


  server.on("/config.json", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(LittleFS, CONFIG_FILE, "application/json");
  });

server.on("/config", HTTP_POST, [](AsyncWebServerRequest* req) {}, NULL,
[](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, data, len)) {
    Serial.println("[CONFIG] Failed to parse JSON.");
    req->send(400, "text/plain", "Invalid JSON");
    return;
  }

  // Debug: Print received JSON
  Serial.println("[CONFIG] Received JSON:");
  serializeJsonPretty(doc, Serial);
  Serial.println();

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

  for (int i = 0; i < arr.size() && i < MAX_SENSORS; i++) {
    JsonObject s = arr[i];
    
    // Get and validate name
    const char* namePtr = s["name"].as<const char*>();
    String name;
    if (namePtr) {
      name = String(namePtr);
      name.trim();
    }
    if (!namePtr || name == "") {
      Serial.printf("[WARN] Sensor %d has invalid name, skipping.\n", i);
      continue;
    }

    // Get and validate homeeID
    uint16_t id = s["homeeID"] | 0;
    if (id == 0) {
      Serial.printf("[WARN] Sensor %d has invalid ID, skipping.\n", i);
      continue;
    }

    // Check for duplicate IDs
    bool duplicate = false;
    for (int k = 0; k < valid; k++) {
      if (usedIDs[k] == id) duplicate = true;
    }
    if (duplicate) {
      Serial.printf("[WARN] Duplicate homee-ID (%d), sensor %d skipped.\n", id, i);
      continue;
    }
    usedIDs[valid++] = id;

    // Configure sensor
    auto& sens = config.sensors[i];
    sens.active = s["active"] | true;
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
});


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

  server.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request)
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
      doc["bitLength"]     = lastSignal.bitLength;
      doc["binary"]        = lastSignal.binary;
      doc["protocol"]      = lastSignal.protocol;

      serializeJson(doc, *response);
      request->send(response);
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->redirect("/config.html");
  });

  if (isAPMode) server.begin();
  Serial.println("[SETUP] Web server started");

  homee_setup(); // Homee setup
  ledOff(); // LED ausschalten, wenn Setup abgeschlossen ist
}



void loop()
{
  static bool firstCall = true;
  if (firstCall)
  {
    Serial.println("Enter control loop for the first time.");
    firstCall = false;
  }

  if (!isAPMode) WiFi_check();
  Receiver_check(!isAPMode);
  yield();  //delay is not allowed here, because homee connection would become unstable
}
