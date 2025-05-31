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
#include <LittleFS.h>
#include "ConfigManager.h"
#include <string>
#include "virtualHomee.hpp"
#include <RCSwitch.h>


#define PIN_SYSMODE  GPIO_NUM_34           // GPIO pin for system mode selection (AP or STA)
#define PIN_LED      GPIO_NUM_2            // GPIO pin for the LED
#define PIN_RECEIVER GPIO_NUM_15

const double FW_VERSION = 0.04;
String FW_VERSION_STR = String(FW_VERSION, 2);

#define CANodeProfileOneButtonRemote 20
#define CANodeProfileOpenCloseSensor 2000
#define CAAttributeTypeOpenClose 14
#define CAAttributeTypeAlarm 108
#define CAAttributeTypeBatteryLevel 8
#define CAAttributeTypeOnOff 1
#define CAAttributeTypeFirmwareRevision 44
#define CAAttributeTypeBinaryInput 19
#define CAAttributeTypeButtonState 40
#define AttrID_FwRev      0b00000000
#define AttrID_State      0b01000000
#define AttrID_Alarm      0b10000000
#define AttrID_BattLevel  0b11000000
#define SensorSignalOpen  0x0000
#define SensorSignalClosed 0x0001
#define SensorSignalAlarm 0x0002
#define SensorSignalBattery 0x0003



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
      Serial.printf("[CONFIG] Sensor %d is not active or has no name or invalid homeeID -> skipping.\n", sns);      
      continue;
    }

    if ((config.sensors[sns].type != "Window-Sensor") && (config.sensors[sns].type != "Button"))
    {
      Serial.printf("[CONFIG] Sensor %d has an invalid type '%s' -> skipping.\n", sns, config.sensors[sns].type.c_str());
      continue;
    }

    Serial.printf("setup sensor %s with address 0x%05x as %s as homee device with ID %d\n", config.sensors[sns].name.c_str(), config.sensors[sns].address, config.sensors[sns].type.c_str(), config.sensors[sns].homeeID);

    node *n;
    nodeAttributes *na;

    //Neues Gerät
    if (config.sensors[sns].type == "Window-Sensor")
    {

      //A Window-Sensor needs at least a value attribute for the open/closed state
      //additionally there might be an alaram and a battery attribute

      n = new node(config.sensors[sns].homeeID, CANodeProfileOpenCloseSensor, config.sensors[sns].name);     

      na = new nodeAttributes(CAAttributeTypeFirmwareRevision); 
      na->setName("Firmware Version");
      na->setId(AttrID_FwRev | sns << 1); //unique ID for each sensor
      na->setUnit("");  
      na->setMinimumValue(0);
      na->setMaximumValue(100); 
      na->setCurrentValue(FW_VERSION);
      na->setEditable(false);
      na->setCallback(nullptr);
      n->AddAttributes(na);       //set attribute to node


      //Attribut Status
      na = new nodeAttributes(CAAttributeTypeOpenClose);  //open / closed state
      na->setName("Status");
      na->setId(AttrID_State | sns << 1); //unique ID for each sensor        
      na->setMinimumValue(0);
      na->setMaximumValue(1); 
      na->setCurrentValue(0);
      na->setCallback(nullptr);
      na->setEditable(false);
      n->AddAttributes(na);       //set attribute to node

      if(config.sensors[sns].signalAlarm > 0)
      {
        //Attribut Alarm
        na = new nodeAttributes(CAAttributeTypeAlarm);  //CAAttributeTypeAlarm
        na->setName("Alarm");
        na->setId(AttrID_Alarm | sns << 1); //unique ID for each sensor
        na->setUnit("");  
        na->setMinimumValue(0);
        na->setMaximumValue(1); 
        na->setCurrentValue(0);  //default value
        na->setEditable(false);
        na->setCallback(nullptr);
        n->AddAttributes(na);       //set attribute to node
      }

      if(config.sensors[sns].signalBattery > 0)
      {
        //Attribut Batterie
        na = new nodeAttributes(CAAttributeTypeBatteryLevel);
        na->setName("Battery Level");
        na->setId(AttrID_BattLevel | sns << 1); //unique ID for each sensor
        na->setUnit("%");
        na->setMinimumValue(0);
        na->setMaximumValue(100); 
        na->setCurrentValue(100);  //default value
        na->setEditable(false);
        na->setCallback(nullptr);
        n->AddAttributes(na);       //set attribute to node
      }
    }
    else if (config.sensors[sns].type == "Button")
    {
      //A Push-Button needs only the value attribute for the pushed state
      n = new node(config.sensors[sns].homeeID, CANodeProfileOneButtonRemote, config.sensors[sns].name);

      na = new nodeAttributes(CAAttributeTypeFirmwareRevision); 
      na->setName("Firmware Version");
      na->setId(AttrID_FwRev | sns << 1); //unique ID for each sensor
      na->setUnit("");  
      na->setMinimumValue(0);
      na->setMaximumValue(100); 
      na->setCurrentValue(FW_VERSION);
      na->setEditable(false);
      na->setCallback(nullptr);
      n->AddAttributes(na);       //set attribute to node

      //Attribut Status
      na = new nodeAttributes(CAAttributeTypeButtonState);  //open / closed state
      na->setName("Status");
      na->setId(AttrID_State | sns << 1); //unique ID for each sensor        
      na->setUnit("");  
      na->setMinimumValue(0);
      na->setMaximumValue(1); 
      na->setCurrentValue(0);
      na->setCallback(nullptr);
      na->setEditable(false);
      n->AddAttributes(na);       //set attribute to node  
    }

    //Gerät hinzufügen
    vhih.addNode(n);
  }

  vhih.start();

  Serial.println("Homee configured");
  delay(1000);  
}


