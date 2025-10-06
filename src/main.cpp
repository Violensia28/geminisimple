#include <Arduino.h>
#include <WiFi.h>

const char* ssid = "MOTsmart_Welder_TEST";

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\nMemulai Tes WiFi Minimalis...");

  WiFi.softAP(ssid);

  Serial.println("Access Point Seharusnya Aktif Sekarang.");
  Serial.print("Nama WiFi (SSID): ");
  Serial.println(ssid);
  Serial.print("Alamat IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("===============================");
}

void loop() {
  // Biarkan kosong
}
