// GANTI FUNGSI SETUP LAMA ANDA DENGAN YANG INI
void setup() {
    Serial.begin(115200);
    delay(1000); // Beri jeda 1 detik agar Serial Monitor siap
    Serial.println("\n\nBooting MOTsmart Welder...");

    Serial.println("Checkpoint 1: Initializing Pins...");
    pinMode(SSR_PIN, OUTPUT);
    digitalWrite(SSR_PIN, LOW);
    pinMode(MACROSWITCH_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(MACROSWITCH_PIN), macroswitch_ISR, FALLING);
    Serial.println("Checkpoint 2: Pin Initialization Complete.");

    Serial.println("Checkpoint 3: Calibrating Sensors...");
    calibrateSensors();
    Serial.println("Checkpoint 4: Sensor Calibration Complete.");

    Serial.println("Checkpoint 5: Setting up EmonLib...");
    emon.voltage(ZMPT_PIN, 230.0, 1.7);
    emon.current(ACS712_PIN, 30.0);
    Serial.println("Checkpoint 6: EmonLib Setup Complete.");

    Serial.println("Checkpoint 7: Starting WiFi AP...");
    WiFi.softAP(ssid);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);
    Serial.println("Checkpoint 8: WiFi AP Started Successfully!");

    ws.onEvent(onEvent);
    server.addHandler(&ws);
    server.on("/", HTTP_get, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html);
    });

    ElegantOTA.begin(&server);

    server.begin();
    Serial.println("Server started. OTA on /update");
    Serial.println("===================================");
    Serial.println("BOOTING COMPLETE. SYSTEM READY.");
    Serial.println("===================================");
}
