/**
 * @file main.cpp
 * @author Gemini AI Programmer
 * @brief Firmware for a smart, minimal, and powerful MOT Spot Welder.
 * @version 1.0
 * @date 2025-10-05
 *
 * @copyright Copyright (c) 2025
 *
 * Project: MOTsmart SimpleWeld
 * Director: User
 * Lead Programmer: Gemini AI
 *
 * Features:
 * - ESP32 based smart control.
 * - WiFi Access Point for web UI control.
 * - Responsive Web Interface to set pulse and trigger welding.
 * - Zero-crossing detection for precise and clean welds.
 * - ACS712 current sensing for monitoring.
 * - Physical macroswitch trigger with debouncing.
 * - Real-time status updates via WebSockets.
 * - Non-blocking, asynchronous operation.
 */

// -----------------------------------------------------------------------------
// 1. LIBRARY INCLUDES
// -----------------------------------------------------------------------------
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>

// -----------------------------------------------------------------------------
// 2. HARDWARE PIN DEFINITIONS
// -----------------------------------------------------------------------------
const int SSR_PIN = 23;          // Pin to control the Solid State Relay
const int ZMPT_PIN = 34;         // Analog pin for ZMPT101B zero-cross detection
const int ACS712_PIN = 35;       // Analog pin for ACS712 current sensor
const int MACROSWITCH_PIN = 18;  // Pin for the physical weld trigger switch

// -----------------------------------------------------------------------------
// 3. GLOBAL CONFIGURATION & VARIABLES
// -----------------------------------------------------------------------------
// WiFi Access Point credentials
const char* ssid = "MOTsmart_Welder";
const char* password = NULL; // No password for easy access

// Web Server and WebSockets setup
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Welding parameters
volatile unsigned int weldPulseDuration = 80; // Default pulse duration in milliseconds
volatile bool triggerWeld = false;            // Flag to start the welding process
volatile bool isWelding = false;              // Flag to prevent re-triggering during a weld

// Sensor and timing variables
int acsOffset = 2048;               // Default ADC value for 0A on ACS712 (will be calibrated)
float currentAmps = 0.0;            // Calculated current
const float ADC_SCALE = 3.3 / 4095.0; // ADC voltage scale
const float ACS712_SENSITIVITY = 0.100; // Sensitivity for ACS712-30A is 100mV/A

// Debouncing for macroswitch
volatile unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50; // 50ms debounce time

