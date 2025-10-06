#include <Arduino.h>

// Definisikan pin ZMPT yang kita gunakan (sesuai firmware terakhir)
const int ZMPT_PIN = 35;

void setup() {
  Serial.begin(115200);
  pinMode(ZMPT_PIN, INPUT);
  Serial.println("=========================================");
  Serial.println("   MEMULAI TES PEMBACAAN SENSOR ZMPT   ");
  Serial.println("=========================================");
  Serial.println("Amati angka yang muncul di bawah ini:");
}

void loop() {
  // Baca nilai analog mentah dari pin ZMPT
  int sensorValue = analogRead(ZMPT_PIN);

  // Tampilkan nilainya di Serial Monitor
  Serial.println(sensorValue);

  delay(5); // Jeda singkat agar tidak terlalu cepat
}