static double lastValue = 1;
void homee_updateValues(uint32_t snsNo, uint32_t value)
{  
  Serial.print("update homee values for sensor " + String(snsNo) + " with attribute ");

  // Update Sensor or Button-State
  nodeAttributes *na;
  if (config.sensors[snsNo].type == "Window-Sensor")
  {
    if (value == SensorSignalOpen) 
    {
      Serial.println("open");
      na = vhih.getAttributeById(AttrID_State | snsNo << 1);
      na->setCurrentValue(1);
    } 
    else if (value == SensorSignalClosed) 
    {
      Serial.println("closed");
      na = vhih.getAttributeById(AttrID_State | snsNo << 1);
      na->setCurrentValue(0);
    } 
    else if (value == SensorSignalBattery) 
    {
      Serial.println("battery warning");
      na = vhih.getAttributeById(AttrID_BattLevel | snsNo << 1);
      na->setCurrentValue(1);
    } 
  }
  else if (config.sensors[snsNo].type == "Button")
  {
    Serial.println("pushed");
    // For Push-Button, 0 = pushed
    na = vhih.getAttributeById(AttrID_State | snsNo << 1);
    
    if (lastValue == 0) lastValue = 1;
    else lastValue = 0;
    na->setCurrentValue(lastValue);
  }


  if (na) vhih.updateAttributeValue(na, na->getCurrentValue());
  delay(200);
  yield();
} 


/*
void homee_updateValues(uint32_t snsNo, double snsStatus, double alarmStatus, double batteryLevel)
{  
  nodeAttributes *na;
  Serial.print("update homee values for sensor " + String(snsNo) + " with attributes ");

  // Update Sensor or Button-State
  na = vhih.getAttributeById(AttrID_State | snsNo << 1);
  if (na)
  {
    vhih.updateAttributeValue(na, snsStatus );
    delay(200);
    yield();
    Serial.print("Status: " + String(snsStatus) + ", ");
  } 

  // Update Alarm-State
  na = vhih.getAttributeById(AttrID_Alarm | snsNo << 1);
  if (na)
  {
    vhih.updateAttributeValue(na, alarmStatus);
    delay(200);
    yield();
    Serial.print("Alarm: " + String(alarmStatus) + ", ");
  } 

  //Update Battery-Level
  na = vhih.getAttributeById(AttrID_BattLevel | snsNo << 1);
  if (na)
  {
    vhih.updateAttributeValue(na, batteryLevel);  
    delay(200);
    yield();
    Serial.print("BatteryLevel: " + String(batteryLevel));
  }

  Serial.println();
  Serial.println("homee Update done");
}
*/

// RCSwitch related functions
RCSwitch mySwitch = RCSwitch();

void RCSwitch_setup()
{
    mySwitch.enableReceive(digitalPinToInterrupt(PIN_RECEIVER)); // Empfänger-Pin setzen
    Serial.println("[RCSWITCH] RCSwitch setup complete.");
}


