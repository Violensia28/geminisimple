/**
 * @file main.cpp
 * @author Gemini AI Programmer
 * @brief FINAL firmware with all features, including Smart AI Feedback.
 * @version 3.2 (Final - Smart AI Restored)
 * @date 2025-10-06
 *
 * Project: MOTsmart SimpleWeld
 *
 * Features:
 * - Smart AI mode with feedback loop ("OK" / "Weak" buttons).
 * - Auto-calibration for ZMPT & ACS712.
 * - OTA Updates.
 * - Auto Spot trigger.
 * - Dual & Single Pulse modes.
 *
 * Pinout:
 * - SSR: GPIO 26
 * - ZMPT: GPIO 35
 * - ACS712: GPIO 34
 * - Macroswitch: GPIO 18
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include "EmonLib.h"
#include <ElegantOTA.h>

// Pin Definitions
const int SSR_PIN = 26;
const int ZMPT_PIN = 35;
const int ACS712_PIN = 34;
const int MACROSWITCH_PIN = 18;

// Global Config & Variables
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
  int target_energy_ws = 25;
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
float last_weld_energy = 0;
float locked_energy = 0;

volatile unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

// Web Interface
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>MOTsmart Welder Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    :root { --accent-color: #00bcd4; --bg-color: #1e1e1e; --text-color: #e0e0e0; --card-color: #333; --success-color: #2ecc71; --warning-color: #f1c40f; --danger-color: #e74c3c;}
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
    .mode-selector label { border: 1px solid #555; padding: 10px; border-radius: 5px; width: 30%; }
    .mode-selector input[type="radio"] { display: none; }
    .mode-selector input[type="radio"]:checked + label { background-color: var(--accent-color); border-color: var(--accent-color); }
    .toggle-switch { position: relative; display: inline-block; width: 60px; height: 34px; }
    .toggle-switch input { opacity: 0; width: 0; height: 0; }
    .toggle-slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; transition: .4s; border-radius: 34px; }
    .toggle-slider:before { position: absolute; content: ""; height: 26px; width: 26px; left: 4px; bottom: 4px; background-color: white; transition: .4s; border-radius: 50%; }
    input:checked + .toggle-slider { background-color: var(--accent-color); }
    input:checked + .toggle-slider:before { transform: translateX(26px); }
    .sensor-readings { font-size: 1.2em; margin-top: 15px; display: flex; justify-content: space-around; }
  </style>
</head>
<body>
  <h1>MOTsmart Welder v3.2</h1>
  <div class="card">
    <h2>Sensor Readings</h2>
    <div class="sensor-readings">
      <div>Vrms: <span id="vrms-val">0.0</span> V</div>
      <div>Irms: <span id="irms-val">0.00</span> A</div>
    </div>
  </div>
  <div class="card">
    <h2>Auto Spot</h2>
    <label class="toggle-switch"><input type="checkbox" id="autospot-enabled" onchange="sendAutoSpotSettings()"><span class="toggle-slider"></span></label>
    <div id="autospot-settings" class="hidden" style="margin-top: 15px;">
      <label>Trigger Current: <input type="number" id="trig-thresh" step="0.1" value="0.8" onchange="sendAutoSpotSettings()"> A</label>
      <label style="margin-top: 10px;">Voltage Cutoff: <input type="number" id="v-cutoff" step="1" value="210" onchange="sendAutoSpotSettings()"> V</label>
    </div>
  </div>
  <div class="card">
    <h2>Mode</h2>
    <div class="mode-selector">
      <input type="radio" id="mode-double" name="weld-mode" value="double" onchange="toggleMode()" checked><label for="mode-double">Dual</label>
      <input type="radio" id="mode-single" name="weld-mode" value="single" onchange="toggleMode()"><label for="mode-single">Single</label>
      <input type="radio" id="mode-smart" name="weld-mode" value="smart" onchange="toggleMode()"><label for="mode-smart">Smart</label>
    </div>
    <div id="pre-pulse-container" class="slider-container"><label for="pre-pulse-slider">Pre-Pulse: <span id="pre-pulse-val">20</span> ms</label><input type="range" min="0" max="100" value="20" id="pre-pulse-slider" oninput="updateSliderVal('pre-pulse')" onchange="sendWeldSettings()"></div>
    <div id="gap-container" class="slider-container"><label for="gap-slider">Gap: <span id="gap-val">40</span> ms</label><input type="range" min="10" max="200" value="40" id="gap-slider" oninput="updateSliderVal('gap')" onchange="sendWeldSettings()"></div>
    <div class="slider-container"><label id="main-label" for="main-pulse-slider">Main Pulse: <span id="main-pulse-val">120</span> ms</label><input type="range" min="20" max="500" value="120" id="main-pulse-slider" oninput="updateSliderVal('main-pulse')" onchange="sendWeldSettings()"></div>
  </div>
  <button id="spot-btn" class="button">SPOT</button>
  <div id="feedback-section" class="card hidden">
    <h2>Hasil Las Terakhir</h2>
    <div class="sensor-readings">
        <div>Energi: <span id="energy-val">0.00</span> Ws</div>
        <div>Durasi: <span id="pulse-val">0</span> ms</div>
    </div>
    <button id="ok-btn" class="button" style="background-color:var(--success-color);" onclick="sendFeedback('ok')">Hasil OK 👍 (Kunci)</button>
    <button class="button" style="background-color:var(--danger-color);" onclick="sendFeedback('weak')">Kurang Kuat 👎</button>
  </div>
  <div class="card"><a href="/update">Firmware Update</a></div>
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
          spotBtn.style.backgroundColor = (data.status !== "READY") ? 'var(--warning-color)' : 'var(--accent-color)';
      }
      if (data.vrms !== undefined) document.getElementById('vrms-val').innerText = data.vrms.toFixed(1);
      if (data.irms !== undefined) document.getElementById('irms-val').innerText = data.irms.toFixed(2);
      if (data.energy !== undefined) {
          document.getElementById('energy-val').innerText = data.energy.toFixed(2);
          document.getElementById('feedback-section').classList.remove('hidden');
      }
      if (data.pulse !== undefined) document.getElementById('pulse-val').innerText = data.pulse;
      if (data.locked_energy !== undefined) {
          const okBtn = document.getElementById('ok-btn');
          if (data.locked_energy > 0) {
              okBtn.innerText = `Terkunci: ${data.locked_energy.toFixed(2)} Ws`;
              okBtn.style.backgroundColor = 'var(--warning-color)';
          } else {
              okBtn.innerText = 'Hasil OK 👍 (Kunci)';
              okBtn.style.backgroundColor = 'var(--success-color)';
          }
      }
    };
  }
  function updateSliderVal(id) {
    const slider = document.getElementById(id + '-slider');
    const valSpan = document.getElementById(id + '-val');
    const mode = document.querySelector('input[name="weld-mode"]:checked').value;
    const unit = (id === 'main-pulse' && mode === 'smart') ? ' Ws' : ' ms';
    valSpan.innerText = slider.value + unit;
  }
  function toggleMode() {
    const mode = document.querySelector('input[name="weld-mode"]:checked').value;
    const prePulse = document.getElementById('pre-pulse-container');
    const gap = document.getElementById('gap-container');
    const feedback = document.getElementById('feedback-section');
    const mainLabel = document.getElementById('main-label');
    const mainSlider = document.getElementById('main-pulse-slider');

    prePulse.classList.toggle('hidden', mode !== 'double');
    gap.classList.toggle('hidden', mode !== 'double');
    feedback.classList.toggle('hidden', mode !== 'smart');

    if (mode === 'smart') {
      mainLabel.childNodes[0].nodeValue = 'Target Energi: ';
      mainSlider.min = 5; mainSlider.max = 100; mainSlider.value = 25;
    } else {
      mainLabel.childNodes[0].nodeValue = 'Main Pulse: ';
      mainSlider.min = 20; mainSlider.max = 500; mainSlider.value = 120;
    }
    updateSliderVal('main-pulse');
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
  function sendFeedback(type) {
    websocket.send(JSON.stringify({ action: `feedback_${type}` }));
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

// Helper Functions & WebSocket Handler
void notifyStatus(const char* status) {
    JsonDocument doc;
    doc["status"] = status;
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void notifyClients(float vrms, float irms, float lockedEnergy) {
    JsonDocument doc;
    doc["vrms"] = vrms;
    doc["irms"] = irms;
    doc["locked_energy"] = lockedEnergy;
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void notifyWeldResult(unsigned long final_pulse, float energy) {
    JsonDocument doc;
    doc["pulse"] = final_pulse;
    doc["energy"] = energy;
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
    last_weld_energy = energy;
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) return;

        const char* action = doc["action"];
        if (strcmp(action, "spot") == 0) {
            if (!isWelding && !autoSpot.enabled) triggerWeld = true;
        } else if (strcmp(action, "update_weld_settings") == 0) {
            settings.mode = doc["mode"].as<String>();
            if (settings.mode == "double") {
                settings.pre_pulse_ms = doc["pre"];
                settings.gap_ms = doc["gap"];
            }
            if (settings.mode == "smart") {
                settings.target_energy_ws = doc["main"];
            } else {
                settings.main_pulse_ms = doc["main"];
            }
        } else if (strcmp(action, "update_autospot_settings") == 0) {
            autoSpot.enabled = doc["enabled"];
            autoSpot.trigThresh_A = doc["trigThresh"];
            autoSpot.vCutoff_V = doc["vCutoff"];
        } else if (strcmp(action, "feedback_ok") == 0) {
            locked_energy = last_weld_energy;
            notifyClients(emon.Vrms, emon.Irms, locked_energy);
        } else if (strcmp(action, "feedback_weak") == 0) {
            locked_energy = 0;
            notifyClients(emon.Vrms, emon.Irms, locked_energy);
        }
    }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT: Serial.printf("Client #%u connected\n", client->id()); break;
        case WS_EVT_DISCONNECT: Serial.printf("Client #%u disconnected\n", client->id()); break;
        case WS_EVT_DATA: handleWebSocketMessage(arg, data, len); break;
        case WS_EVT_PONG: case WS_EVT_ERROR: break;
    }
}

// Interrupt Service Routine (ISR)
void IRAM_ATTR macroswitch_ISR() {
    if ((millis() - lastDebounceTime) > debounceDelay) {
        if (!isWelding && !autoSpot.enabled) triggerWeld = true;
        lastDebounceTime = millis();
    }
}

// Welding Core Logic
void performWeld() {
    if (!triggerWeld || isWelding) return;
    isWelding = true;

    Serial.println("[WELD] Initiating...");
    notifyStatus("WELDING...");

    unsigned long timeoutStart = millis();
    while(analogRead(ZMPT_PIN) > zmptMidpoint) { if(millis() - timeoutStart > 200) { notifyStatus("ZMPT ERR"); isWelding = false; triggerWeld = false; return; } }
    timeoutStart = millis();
    while(analogRead(ZMPT_PIN) < zmptMidpoint) { if(millis() - timeoutStart > 200) { notifyStatus("ZMPT ERR"); isWelding = false; triggerWeld = false; return; } }
    Serial.println("[WELD] Z-Cross OK.");

    if (settings.mode == "double" && settings.pre_pulse_ms > 0) {
        digitalWrite(SSR_PIN, HIGH); delay(settings.pre_pulse_ms); digitalWrite(SSR_PIN, LOW);
        delay(settings.gap_ms);
        timeoutStart = millis();
        while(analogRead(ZMPT_PIN) > zmptMidpoint) { if(millis() - timeoutStart > 200) { notifyStatus("ZMPT ERR"); isWelding = false; triggerWeld = false; return; } }
        timeoutStart = millis();
        while(analogRead(ZMPT_PIN) < zmptMidpoint) { if(millis() - timeoutStart > 200) { notifyStatus("ZMPT ERR"); isWelding = false; triggerWeld = false; return; } }
    }

    unsigned long mainPulseStartTime = millis();
    digitalWrite(SSR_PIN, HIGH);
    
    float targetEnergy = (locked_energy > 0) ? locked_energy : (float)settings.target_energy_ws;
    if (settings.mode == "smart") {
        float current_energy = 0;
        unsigned long last_calc_time = millis();
        while (current_energy < targetEnergy) {
            emon.calcVI(1, 100);
            float power = emon.Vrms * emon.Irms;
            unsigned long now = millis();
            current_energy += power * ((now - last_calc_time) / 1000.0);
            last_calc_time = now;
            if (now - mainPulseStartTime > 1000) break; // 1 second safety timeout
        }
    } else {
        delay(settings.main_pulse_ms);
    }
    digitalWrite(SSR_PIN, LOW);
    unsigned long finalPulseDuration = millis() - mainPulseStartTime;

    emon.calcVI(20, 2000);
    float final_power = emon.Vrms * emon.Irms;
    float final_energy = final_power * (finalPulseDuration / 1000.0);

    if(settings.mode == "smart") notifyWeldResult(finalPulseDuration, final_energy);
    
    notifyStatus("READY");
    Serial.printf("[WELD] Done. Duration: %lu ms, Energy: %.2f Ws\n", finalPulseDuration, final_energy);
    isWelding = false;
    triggerWeld = false;
}

// Setup Function
void calibrateSensors() {
    Serial.println("[SETUP] Calibrating ZMPT...");
    long zmptTotal = 0;
    for (int i = 0; i < 1000; i++) { zmptTotal += analogRead(ZMPT_PIN); delay(1); }
    zmptMidpoint = zmptTotal / 1000;
    Serial.printf("[SETUP] ZMPT midpoint: %d\n", zmptMidpoint);

    Serial.println("[SETUP] Calibrating ACS712...");
    long acsTotal = 0;
    for (int i = 0; i < 1000; i++) { acsTotal += analogRead(ACS712_PIN); delay(1); }
    acsOffset = acsTotal / 1000;
    Serial.printf("[SETUP] ACS712 offset: %d\n", acsOffset);
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n--- MOTsmart Welder Boot Sequence (v3.2) ---");

    pinMode(SSR_PIN, OUTPUT); digitalWrite(SSR_PIN, LOW);
    pinMode(MACROSWITCH_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(MACROSWITCH_PIN), macroswitch_ISR, FALLING);
    
    calibrateSensors();

    emon.voltage(ZMPT_PIN, 220.0, 0.8);
    emon.current(ACS712_PIN, 66.0); // Calibrated for ACS712-30A
    
    WiFi.softAP(ssid);
    Serial.print("[SETUP] AP IP: "); Serial.println(WiFi.softAPIP());

    ws.onEvent(onEvent);
    server.addHandler(&ws);
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", index_html); });
    
    ElegantOTA.begin(&server);
    server.begin();
    Serial.println("[SETUP] System READY.");
}

// Main Loop
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
            notifyClients(emon.Vrms, emon.Irms, locked_energy);
            lastNotify = millis();
        }
        lastSensorRead = millis();
    }
}

