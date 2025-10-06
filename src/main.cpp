/**
 * @file main.cpp
 * @author Gemini AI Programmer
 * @brief FINAL Firmware for MOTsmart Welder with all features and robust diagnostics.
 * @version 3.0 (Final - Corrected Pins & Full Debug)
 * @date 2025-10-06
 *
 * @copyright Copyright (c) 2025
 *
 * Project: MOTsmart SimpleWeld
 * Director: User
 * Lead Programmer: Gemini AI
 *
 * Features:
 * - ZMPT Auto-Calibration on startup (debug messages included).
 * - ACS712 Auto-Calibration on startup (debug messages included).
 * - OTA (Over-the-Air) Updates via /update endpoint.
 * - Auto Spot trigger based on Vrms and Irms.
 * - Dual Pulse & Single Pulse Modes.
 *
 * Pinout (Standard ESP32 Dev Board):
 * - SSR Control -> GPIO 26
 * - ZMPT101B (Voltage Zero Cross) -> GPIO 35 (ADC1_CH7)
 * - ACS712 (Current Sense) -> GPIO 34 (ADC1_CH6)
 * - Macroswitch -> GPIO 18 (Input_Pullup, Interrupt)
 */

// -----------------------------------------------------------------------------
// 1. LIBRARY INCLUDES
// -----------------------------------------------------------------------------
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include "EmonLib.h"
#include <ElegantOTA.h>

// -----------------------------------------------------------------------------
// 2. HARDWARE PIN DEFINITIONS
// -----------------------------------------------------------------------------
const int SSR_PIN = 26;
const int ZMPT_PIN = 35; // ADC1_CH7, pastikan pin ini tidak digunakan untuk hal lain
const int ACS712_PIN = 34; // ADC1_CH6, pastikan pin ini tidak digunakan untuk hal lain
const int MACROSWITCH_PIN = 18;

// -----------------------------------------------------------------------------
// 3. GLOBAL CONFIGURATION & VARIABLES
// -----------------------------------------------------------------------------
const char* ssid = "MOTsmart_Welder";
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
EnergyMonitor emon;

// Sensor calibration variables
int zmptMidpoint = 2048; // Default value, will be updated by calibration
int acsOffset = 2048;    // Default value, will be updated by calibration

struct WeldSettings {
  String mode = "double";
  int pre_pulse_ms = 20;
  int gap_ms = 40;
  int main_pulse_ms = 120;
};
WeldSettings settings;

struct AutoSpotSettings {
  bool enabled = false;
  float trigThresh_A = 0.8;
  float vCutoff_V = 210.0;
  float iLimit_A = 35.0; // Overcurrent limit
};
AutoSpotSettings autoSpot;

volatile bool triggerWeld = false;
volatile bool isWelding = false;

volatile unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

