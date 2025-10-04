// WAJIB DI BARIS PALING ATAS
#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <ArduinoJson.h>
#include "EmonLib.h"

// --- KONFIGURASI ---
const char* ssid = "MOTsmart SimpleWeld";
const char* password = "password123";
const int WELD_PIN = 22;      // Pin trigger SSR/Relay
const int VOLTAGE_PIN = 35;   // Pin analog untuk ZMPT101B
const int CURRENT_PIN = 34;   // Pin analog untuk ACS712
const int AUTOSPOT_PIN = 23; // Pin untuk footswitch/microswitch
const int MAX_PULSE_SAFETY_LIMIT_MS = 1000; // Batas aman pulsa maksimal 1 detik

// --- Objek & Variabel Global ---
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
EnergyMonitor emon;
unsigned long last_sensor_read = 0;
const int SENSOR_READ_INTERVAL = 1000;

// Variabel untuk mode Smart-Learn
float suggested_energy = 400; // Energi awal yang disarankan
float locked_energy = 0;      // 0 berarti belum dikunci
const int ENERGY_ADJUST_STEP = 50; // Penambahan/pengurangan energi saat feedback

// --- Halaman Web (HTML, CSS, JavaScript) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>MOTsmart SimpleWeld</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    :root { --accent-color: #00bcd4; --bg-color: #222; --text-color: #fff; --card-color: #333; --btn-color: #444; }
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; text-align: center; background-color: var(--bg-color); color: var(--text-color); margin: 0; padding: 15px;}
    .card { background-color: var(--card-color); border-radius: 10px; padding: 20px; max-width: 400px; margin: 20px auto; box-shadow: 0 4px 8px rgba(0,0,0,0.2); }
    h1 { color: var(--accent-color); margin-top: 0; }
    .sensor-readings { display: flex; justify-content: space-around; margin-bottom: 20px; font-size: 1.2rem; }
    .slider-container { margin: 20px 0; }
    .slider-label { display: flex; justify-content: space-between; font-size: 1.1rem; margin-bottom: 10px; }
    input[type=range] { width: 100%; }
    #spot-btn { background-color: var(--accent-color); color: var(--bg-color); font-size: 1.5rem; font-weight: bold; padding: 15px 30px; border: none; border-radius: 5px; cursor: pointer; width: 100%; transition: background-color 0.3s; }
    #status-box { background-color: var(--bg-color); padding: 15px; margin-top: 20px; border-radius: 5px; font-size: 1.5rem; font-weight: bold; color: #7f8c8d; }
    .mode-selector { display: flex; justify-content: space-around; background-color: var(--btn-color); border-radius: 5px; padding: 5px; margin-bottom: 20px; }
    .mode-selector label { padding: 10px; cursor: pointer; flex-grow: 1; border-radius: 5px; transition: all 0.2s; }
    .mode-selector input { display: none; }
    .mode-selector input:checked + label { background-color: var(--accent-color); color: var(--bg-color); font-weight: bold; }
    .feedback-buttons { display: flex; justify-content: space-between; margin-top: 15px; gap: 10px; }
    .feedback-buttons button { flex-grow: 1; padding: 10px; font-size: 1rem; border: none; border-radius: 5px; cursor: pointer; }
    #weak-btn { background-color: #e74c3c; color: white; }
    #ok-btn { background-color: #2ecc71; color: white; }
    .hidden { display: none; }
    footer a { color: #555; text-decoration: none; font-size: 0.8rem; }
  </style>
</head>
<body>
  <div class="card">
    <h1>MOTsmart SimpleWeld</h1>
    <div class="sensor-readings">
      <div>Voltage: <span id="vrms">0.0</span> V</div>
      <div>Current: <span id="irms">0.00</span> A</div>
    </div>
    <div class="mode-selector">
      <input type="radio" id="mode-smart" name="weld-mode" value="smart" checked onchange="updateUI()">
      <label for="mode-smart">Smart-Learn</label>
      <input type="radio" id="mode-single" name="weld-mode" value="single" onchange="updateUI()">
      <label for="mode-single">Single</label>
      <input type="radio" id="mode-double" name="weld-mode" value="double" onchange="updateUI()">
      <label for="mode-double">Double</label>
    </div>
    <div id="pre-pulse-container" class="slider-container hidden">
      <div class="slider-label"><span>Pre-Pulse</span><span id="pre-pulse-val">20 ms</span></div>
      <input type="range" id="pre-pulse-slider" min="1" max="100" value="20" oninput="updateSliderVal('pre-pulse')">
    </div>
    <div id="gap-container" class="slider-container hidden">
      <div class="slider-label"><span>Gap</span><span id="gap-val">40 ms</span></div>
      <input type="range" id="gap-slider" min="1" max="200" value="40" oninput="updateSliderVal('gap')">
    </div>
    <div id="main-pulse-container" class="slider-container">
      <div class="slider-label"><span id="main-label">Energy</span><span id="main-pulse-val">400 Ws</span></div>
      <input type="range" id="main-pulse-slider" min="50" max="5000" value="400" step="50" oninput="updateSliderVal('main-pulse')">
    </div>
    <button id="spot-btn" onclick="sendSpotCommand()">SPOT</button>
    <div id="status-box">STATUS: IDLE</div>
    <div id="feedback-section" class="feedback-buttons">
        <button id="weak-btn" onclick="sendFeedback('weak')">Kurang Kuat 👎</button>
        <button id="ok-btn" onclick="sendFeedback('ok')">Hasil OK 👍 (Kunci)</button>
    </div>
  </div>
  <footer><a href="/update">Firmware Update</a></footer>

<script>
  var websocket;
  function initWebSocket() {
    websocket = new WebSocket('ws://' + window.location.hostname + '/ws');
    websocket.onopen = ()=>{ console.log('WS Opened'); };
    websocket.onclose = ()=>{ console.log('WS Closed'); setTimeout(initWebSocket, 2000); };
    websocket.onmessage = (event)=>{
      var data = JSON.parse(event.data);
      if (data.status) { document.getElementById('status-box').innerText = 'STATUS: ' + data.status; }
      if (data.vrms != null) { document.getElementById('vrms').innerText = data.vrms.toFixed(1); }
      if (data.irms != null) { document.getElementById('irms').innerText = data.irms.toFixed(2); }
      if (data.suggested_energy != null) {
        var slider = document.getElementById('main-pulse-slider');
        slider.value = data.suggested_energy;
        updateSliderVal('main-pulse');
      }
      if (data.locked_energy > 0) {
        document.getElementById('ok-btn').style.backgroundColor = '#f1c40f';
        document.getElementById('ok-btn').innerText = 'Terkunci: ' + data.locked_energy + ' Ws';
      } else {
        document.getElementById('ok-btn').style.backgroundColor = '#2ecc71';
        document.getElementById('ok-btn').innerText = 'Hasil OK 👍 (Kunci)';
      }
    };
  }

  function updateSliderVal(id) {
    var mode = document.querySelector('input[name="weld-mode"]:checked').value;
    var slider = document.getElementById(id + '-slider');
    var valSpan = document.getElementById(id + '-val');
    var unit = (id === 'main-pulse' && mode === 'smart') ? ' Ws' : ' ms';
    valSpan.innerText = slider.value + unit;
  }

  function updateUI() {
    var mode = document.querySelector('input[name="weld-mode"]:checked').value;
    document.getElementById('pre-pulse-container').classList.add('hidden');
    document.getElementById('gap-container').classList.add('hidden');
    document.getElementById('feedback-section').classList.add('hidden');
    
    var mainLabel = document.getElementById('main-label');
    var mainSlider = document.getElementById('main-pulse-slider');
    
    if (mode === 'double') {
      document.getElementById('pre-pulse-container').classList.remove('hidden');
      document.getElementById('gap-container').classList.remove('hidden');
    }
    
    if (mode === 'smart') {
      mainLabel.innerText = 'Energy';
      mainSlider.min = 50; mainSlider.max = 5000; mainSlider.step = 50;
      if (mainSlider.value > 5000) mainSlider.value = 5000;
      document.getElementById('feedback-section').classList.remove('hidden');
    } else {
      mainLabel.innerText = 'Main Pulse';
      mainSlider.min = 1; mainSlider.max = 500; mainSlider.step = 1;
       if (mainSlider.value > 500) mainSlider.value = 500;
    }
    updateSliderVal('main-pulse');
  }

  function sendSpotCommand() {
    var data = {
      action: "spot",
      mode: document.querySelector('input[name="weld-mode"]:checked').value,
      pre: parseInt(document.getElementById('pre-pulse-slider').value),
      gap: parseInt(document.getElementById('gap-slider').value),
      main: parseInt(document.getElementById('main-pulse-slider').value)
    };
    websocket.send(JSON.stringify(data));
  }

  function sendFeedback(feedbackType){
    var action = (feedbackType === 'weak') ? 'feedback_weak' : 'feedback_ok';
    websocket.send(JSON.stringify({ action: action }));
  }

  window.onload = function(event) {
    initWebSocket();
    updateUI();
    ['pre-pulse', 'gap', 'main-pulse'].forEach(id => updateSliderVal(id));
  }
</script>
</body>
</html>
)rawliteral";

