#include <Arduino.h>

const int LED_PIN = 2; // LED internal pada sebagian besar ESP32 Dev Kit

void setup() {
  Serial.begin(115200);
  delay(1000); // Beri jeda 1 detik agar Serial Monitor siap
  Serial.println("\n\n--- Hello World Test Started ---");
  Serial.println("Attempting to blink internal LED...");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // Nyalakan LED

  Serial.println("Internal LED should be ON now.");
  Serial.println("If you see this text, serial communication and basic code execution is working.");
  Serial.println("===============================");
}

void loop() {
  // Hanya biar loop tetap berjalan, bisa juga tambahkan blink di sini
  // digitalWrite(LED_PIN, !digitalRead(LED_PIN)); // Toggle LED
  // delay(1000);
}