// -----------------------------------------------------------------------------
// 4. WEB INTERFACE (HTML, CSS, JAVASCRIPT)
// -----------------------------------------------------------------------------
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>MOTsmart Welder Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    :root { --accent-color: #00bcd4; --bg-color: #1e1e1e; --text-color: #e0e0e0; --card-color: #333; }
    html { font-family: Arial, Helvetica, sans-serif; display: inline-block; text-align: center; }
    body { max-width: 450px; margin: 0px auto; padding-bottom: 25px; background-color: var(--bg-color); color: var(--text-color); }
    h1 { color: var(--accent-color); }
    a { color: var(--accent-color); }
    .card { background-color: var(--card-color); padding: 15px; border-radius: 8px; margin-top: 20px; }
    .slider-container { margin: 15px 0; }
    .hidden { display: none; }
    label { display: block; margin-bottom: 5px; font-weight: bold; }
    input[type=range] { width: 80%; }
    input[type=number] { width: 80px; background-color: #555; color: white; border: 1px solid #777; border-radius: 4px; padding: 5px; }
    .button { border: none; color: white; padding: 16px 32px; font-size: 24px; margin: 10px 2px; cursor: pointer; border-radius: 8px; width: 90%; }
    #spot-btn { background-color: var(--accent-color); }
    .mode-selector { display: flex; justify-content: space-around; margin-bottom: 20px; }
    .mode-selector label { border: 1px solid #555; padding: 10px; border-radius: 5px; width: 45%; }
    .mode-selector input[type="radio"] { display: none; }
    .mode-selector input[type="radio"]:checked + label { background-color: var(--accent-color); border-color: var(--accent-color); }
    .toggle-switch { position: relative; display: inline-block; width: 60px; height: 34px; }
    .toggle-switch input { opacity: 0; width: 0; height: 0; }
    .toggle-slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .4s; border-radius: 34px; }
    .toggle-slider:before { position: absolute; content: ""; height: 26px; width: 26px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }
    input:checked + .toggle-slider { background-color: var(--accent-color); }
    input:checked + .toggle-slider:before { transform: translateX(26px); }
  </style>
</head>
<body>
  <h1>MOTsmart Welder v3.0</h1>
  <div class="card">
    <h2>Auto Spot</h2>
    <label class="toggle-switch">
      <input type="checkbox" id="autospot-enabled" onchange="sendAutoSpotSettings()">
      <span class="toggle-slider"></span>
    </label>
    <div id="autospot-settings" class="hidden" style="margin-top: 15px;">
      <label>Trigger Current: <input type="number" id="trig-thresh" step="0.1" value="0.8" onchange="sendAutoSpotSettings()"> A</label>
      <label style="margin-top: 10px;">Voltage Cutoff: <input type="number" id="v-cutoff" step="1" value="210" onchange="sendAutoSpotSettings()"> V</label>
    </div>
  </div>
  <div class="card">
    <h2>Mode</h2>
    <div class="mode-selector">
      <input type="radio" id="mode-double" name="weld-mode" value="double" onchange="toggleMode()" checked>
      <label for="mode-double">Dual</label>
      <input type="radio" id="mode-single" name="weld-mode" value="single" onchange="toggleMode()">
      <label for="mode-single">Single</label>
    </div>
    <div id="pre-pulse-container" class="slider-container">
      <label for="pre-pulse-slider">Pre-Pulse: <span id="pre-pulse-val">20</span> ms</label>
      <input type="range" min="0" max="100" value="20" id="pre-pulse-slider" oninput="updateSliderVal('pre-pulse')" onchange="sendWeldSettings()">
    </div>
    <div id="gap-container" class="slider-container">
      <label for="gap-slider">Gap: <span id="gap-val">40</span> ms</label>
      <input type="range" min="10" max="200" value="40" id="gap-slider" oninput="updateSliderVal('gap')" onchange="sendWeldSettings()">
    </div>
    <div class="slider-container">
      <label id="main-label" for="main-pulse-slider">Main Pulse: <span id="main-pulse-val">120</span> ms</label>
      <input type="range" min="20" max="500" value="120" id="main-pulse-slider" oninput="updateSliderVal('main-pulse')" onchange="sendWeldSettings()">
    </div>
  </div>
  <button id="spot-btn" class="button">SPOT</button>
  <div class="card">
      <a href="/update">Firmware Update</a>
  </div>
<script>
  let websocket;
  function initWebSocket() {
    websocket = new WebSocket(`ws://${window.location.hostname}/ws`);
    websocket.onopen = (event) => { console.log('Connected'); };
    websocket.onclose = (event) => { setTimeout(initWebSocket, 2000); };
    websocket.onmessage = (event) => {
      const data = JSON.parse(event.data);
      if (data.status) {
          const spotBtn = document.getElementById('spot-btn');
          spotBtn.innerText = data.status;
          if(data.status !== "READY"){ spotBtn.style.backgroundColor = '#f1c40f'; } 
          else { spotBtn.style.backgroundColor = 'var(--accent-color)'; }
      }
    };
  }
  function updateSliderVal(id) {
    const slider = document.getElementById(id + '-slider');
    const valSpan = document.getElementById(id + '-val');
    valSpan.innerText = slider.value + ' ms';
  }
  function toggleMode() {
    const mode = document.querySelector('input[name="weld-mode"]:checked').value;
    document.getElementById('pre-pulse-container').classList.toggle('hidden', mode !== 'double');
    document.getElementById('gap-container').classList.toggle('hidden', mode !== 'double');
    sendWeldSettings();
  }
  function sendWeldSettings() {
    websocket.send(JSON.stringify({
      action: 'update_weld_settings',
      mode: document.querySelector('input[name="weld-mode"]:checked').value,
      pre: parseInt(document.getElementById('pre-pulse-slider').value),
      gap: parseInt(document.getElementById('gap-slider').value),
      main: parseInt(document.getElementById('main-pulse-slider').value)
    }));
  }
  function sendAutoSpotSettings() {
      const settings = {
          action: 'update_autospot_settings',
          enabled: document.getElementById('autospot-enabled').checked,
          trigThresh: parseFloat(document.getElementById('trig-thresh').value),
          vCutoff: parseFloat(document.getElementById('v-cutoff').value)
      };
      document.getElementById('autospot-settings').classList.toggle('hidden', !settings.enabled);
      websocket.send(JSON.stringify(settings));
  }
  window.onload = () => {
    initWebSocket();
    document.getElementById('spot-btn').onclick = () => websocket.send(JSON.stringify({ action: 'spot' }));
    ['pre-pulse', 'gap', 'main-pulse'].forEach(id => updateSliderVal(id));
    toggleMode();
    sendAutoSpotSettings();
  };
</script>
</body></html>
)rawliteral";

// -----------------------------------------------------------------------------
// 5. HELPER FUNCTIONS & WEBSOCKET HANDLER
// -----------------------------------------------------------------------------
void notifyClients(float vrms, float irms) {
    JsonDocument doc;
    doc["vrms"] = vrms;
    doc["irms"] = irms;
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, data, len);
        if (error) { return; }

        const char* action = doc["action"];
        if (strcmp(action, "spot") == 0) {
            if (!isWelding && !autoSpot.enabled) triggerWeld = true;
        } else if (strcmp(action, "update_weld_settings") == 0) {
            settings.mode = doc["mode"].as<String>();
            if (settings.mode == "double") {
                settings.pre_pulse_ms = doc["pre"];
                settings.gap_ms = doc["gap"];
            }
            settings.main_pulse_ms = doc["main"];
        } else if (strcmp(action, "update_autospot_settings") == 0) {
            autoSpot.enabled = doc["enabled"];
            autoSpot.trigThresh_A = doc["trigThresh"];
            autoSpot.vCutoff_V = doc["vCutoff"];
        }
    }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT: Serial.printf("Client #%u connected\n", client->id()); break;
        case WS_EVT_DISCONNECT: Serial.printf("Client #%u disconnected\n", client->id()); break;
        case WS_EVT_DATA: handleWebSocketMessage(arg, data, len); break;
        case WS_EVT_PONG: // Menambahkan handling untuk PONG dan ERROR agar kompatibel dengan library AsyncWebSocket yang berbeda versi
        case WS_EVT_ERROR:
            break;
    }
}

