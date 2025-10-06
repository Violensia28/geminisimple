#include <Arduino.h>
#include <WiFi.h>

const char* ssid = "MOTsmart_Welder_TEST";

void setup() {
  Serial.begin(115200);
  delay(1000); // Beri jeda 1 detik agar Serial Monitor siap
  Serial.println("\n\n--- Memulai Tes WiFi Minimalis ---");

  WiFi.softAP(ssid);

  Serial.println("--- Access Point Seharusnya Aktif Sekarang ---");
  Serial.print("Nama WiFi (SSID): ");
  Serial.println(ssid);
  Serial.print("Alamat IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("===============================");
}

void loop() {
  // Biarkan kosong
}
