#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <RCSwitch.h>
#include <time.h>
#include <ArduinoJson.h>

#define INPUT_PIN GPIO_NUM_15

RCSwitch mySwitch = RCSwitch();
AsyncWebServer server(80);

// Datenstruktur für den letzten Empfang
String lastData = "{}";

// AP-Konfiguration
const char* ssid = "433MHz";
const char* password = "12345678";
IPAddress local_IP(192, 168, 42, 1);
IPAddress gateway(192, 168, 42, 1);
IPAddress subnet(255, 255, 255, 0);


unsigned long lastSignalTime = 0;





void setup() {
  Serial.begin(115200);
  delay(500);

  // Zeitquelle (nur für korrekten Timestamp)
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  // Start AP
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(ssid, password);
  Serial.println("Access Point gestartet: 433MHz");

  // RCSwitch aktivieren
  mySwitch.enableReceive(digitalPinToInterrupt(INPUT_PIN));

  // Routen
server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, lastData);

  unsigned long secondsAgo = (millis() - lastSignalTime) / 1000;

  String html = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <meta charset="utf-8">
    <meta http-equiv="refresh" content="2">
    <title>433MHz Monitor</title>
    <style>
      body { font-family: Arial, sans-serif; background-color: #f4f4f4; padding: 20px; }
      .container { background: #fff; padding: 20px; border-radius: 10px; max-width: 550px; margin: auto; box-shadow: 0 0 10px rgba(0,0,0,0.1); }
      h2 { text-align: center; margin-bottom: 5px; }
      label { font-weight: bold; display: block; margin-top: 15px; }
      .row { display: flex; gap: 5px; }
      input[type="text"] {
        flex: 1;
        padding: 8px;
        margin-top: 5px;
        font-family: monospace;
        font-size: 1em;
      }
      button.copyBtn {
        padding: 8px 10px;
        margin-top: 5px;
        cursor: pointer;
      }
      .timestamp {
        text-align: center;
        font-size: 1.1em;
        color: #555;
        margin-bottom: 20px;
      }
    </style>
    <script>
      function copyToClipboard(id) {
        var copyText = document.getElementById(id);
        copyText.select();
        copyText.setSelectionRange(0, 99999);
        navigator.clipboard.writeText(copyText.value);
      }
    </script>
  </head>
  <body>
    <div class="container">
      <h2>Letztes Signal</h2>
      <div class="timestamp">)rawliteral" + String(secondsAgo) + R"rawliteral( Sekunden seit letztem Empfang</div>

      <label>Dezimalwert:</label>
      <div class="row">
        <input type="text" readonly id="decimal" value=")rawliteral" + doc["decimal"].as<String>() + R"rawliteral(">
        <button class="copyBtn" onclick="copyToClipboard('decimal')">Kopieren</button>
      </div>

      <label>Binärwert:</label>
      <input type="text" readonly value=")rawliteral" + doc["binary"].as<String>() + R"rawliteral(">

      <label>Bitlänge:</label>
      <input type="text" readonly value=")rawliteral" + doc["bitLength"].as<String>() + R"rawliteral(">

      <label>Sensoradresse:</label>
      <div class="row">
        <input type="text" readonly id="sensorAddress" value=")rawliteral" + doc["sensorAddress"].as<String>() + R"rawliteral(">
        <button class="copyBtn" onclick="copyToClipboard('sensorAddress')">Kopieren</button>
      </div>

      <label>Sensordaten:</label>
      <div class="row">
        <input type="text" readonly id="sensorData" value=")rawliteral" + doc["sensorData"].as<String>() + R"rawliteral(">
        <button class="copyBtn" onclick="copyToClipboard('sensorData')">Kopieren</button>
      </div>

      <label>Protokoll:</label>
      <input type="text" readonly value=")rawliteral" + doc["protocol"].as<String>() + R"rawliteral(">
    </div>
  </body>
  </html>
  )rawliteral";

  request->send(200, "text/html", html);
});

  server.on("/data", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "application/json", lastData);
  });

  server.begin();
}

void loop() {
  if (mySwitch.available()) {
    lastSignalTime = millis();
    uint32_t code = mySwitch.getReceivedValue();
    uint32_t bitLength = mySwitch.getReceivedBitlength();
    uint32_t protocol = mySwitch.getReceivedProtocol();

    uint32_t sensorAddress = code >> 4;
    uint32_t sensorData = code & 0x0F;

    // Daten aktualisieren
    lastData = "{";
    lastData += "\"decimal\":" + String(code) + ",";
    lastData += "\"binary\":\"" + String(code, BIN) + "\",";
    lastData += "\"bitLength\":" + String(bitLength) + ",";
    lastData += "\"sensorAddress\":" + String(sensorAddress) + ",";
    lastData += "\"sensorData\":" + String(sensorData) + ",";
    lastData += "\"protocol\":" + String(protocol) + ",";
    lastData += "}";

    Serial.println("Empfangen: " + lastData);

    mySwitch.resetAvailable();
  }
}
