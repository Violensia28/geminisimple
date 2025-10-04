// WAJIB DI BARIS PALING ATAS
#define ELEGANTOTA_USE_ASYNC_WEBSERVER 1

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <ArduinoJson.h>

// --- KONFIGURASI ---
const char* ssid = "MOTsmart SimpleWeld";
const char* password = "password123";
const int WELD_PIN = 23; // Ganti dengan pin GPIO yang Anda gunakan untuk trigger relay/SSR

// --- Objek Global ---
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

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
    .slider-container { margin: 20px 0; }
    .slider-label { display: flex; justify-content: space-between; font-size: 1.1rem; margin-bottom: 10px; }
    input[type=range] { width: 100%; -webkit-appearance: none; height: 10px; background: var(--btn-color); border-radius: 5px; outline: none; }
    input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 25px; height: 25px; background: var(--accent-color); cursor: pointer; border-radius: 50%; }
    #spot-btn { background-color: var(--accent-color); color: var(--bg-color); font-size: 1.5rem; font-weight: bold; padding: 15px 30px; border: none; border-radius: 5px; cursor: pointer; width: 100%; transition: background-color 0.3s; }
    #spot-btn:active { background-color: #008a9a; }
    #status-box { background-color: var(--bg-color); padding: 15px; margin-top: 20px; border-radius: 5px; font-size: 1.5rem; font-weight: bold; color: #7f8c8d; }
    .mode-selector { display: flex; justify-content: space-around; background-color: var(--btn-color); border-radius: 5px; padding: 5px; margin-bottom: 20px; }
    .mode-selector label { padding: 10px; cursor: pointer; flex-grow: 1; border-radius: 5px; }
    .mode-selector input { display: none; }
    .mode-selector input:checked + label { background-color: var(--accent-color); color: var(--bg-color); }
    .hidden { display: none; }
    footer a { color: #555; text-decoration: none; font-size: 0.8rem; }
  </style>
</head>
<body>
  <div class="card">
    <h1>MOTsmart SimpleWeld</h1>

    <div class="mode-selector">
      <input type="radio" id="mode-single" name="weld-mode" value="single" checked onchange="updateUI()">
      <label for="mode-single">Single</label>
      <input type="radio" id="mode-double" name="weld-mode" value="double" onchange="updateUI()">
      <label for="mode-double">Double</label>
      <input type="radio" id="mode-triple" name="weld-mode" value="triple" onchange="updateUI()">
      <label for="mode-triple">Triple</label>
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
      <div class="slider-label"><span>Main Pulse</span><span id="main-pulse-val">80 ms</span></div>
      <input type="range" id="main-pulse-slider" min="1" max="500" value="80" oninput="updateSliderVal('main-pulse')">
    </div>

    <button id="spot-btn" onclick="sendSpotCommand()">SPOT</button>
    <div id="status-box">STATUS: IDLE</div>
  </div>
  <footer><a href="/update">Firmware Update</a></footer>

<script>
  var websocket;

  function initWebSocket() {
    console.log('Trying to open a WebSocket connection...');
    websocket = new WebSocket('ws://' + window.location.hostname + '/ws');
    websocket.onopen = onOpen;
    websocket.onclose = onClose;
    websocket.onmessage = onMessage;
  }

  function onOpen(event) { console.log('Connection opened'); }
  function onClose(event) { console.log('Connection closed'); setTimeout(initWebSocket, 2000); }

  function onMessage(event) {
    console.log('Received: ' + event.data);
    var data = JSON.parse(event.data);
    if (data.status) {
      document.getElementById('status-box').innerText = 'STATUS: ' + data.status;
    }
  }

  function updateSliderVal(id) {
    var slider = document.getElementById(id + '-slider');
    var valSpan = document.getElementById(id + '-val');
    valSpan.innerText = slider.value + ' ms';
  }

  function updateUI() {
    var mode = document.querySelector('input[name="weld-mode"]:checked').value;
    var prePulseContainer = document.getElementById('pre-pulse-container');
    var gapContainer = document.getElementById('gap-container');

    if (mode === 'single') {
      prePulseContainer.classList.add('hidden');
      gapContainer.classList.add('hidden');
    } else {
      prePulseContainer.classList.remove('hidden');
      gapContainer.classList.remove('hidden');
    }
  }

  function sendSpotCommand() {
    var mode = document.querySelector('input[name="weld-mode"]:checked').value;
    var pre = document.getElementById('pre-pulse-slider').value;
    var gap = document.getElementById('gap-slider').value;
    var main = document.getElementById('main-pulse-slider').value;

    var data = {
      action: "spot",
      mode: mode,
      pre: parseInt(pre),
      gap: parseInt(gap),
      main: parseInt(main)
    };
    
    console.log('Sending: ' + JSON.stringify(data));
    websocket.send(JSON.stringify(data));
  }

  window.onload = function(event) {
    initWebSocket();
    updateUI();
    updateSliderVal('pre-pulse');
    updateSliderVal('gap');
    updateSliderVal('main-pulse');
  }
</script>
</body>
</html>
)rawliteral";


// --- Fungsi Logika Spot Welder ---

void doWeldPulse(int duration_ms) {
  ws.textAll("{\"status\":\"WELDING...\"}"); // Kirim status ke semua client
  digitalWrite(WELD_PIN, HIGH);
  delay(duration_ms);
  digitalWrite(WELD_PIN, LOW);
}

void handleSpotCommand(JsonObject doc) {
  String mode = doc["mode"];
  int pre = doc["pre"];
  int gap = doc["gap"];
  int main = doc["main"];

  if (mode == "single") {
    doWeldPulse(main);
  } 
  else if (mode == "double") {
    doWeldPulse(pre);
    delay(gap);
    doWeldPulse(main);
  }
  else if (mode == "triple") {
    doWeldPulse(pre);
    delay(gap);
    doWeldPulse(main);
    delay(gap);
    doWeldPulse(main);
  }

  ws.textAll("{\"status\":\"IDLE\"}"); // Kembalikan status ke IDLE
}


// --- Fungsi Handler WebSocket ---

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    client->text("{\"status\":\"CONNECTED\"}");
  } 
  else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
  }
  else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      data[len] = 0;
      Serial.printf("Received WebSocket data: %s\n", (char*)data);
      
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, (char*)data);
      
      if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.c_str());
        return;
      }

      const char* action = doc["action"];
      if (strcmp(action, "spot") == 0) {
        handleSpotCommand(doc.as<JsonObject>());
      }
    }
  }
}

// --- Fungsi Setup & Loop ---

void setup() {
  Serial.begin(115200);
  pinMode(WELD_PIN, OUTPUT);
  digitalWrite(WELD_PIN, LOW);

  Serial.println("Booting...");
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  // Daftarkan handler WebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Route untuk halaman utama
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  // Mulai ElegantOTA
  ElegantOTA.begin(&server);

  // Mulai server
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  // Kosongkan, semua berjalan secara event-based
}