// -----------------------------------------------------------------------------
// 6. INTERRUPT SERVICE ROUTINE (ISR)
// -----------------------------------------------------------------------------
void IRAM_ATTR macroswitch_ISR() {
    if ((millis() - lastDebounceTime) > debounceDelay) {
        if (!isWelding && !autoSpot.enabled) {
            triggerWeld = true;
        }
        lastDebounceTime = millis();
    }
}

// -----------------------------------------------------------------------------
// 7. WELDING CORE LOGIC
// -----------------------------------------------------------------------------
void performWeld() {
    if (!triggerWeld || isWelding) return;
    isWelding = true;

    Serial.println("[WELD] Initiating weld sequence...");
    ws.textAll("{\"status\":\"WELDING...\"}");

    unsigned long timeoutStart = millis();
    Serial.println("[WELD] Waiting for ZMPT zero-crossing (rising edge)...");
    // Wait for the voltage to go below midpoint, then above, to catch rising zero-crossing
    while(analogRead(ZMPT_PIN) > zmptMidpoint) { 
        if(millis() - timeoutStart > 200) { // Increased timeout for safety
            Serial.println("[WELD ERROR] ZMPT timeout during pre-zero-cross wait.");
            ws.textAll("{\"status\":\"ZMPT ERR\"}"); 
            isWelding = false; triggerWeld = false; return; 
        } 
    }
    timeoutStart = millis(); // Reset timeout for the next wait
    while(analogRead(ZMPT_PIN) < zmptMidpoint) { 
        if(millis() - timeoutStart > 200) { // Increased timeout for safety
            Serial.println("[WELD ERROR] ZMPT timeout during zero-cross wait.");
            ws.textAll("{\"status\":\"ZMPT ERR\"}"); 
            isWelding = false; triggerWeld = false; return; 
        } 
    }
    Serial.println("[WELD] ZMPT zero-crossing detected.");


    // Pre-pulse if in double mode
    if (settings.mode == "double" && settings.pre_pulse_ms > 0) {
        Serial.printf("[WELD] Pre-pulse (%d ms) started.\n", settings.pre_pulse_ms);
        digitalWrite(SSR_PIN, HIGH);
        delay(settings.pre_pulse_ms);
        digitalWrite(SSR_PIN, LOW);
        Serial.printf("[WELD] Pre-pulse ended. Gap (%d ms) started.\n", settings.gap_ms);
        delay(settings.gap_ms);
        
        Serial.println("[WELD] Waiting for ZMPT zero-crossing before main pulse...");
        timeoutStart = millis(); // Reset timeout for next wait
        while(analogRead(ZMPT_PIN) > zmptMidpoint) { 
            if(millis() - timeoutStart > 200) { 
                Serial.println("[WELD ERROR] ZMPT timeout before main pulse.");
                ws.textAll("{\"status\":\"ZMPT ERR\"}"); 
                isWelding = false; triggerWeld = false; return; 
            } 
        }
        timeoutStart = millis(); // Reset timeout for next wait
        while(analogRead(ZMPT_PIN) < zmptMidpoint) { 
            if(millis() - timeoutStart > 200) { 
                Serial.println("[WELD ERROR] ZMPT timeout before main pulse.");
                ws.textAll("{\"status\":\"ZMPT ERR\"}"); 
                isWelding = false; triggerWeld = false; return; 
            } 
        }
        Serial.println("[WELD] ZMPT zero-crossing for main pulse detected.");
    }

    // Main Pulse
    Serial.printf("[WELD] Main pulse (%d ms) started.\n", settings.main_pulse_ms);
    digitalWrite(SSR_PIN, HIGH);
    delay(settings.main_pulse_ms);
    digitalWrite(SSR_PIN, LOW);
    Serial.println("[WELD] Main pulse ended.");
    
    ws.textAll("{\"status\":\"READY\"}");
    Serial.println("[WELD] Welding sequence complete. System READY.");
    
    isWelding = false;
    triggerWeld = false;
}

