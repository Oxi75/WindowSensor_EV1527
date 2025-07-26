#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include "ConfigManager.h"

#define GPIO_MODE_SELECT 23

const char* AP_SSID = "vhih";
const char* AP_PASS = "12345678";
const IPAddress AP_IP(192, 168, 42, 1);
const IPAddress AP_SUBNET(255, 255, 255, 0);

bool isAPMode = false;

ConfigManager config;
AsyncWebServer server(80);

void startWiFi() {
  pinMode(GPIO_MODE_SELECT, INPUT_PULLUP);
  isAPMode = digitalRead(GPIO_MODE_SELECT) == LOW;

  if (isAPMode) {
    Serial.println("[BOOT] AP-Modus aktiviert");
    WiFi.softAPConfig(AP_IP, AP_IP, AP_SUBNET);
    WiFi.softAP(AP_SSID, AP_PASS);
  } else {
    Serial.println("[BOOT] Standard-Modus (STA)");
    // STA-Modus – Konfigdaten folgen später
    WiFi.begin("SSID", "PASS"); // Dummy, wird ersetzt
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("[SETUP] Starte Setup...");

  if (!LittleFS.begin()) {
    Serial.println("[ERROR] LittleFS konnte nicht gestartet werden!");
    return;
  }

  if (!config.load()) config.save(); // bei Erststart speichern

  startWiFi();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "text/plain", "Webinterface kommt gleich...");
  });

  server.begin();
  Serial.println("[SETUP] Webserver gestartet");
}

void loop() {}