// ====================================================================================
// == AKHIR DARI HTML - SEMUA KODE C++ HARUS BERADA DI BAWAH GARIS INI ==
// ====================================================================================

// --- Deklarasi Fungsi (PENTING untuk menghindari error "not declared in this scope") ---
void handleSpotCommand(JsonObject doc);

// --- Fungsi Logika Spot Welder ---
void doWeldPulse(int duration_ms) {
  ws.textAll("{\"status\":\"WELDING...\"}");
  digitalWrite(WELD_PIN, HIGH);
  delay(duration_ms);
  digitalWrite(WELD_PIN, LOW);
}

void doEnergyPulse(float target_energy_ws) {
  ws.textAll("{\"status\":\"SMART WELDING...\"}");
  
  float cumulative_energy = 0;
  unsigned long pulse_start_time = millis();

  digitalWrite(WELD_PIN, HIGH);

  while (cumulative_energy < target_energy_ws && (millis() - pulse_start_time) < MAX_PULSE_SAFETY_LIMIT_MS) {
    emon.calcVI(1, 100); // Lakukan pembacaan cepat
    float power = emon.Vrms * emon.Irms;
    unsigned long current_time = millis();
    float time_elapsed_seconds = (current_time - pulse_start_time) / 1000.0;
    cumulative_energy = power * time_elapsed_seconds;
  }

  digitalWrite(WELD_PIN, LOW);
}