// -----------------------------------------------------------------------------
// 8. SETUP FUNCTION
// -----------------------------------------------------------------------------
void calibrateSensors() {
    Serial.println("[SETUP] Calibrating ZMPT sensor midpoint...");
    long zmptTotal = 0;
    for (int i = 0; i < 500; i++) {
        zmptTotal += analogRead(ZMPT_PIN);
        delay(1);
    }
    zmptMidpoint = zmptTotal / 500;
    Serial.printf("[SETUP] ZMPT midpoint calibrated to: %d\n", zmptMidpoint);

    Serial.println("[SETUP] Calibrating ACS712 sensor offset (no current)...");
    long acsTotal = 0;
    for (int i = 0; i < 500; i++) {
        acsTotal += analogRead(ACS712_PIN);
        delay(1);
    }
    acsOffset = acsTotal / 500;
    Serial.printf("[SETUP] ACS712 offset calibrated to: %d\n", acsOffset);
    Serial.println("[SETUP] NOTE: Ensure no current is flowing through ACS712 during calibration.");
}

void setup() {
    Serial.begin(115200);
    delay(1000); // Give 1 second for Serial Monitor to prepare
    Serial.println("\n\n--- MOTsmart Welder Boot Sequence Initiated (v3.0) ---");

    Serial.println("[SETUP] Initializing Pins...");
    pinMode(SSR_PIN, OUTPUT);
    digitalWrite(SSR_PIN, LOW);
    pinMode(MACROSWITCH_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(MACROSWITCH_PIN), macroswitch_ISR, FALLING);
    Serial.println("[SETUP] Pin Initialization Complete.");

    calibrateSensors(); // Will print its own debug messages

    Serial.println("[SETUP] Setting up EmonLib...");
    emon.voltage(ZMPT_PIN, 230.0, 1.7); // Adjust 1.7 calibration constant if needed
    emon.current(ACS712_PIN, 30.0);    // Adjust 30.0 for your specific ACS712 module (mV/A)
    Serial.println("[SETUP] EmonLib Setup Complete.");

    Serial.println("[SETUP] Starting WiFi Access Point...");
    WiFi.softAP(ssid);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("[SETUP] AP IP address: ");
    Serial.println(IP);
    Serial.printf("[SETUP] WiFi AP '%s' Started Successfully!\n", ssid);

    ws.onEvent(onEvent);
    server.addHandler(&ws);
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html);
    });
    
    ElegantOTA.begin(&server);
    
    server.begin();
    Serial.println("[SETUP] Web Server started. OTA available at /update");
    Serial.println("===================================");
    Serial.println("--- MOTsmart Welder System READY ---");
    Serial.println("===================================");
}

// -----------------------------------------------------------------------------
// 9. MAIN LOOP
// -----------------------------------------------------------------------------
void loop() {
    ws.cleanupClients();
    performWeld();
    ElegantOTA.loop();

    static unsigned long lastSensorRead = 0;
    // Read sensors and update web UI every 250ms
    if (millis() - lastSensorRead > 250) {
        emon.calcVI(20, 2000); // 20 cycles, 2000ms timeout
        
        if (autoSpot.enabled && !isWelding) {
            // Only trigger auto-spot if current is above threshold, voltage is good, and not over current limit
            if (emon.Irms > autoSpot.trigThresh_A && emon.Vrms >= autoSpot.vCutoff_V && emon.Irms < autoSpot.iLimit_A) {
                Serial.printf("[AUTOSPOT] Triggered: Irms=%.2fA, Vrms=%.2fV\n", emon.Irms, emon.Vrms);
                triggerWeld = true;
            }
        }
        
        static unsigned long lastNotify = 0;
        // Send updates to clients every 1 second
        if(millis() - lastNotify > 1000){
            notifyClients(emon.Vrms, emon.Irms);
            lastNotify = millis();
        }
        lastSensorRead = millis();
    }
}
