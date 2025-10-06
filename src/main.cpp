#include <Arduino.h>
#include <WiFi.h>

const char* ssid = "MOTsmart_Welder_TEST";

void setup() {
  Serial.begin(115200);
  delay(1000); // Give 1 second for Serial Monitor to prepare
  Serial.println("\n\n--- Starting Minimal WiFi Test on BARE ESP32 ---");

  // Initialize WiFi in AP mode
  bool wifiStarted = WiFi.softAP(ssid);

  if (wifiStarted) {
    Serial.println("--- Access Point Should Be Active Now ---");
    Serial.print("WiFi SSID: ");
    Serial.println(ssid);
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("!!! FAILED TO START WIFI ACCESS POINT !!!");
  }
  Serial.println("===============================");
}

void loop() {
  // Empty loop
}