bool RCSwitch_check()
{
  static uint32_t lastValue = 0; // Letzter empfangener Wert
  static uint32_t lastValueTime = 0; // Zeitstempel des letzten empfangenen Werts

  uint32_t currentTime = millis();

  if (!mySwitch.available()) return false; // Keine Daten verfügbar

  unsigned long value = mySwitch.getReceivedValue();    
  mySwitch.resetAvailable(); // Verfügbare Daten zurücksetzen
  if (value == 0) return false; // Kein gültiger Wert empfangen

  if (value == lastValue && (currentTime - lastValueTime) < 500)
  {
    lastValueTime = currentTime; // Zeitstempel aktualisieren, wenn der Wert gleich bleibt
    return false;                // Wert ist gleich dem letzten, also ignorieren
  }

  lastValue = value;           // Neuen Wert speichern
  lastValueTime = currentTime; // Zeitstempel aktualisieren

  bool sensorFound = false;

  //Daten auswerten um dem Sensor zuzuordnen
  for (uint32_t sns = 0; sns < MAX_SENSORS; sns++)  // Durchlaufe alle konfigurierten Sensoren bis Sensor gefunden oder alle Sensoren geprüft wurden
  {
    if ((config.sensors[sns].name == "") || (!config.sensors[sns].active) || (config.sensors[sns].homeeID == 0))
    {
      //Serial.printf("[CONFIG] Sensor %d is not active or has no name or invalid homeeID -> skipping.\n", sns);      
      continue;
    }

    if (config.sensors[sns].type == "Window-Sensor")
    {
      uint32_t addr = (value >> 4) & 0xFFFFF; // Adresse aus den oberen 20 Bit extrahieren

      if (addr == config.sensors[sns].address) // Überprüfen, ob die Adresse übereinstimmt
      {
        sensorFound = true; // Sensor gefunden
        Serial.printf("Sensor %s has send value %d\n", String(addr), value & 0x000F); // Ausgabe der Adresse und des Wertes


        if ((value & 0x000F) == config.sensors[sns].signalOn)      homee_updateValues(sns, SensorSignalOpen);  // Sensor ist offen
        if ((value & 0x000F) == config.sensors[sns].signalOff)     homee_updateValues(sns, SensorSignalClosed);  // Sensor ist geschlossen
        if ((value & 0x000F) == config.sensors[sns].signalAlarm)   homee_updateValues(sns, SensorSignalAlarm);  // Sensor hat Alarm ausgelöst
        if ((value & 0x000F) == config.sensors[sns].signalBattery) homee_updateValues(sns, SensorSignalBattery);  // Sensor hat Batteriewarnung ausgelöst
        break; // Sensor gefunden -> For-Schleife beenden
      }

      // Wenn die Adresse nicht übereinstimmt mit nächstem Schleifendurchlauf fortfahren
      continue;
    }

    if (config.sensors[sns].type == "Button")
    {
      if (value == config.sensors[sns].address) // Überprüfen, ob die Adresse übereinstimmt
      {
        sensorFound = true; // Sensor gefunden
        Serial.printf("Button %d was pushed\n", value); // Ausgabe der Adresse und des Wertes
        homee_updateValues(sns, 0);
        break; // Sensor gefunden -> For-Schleife beenden
      }

      // Wenn die Adresse nicht übereinstimmt mit nächstem Schleifendurchlauf fortfahren
      continue;
    }

    //unknown sensor type
    Serial.printf("RCSwitch_check() - Sensor %d, configured type (%s) is unknown. Check configuration or program code\n", sns, config.sensors[sns].type.c_str()); 
  } // Ende der For-Schleife

  if (!sensorFound)
  {
    Serial.printf("Transmission from unknown sensor received. Received value is %05x\n", value);      
  }


  return sensorFound;
}