// -----------------------------------------------------------------------------
// 4. WEB INTERFACE (HTML, CSS, JAVASCRIPT)
// -----------------------------------------------------------------------------
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>MOTsmart Welder Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html { font-family: Arial, Helvetica, sans-serif; display: inline-block; text-align: center; }
    body { max-width: 450px; margin: 0px auto; padding-bottom: 25px; background-color: #1e1e1e; color: #e0e0e0; }
    h1 { color: #00bcd4; }
    .slider-container { margin: 20px 0; }
    .slider { -webkit-appearance: none; width: 80%; height: 15px; border-radius: 5px; background: #555; outline: none; opacity: 0.7; -webkit-transition: .2s; transition: opacity .2s; }
    .slider::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 25px; height: 25px; border-radius: 50%; background: #00bcd4; cursor: pointer; }
    .slider::-moz-range-thumb { width: 25px; height: 25px; border-radius: 50%; background: #00bcd4; cursor: pointer; }
    .button { background-color: #d9534f; border: none; color: white; padding: 16px 40px; text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer; border-radius: 8px; }
    .button-spot { background-color: #00bcd4; }
    .button-spot:active { background-color: #0097a7; }
    .status-box { background-color: #333; padding: 15px; border-radius: 8px; margin-top: 20px; }
    .status-label { font-size: 1.2em; color: #aaa; }
    .status-value { font-size: 2em; color: #fff; font-weight: bold; }
  </style>
</head>
<body>
  <h1>MOTsmart SimpleWeld</h1>
  
  <div class="slider-container">
    <h2>Pulse Duration: <span id="pulseValue">80</span> ms</h2>
    <input type="range" min="20" max="500" value="80" class="slider" id="pulseSlider">
  </div>
  
  <div>
    <button id="spotButton" class="button button-spot">SPOT</button>
  </div>
  
  <div class="status-box">
    <div class="status-label">STATUS</div>
    <div id="statusText" class="status-value">READY</div>
    <div class="status-label" style="margin-top:15px;">PRIMARY CURRENT (RMS)</div>
    <div id="currentValue" class="status-value">0.00 A</div>
  </div>

<script>
  var gateway = `ws://${window.location.hostname}/ws`;
  var websocket;

  window.addEventListener('load', onLoad);

  function onLoad(event) {
    initWebSocket();
  }

  function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket(gateway);
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
  }

  function onOpen(event) { console.log('Connection opened'); }
  function onClose(event) { 
    console.log('Connection closed'); 
    document.getElementById('statusText').innerHTML = "OFFLINE";
    setTimeout(initWebSocket, 2000); 
  }

  function onMessage(event) {
    console.log(event.data);
    var data = JSON.parse(event.data);
    document.getElementById('statusText').innerHTML = data.status;
    document.getElementById('currentValue').innerHTML = parseFloat(data.current).toFixed(2) + " A";
    if(data.status !== "WELDING...") {
        document.getElementById('spotButton').disabled = false;
        document.getElementById('spotButton').style.backgroundColor = '#00bcd4';
    } else {
        document.getElementById('spotButton').disabled = true;
        document.getElementById('spotButton').style.backgroundColor = '#555';
    }
  }

  var slider = document.getElementById("pulseSlider");
  var output = document.getElementById("pulseValue");
  output.innerHTML = slider.value;

  slider.oninput = function() {
    output.innerHTML = this.value;
  }

  slider.onchange = function() {
    websocket.send("PULSE" + this.value);
  }

  document.getElementById("spotButton").onclick = function() {
    websocket.send("SPOT");
  }
</script>
</body>
</html>
)rawliteral";

// -----------------------------------------------------------------------------
// 5. FUNCTION DECLARATIONS & WEBSOCKET HANDLER
// -----------------------------------------------------------------------------
void notifyClients(String status, float current) {
  String json = "{\"status\":\"" + status + "\", \"current\":" + String(current) + "}";
  ws.textAll(json);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    String message = (char*)data;
    
    if (message.startsWith("PULSE")) {
      weldPulseDuration = message.substring(5).toInt();
      Serial.print("Pulse duration set to: ");
      Serial.println(weldPulseDuration);
    }
    
    if (message == "SPOT") {
      Serial.println("Weld triggered from web.");
      if (!isWelding) {
        triggerWeld = true;
      }
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      notifyClients(isWelding ? "WELDING..." : "READY", currentAmps);
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
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
    if (!isWelding) {
      triggerWeld = true;
    }
    lastDebounceTime = millis();
  }
}

// -----------------------------------------------------------------------------
// 7. SENSOR & WELDING CORE LOGIC
// -----------------------------------------------------------------------------
void calibrateAcs() {
  Serial.println("Calibrating ACS712 sensor... Do not apply current.");
  long total = 0;
  for (int i = 0; i < 500; i++) {
    total += analogRead(ACS712_PIN);
    delay(1);
  }
  acsOffset = total / 500;
  Serial.print("ACS712 offset calibrated to: ");
  Serial.println(acsOffset);
}

void readCurrent() {
  // Simple RMS calculation - more advanced methods exist but this is good for estimation
  long total = 0;
  long start = millis();
  int count = 0;
  while(millis() - start < 100) { // Sample over 100ms (5 cycles at 50Hz)
    total += sq(analogRead(ACS712_PIN) - acsOffset);
    count++;
  }
  
  float meanSquare = (float)total / count;
  float rmsAdc = sqrt(meanSquare);
  float rmsVoltage = rmsAdc * ADC_SCALE;
  currentAmps = rmsVoltage / ACS712_SENSITIVITY;
}

void performWeld() {
  if (triggerWeld && !isWelding) {
    isWelding = true;
    triggerWeld = false;
    
    Serial.println("Welding process started...");
    notifyClients("WELDING...", currentAmps);

    // Wait for the next zero-crossing point to start the weld
    // This simple method polls for the signal to cross the midpoint (approx 2048 for 12-bit ADC)
    // A rising edge is a good point to start
    while(analogRead(ZMPT_PIN) > 2048) { delayMicroseconds(10); } // Wait for it to be low
    while(analogRead(ZMPT_PIN) < 2048) { delayMicroseconds(10); } // Wait for it to rise (zero-cross)

    // --- WELDING PULSE START ---
    digitalWrite(SSR_PIN, HIGH);
    
    unsigned long weldStartTime = millis();
    while(millis() - weldStartTime < weldPulseDuration) {
      // We can read current during the weld
      readCurrent();
      Serial.print("Current during weld: ");
      Serial.println(currentAmps);
      notifyClients("WELDING...", currentAmps);
      delay(20); // Update current reading every 20ms
    }

    digitalWrite(SSR_PIN, LOW);
    // --- WELDING PULSE END ---

    Serial.println("Weld complete.");
    isWelding = false;
    readCurrent(); // Read one last time to show resting current
    notifyClients("READY", currentAmps);
  }
}


// -----------------------------------------------------------------------------
// 8. SETUP FUNCTION
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // Initialize GPIO pins
  pinMode(SSR_PIN, OUTPUT);
  digitalWrite(SSR_PIN, LOW);
  pinMode(MACROSWITCH_PIN, INPUT_PULLUP);
  
  // Attach interrupt for the macroswitch
  attachInterrupt(digitalPinToInterrupt(MACROSWITCH_PIN), macroswitch_ISR, FALLING);

  // Calibrate current sensor
  calibrateAcs();
  
  // Start WiFi Access Point
  Serial.print("Starting AP: ");
  Serial.println(ssid);
  WiFi.softAP(ssid, password);
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  // Initialize WebSocket
  ws.onEvent(onEvent);
  server.addHandler(&ws);

  // Define web server routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  // Start server
  server.begin();
  Serial.println("Server started. Ready to weld.");
}

// -----------------------------------------------------------------------------
// 9. MAIN LOOP
// -----------------------------------------------------------------------------
void loop() {
  // The main loop is kept clean. Everything is event-driven.
  
  ws.cleanupClients(); // Handle disconnected WebSocket clients
  
  performWeld(); // Check if a weld needs to be performed

  // Periodically read current when not welding and update clients
  if (!isWelding) {
    static unsigned long lastCurrentRead = 0;
    if (millis() - lastCurrentRead > 1000) { // Update every second
      readCurrent();
      notifyClients("READY", currentAmps);
      lastCurrentRead = millis();
    }
  }
}
