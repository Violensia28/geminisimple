/**
 * @file main.cpp
 * @author Gemini AI Programmer
 * @brief Final firmware with build fix for WebSocket event handler.
 * @version 2.9 (Final Build Fix)
 * @date 2025-10-06
 *
 * @copyright Copyright (c) 2025
 *
 * Project: MOTsmart SimpleWeld
 * Director: User
 * Lead Programmer: Gemini AI
 *
 * Features:
 * - All sensors auto-calibrated.
 * - OTA Updates enabled.
 * - Auto Spot trigger enabled.
 * - Dual/Single Pulse modes.
 *
 * Pinout:
 * - SSR Control -> GPIO 26
 * - ZMPT101B (Zero Cross) -> GPIO 35
 * - ACS712 (Current Sense) -> GPIO 34
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
const int ZMPT_PIN = 35;
const int ACS712_PIN = 34;
const int MACROSWITCH_PIN = 18;

// -----------------------------------------------------------------------------
// 3. GLOBAL CONFIGURATION & VARIABLES
// -----------------------------------------------------------------------------
const char* ssid = "MOTsmart_Welder";
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
EnergyMonitor emon;

int zmptMidpoint = 2048;
int acsOffset = 2048;

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
  float iLimit_A = 35.0;
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
  <h1>MOTsmart Welder v2.9</h1>
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

// <<<<<<<<<<< PERUBAHAN DI SINI
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("Client #%u connected\n", client->id());
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("Client #%u disconnected\n", client->id());
            break;
        case WS_EVT_DATA:
            handleWebSocketMessage(arg, data, len);
            break;
        case WS_EVT_PONG:
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

    ws.textAll("{\"status\":\"WELDING...\"}");

    unsigned long timeoutStart = millis();
    while(analogRead(ZMPT_PIN) > zmptMidpoint) { if(millis() - timeoutStart > 50) { ws.textAll("{\"status\":\"ZMPT ERROR\"}"); isWelding = false; triggerWeld = false; return; } }
    while(analogRead(ZMPT_PIN) < zmptMidpoint) { if(millis() - timeoutStart > 50) { ws.textAll("{\"status\":\"ZMPT ERROR\"}"); isWelding = false; triggerWeld = false; return; } }

    if (settings.mode == "double" && settings.pre_pulse_ms > 0) {
        digitalWrite(SSR_PIN, HIGH);
        delay(settings.pre_pulse_ms);
        digitalWrite(SSR_PIN, LOW);
        delay(settings.gap_ms);
        
        timeoutStart = millis();
        while(analogRead(ZMPT_PIN) > zmptMidpoint) { if(millis() - timeoutStart > 250) { ws.textAll("{\"status\":\"ZMPT ERROR\"}"); isWelding = false; triggerWeld = false; return; } }
        while(analogRead(ZMPT_PIN) < zmptMidpoint) { if(millis() - timeoutStart > 250) { ws.textAll("{\"status\":\"ZMPT ERROR\"}"); isWelding = false; triggerWeld = false; return; } }
    }

    digitalWrite(SSR_PIN, HIGH);
    delay(settings.main_pulse_ms);
    digitalWrite(SSR_PIN, LOW);
    
    ws.textAll("{\"status\":\"READY\"}");
    
    isWelding = false;
    triggerWeld = false;
}

// -----------------------------------------------------------------------------
// 8. SETUP FUNCTION
// -----------------------------------------------------------------------------
void calibrateSensors() {
    Serial.println("Calibrating ZMPT sensor midpoint...");
    long zmptTotal = 0;
    for (int i = 0; i < 500; i++) {
        zmptTotal += analogRead(ZMPT_PIN);
        delay(1);
    }
    zmptMidpoint = zmptTotal / 500;
    Serial.print("ZMPT midpoint calibrated to: ");
    Serial.println(zmptMidpoint);
    
    Serial.println("Calibrating ACS712 sensor offset...");
    long acsTotal = 0;
    for (int i = 0; i < 500; i++) {
        acsTotal += analogRead(ACS712_PIN);
        delay(1);
    }
    acsOffset = acsTotal / 500;
    Serial.print("ACS712 offset calibrated to: ");
    Serial.println(acsOffset);
}

void setup() {
    Serial.begin(115200);
    pinMode(SSR_PIN, OUTPUT);
    digitalWrite(SSR_PIN, LOW);
    pinMode(MACROSWITCH_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(MACROSWITCH_PIN), macroswitch_ISR, FALLING);
    
    calibrateSensors();
    
    emon.voltage(ZMPT_PIN, 230.0, 1.7);
    emon.current(ACS712_PIN, 30.0);

    WiFi.softAP(ssid);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    ws.onEvent(onEvent); // <<<<<<<<<<< Pemanggilan fungsi ini yang menyebabkan error
    server.addHandler(&ws);
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", index_html);
    });
    
    ElegantOTA.begin(&server);
    
    server.begin();
    Serial.println("Server started. OTA on /update");
}

// -----------------------------------------------------------------------------
// 9. MAIN LOOP
// -----------------------------------------------------------------------------
void loop() {
    ws.cleanupClients();
    performWeld();
    ElegantOTA.loop();

    static unsigned long lastSensorRead = 0;
    if (millis() - lastSensorRead > 250) {
        emon.calcVI(20, 2000);
        
        if (autoSpot.enabled && !isWelding) {
            if (emon.Irms > autoSpot.trigThresh_A && emon.Vrms >= autoSpot.vCutoff_V && emon.Irms < autoSpot.iLimit_A) {
                triggerWeld = true;
            }
        }
        
        static unsigned long lastNotify = 0;
        if(millis() - lastNotify > 1000){
            notifyClients(emon.Vrms, emon.Irms);
            lastNotify = millis();
        }
        lastSensorRead = millis();
    }
}