void setup() 
{
  Serial.begin(115200);
  delay(500);
  Serial.println("[SETUP] Starting setup...");

  LED_setup();
  ledOn(); // LED einschalten, um den Start anzuzeigen

  RCSwitch_setup(); // RCSwitch initialisieren 

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
    config.sensors[0].type = "Window-Sensor";
    config.sensors[0].address = 1234;
    config.sensors[0].signalOn = 1001;
    config.sensors[0].signalOff = 1002;
    config.sensors[0].signalAlarm = 1003;
    config.sensors[0].signalBattery = 1004;
    config.save();
  }

  startWiFi();

  server.serveStatic("/config.html", LittleFS, "/config.html");

  server.on("/config", HTTP_GET, [](AsyncWebServerRequest* req) {
    DynamicJsonDocument doc(2048);
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
      s["address"] = config.sensors[i].address;
      s["signalOn"] = config.sensors[i].signalOn;
      s["signalOff"] = config.sensors[i].signalOff;
      s["signalAlarm"] = config.sensors[i].signalAlarm;
      s["signalBattery"] = config.sensors[i].signalBattery;
    }

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/config.json", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(LittleFS, CONFIG_FILE, "application/json");
  });

  server.on("/config.json", HTTP_PUT, [](AsyncWebServerRequest* req) {}, NULL,
  [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    File f = LittleFS.open(CONFIG_FILE, "w");
    if (!f) {
      Serial.println("[UPLOAD] Failed to open for writing.");
      req->send(500, "text/plain", "Failed to open file.");
      return;
    }
    f.write(data, len);
    f.close();
    config.load();
    Serial.println("[UPLOAD] config.json uploaded and reloaded.");
    req->send(200, "text/plain", "OK");
  });

  server.on("/config", HTTP_POST, [](AsyncWebServerRequest* req) {}, NULL,
  [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, data, len)) {
      Serial.println("[CONFIG] Failed to parse JSON.");
      return;
    }

    config.cfgInSTA = doc["system"]["cfgInSTA"];
    config.ssid = doc["wifi"]["ssid"].as<String>();
    config.password = doc["wifi"]["pw"].as<String>();
    config.clientIP = doc["wifi"]["ip"].as<String>();
    config.gatewayIP = doc["wifi"]["gw"].as<String>();
    config.subnet = doc["wifi"]["mask"].as<String>();

    JsonArray arr = doc["sensors"].as<JsonArray>();
    uint16_t usedIDs[32] = {0};
    int valid = 0;

    for (int i = 0; i < arr.size() && i < MAX_SENSORS; i++) {
      JsonObject s = arr[i];
      const char* namePtr = s["name"].as<const char*>();
      String name;

      if (namePtr) {
        name = String(namePtr);
        name.trim();
      }

      if (!namePtr || (name == "")) {
        Serial.printf("[WARN] Sensor %d has invalid name, skipping.\n", i);
        continue;
      }

      uint16_t id = s["homeeID"] | 0;
      if (id == 0) {
        Serial.printf("[WARN] Sensor %d has invalid ID, skipping.\n", i);
        continue;
      }

      bool duplicate = false;
      for (int k = 0; k < valid; k++) {
        if (usedIDs[k] == id) duplicate = true;
      }
      if (duplicate) {
        Serial.printf("[WARN] Duplicate homee-ID (%d), sensor %d skipped.\n", id, i);
        continue;
      }
      usedIDs[valid++] = id;

      auto& sens = config.sensors[i];
      sens.name = name;
      sens.homeeID = id;
      sens.type = s["type"].as<String>();
      sens.address = s["address"] | 0;
      sens.signalOn = s["signalOn"] | 0;
      sens.signalOff = s["signalOff"] | 0;
      sens.signalAlarm = s["signalAlarm"] | 0;
      sens.signalBattery = s["signalBattery"] | 0;
      sens.active = true;
    }

    for (int i = arr.size(); i < MAX_SENSORS; i++) {
      config.sensors[i].name = "";
      config.sensors[i].active = false;
    }

    config.save();
    Serial.println("[CONFIG] Configuration saved.");
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->redirect("/config.html");
  });

  server.begin();
  Serial.println("[SETUP] Web server started");

  homee_setup(); // Homee setup
  ledOff(); // LED ausschalten, wenn Setup abgeschlossen ist
}

static bool attrChanged = false;
static uint32_t attrSensorNo = 0;
static uint32_t attrStatus = 0;
static uint32_t attrAlarm = 0;
static uint32_t attrBattery = 0;


void loop()
{
    static bool firstCall = true;
    if (firstCall)
    {
      Serial.println("Enter control loop for the first time.");
      firstCall = false;
    }

    if (isAPMode)
    {

    }
    else
    {
      WiFi_check();
      RCSwitch_check();
      yield();  //delay is not allowed here, because homee connection would become unstable
    }
}