void handleSpotCommand(JsonObject doc) {
  String mode = doc["mode"];
  
  if (mode == "single") { doWeldPulse(doc["main"]); } 
  else if (mode == "double") { doWeldPulse(doc["pre"]); delay(doc["gap"]); doWeldPulse(doc["main"]); }
  else if (mode == "smart") {
    float energy = (locked_energy > 0) ? locked_energy : suggested_energy;
    doEnergyPulse(energy); 
  }

  emon.calcVI(20, 2000); 
  String json_string = "{\"status\":\"IDLE\", \"vrms\":" + String(emon.Vrms) + ", \"irms\":" + String(emon.Irms) + "}";
  ws.textAll(json_string);
}

void sendCurrentState() {
    String json_string = "{\"suggested_energy\":" + String(suggested_energy) + ", \"locked_energy\":" + String(locked_energy) + "}";
    ws.textAll(json_string);
}

// --- Fungsi Handler WebSocket ---
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WS client #%u connected\n", client->id());
    sendCurrentState(); // Kirim state saat ini ke client baru
  } 
  else if (type == WS_EVT_DISCONNECT) { Serial.printf("WS client #%u disconnected\n", client->id()); }
  else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      data[len] = 0;
      JsonDocument doc;
      if (deserializeJson(doc, (char*)data) == DeserializationError::Ok) {
        const char* action = doc["action"];
        if (strcmp(action, "spot") == 0) {
          handleSpotCommand(doc.as<JsonObject>());
        } else if (strcmp(action, "feedback_weak") == 0) {
          locked_energy = 0; // Buka kunci jika ada feedback baru
          suggested_energy += ENERGY_ADJUST_STEP;
          if (suggested_energy > 5000) suggested_energy = 5000;
          sendCurrentState();
        } else if (strcmp(action, "feedback_ok") == 0) {
          locked_energy = suggested_energy;
          sendCurrentState();
        }
      }
    }
  }
}

// --- Fungsi Setup & Loop ---
void setup() {
  Serial.begin(115200);
  pinMode(WELD_PIN, OUTPUT);
  digitalWrite(WELD_PIN, LOW);
  pinMode(AUTOSPOT_PIN, INPUT_PULLUP); // Set pin autospot

  emon.voltage(VOLTAGE_PIN, 234.26, 1.7); // WAJIB DIKALIBRASI
  emon.current(CURRENT_PIN, 30); // WAJIB DIKALIBRASI (30 untuk ACS712-30A)

  WiFi.softAP(ssid, password);
  Serial.print("AP IP address: "); Serial.println(WiFi.softAPIP());

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", index_html); });
  ElegantOTA.begin(&server);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  // Cek untuk Autospot
  if (digitalRead(AUTOSPOT_PIN) == LOW) {
    JsonDocument doc; // Buat JSON doc sementara
    String current_mode_on_web = "smart"; // Ganti ini jika Anda ingin autospot mengikuti mode di web
    
    // Kita perlu tahu pengaturan terakhir dari web, tapi untuk simple autospot, kita bisa pakai nilai terkunci/saran
    doc["mode"] = current_mode_on_web;
    
    handleSpotCommand(doc.as<JsonObject>());
    delay(500); // Debounce agar tidak ter-trigger berkali-kali saat saklar ditekan lama
  }
  
  // Kirim data sensor ke client setiap interval waktu tertentu
  if (millis() - last_sensor_read > SENSOR_READ_INTERVAL) {
    last_sensor_read = millis();
    emon.calcVI(20, 2000); 
    String json_string = "{\"vrms\":" + String(emon.Vrms) + ", \"irms\":" + String(emon.Irms) + "}";
    ws.textAll(json_string);
  }
}

