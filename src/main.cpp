#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>

// Ganti dengan nama WiFi dan password yang Anda inginkan
const char* ssid = "MOTsmart SimpleWeld";
const char* password = "password123";

// Buat instance AsyncWebServer pada port 80
AsyncWebServer server(80);

// HTML sederhana untuk halaman utama
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>MOTsmart SimpleWeld</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; text-align: center; background-color: #222; color: white; }
    h1 { color: #00bcd4; }
    p { font-size: 1.2rem; }
    a { color: #00bcd4; text-decoration: none; font-size: 1.2rem; border: 1px solid #00bcd4; padding: 10px 20px; border-radius: 5px; }
    a:hover { background-color: #00bcd4; color: #222; }
  </style>
</head>
<body>
  <h1>MOTsmart SimpleWeld</h1>
  <p>Firmware OTA Updater Ready.</p>
  <br><br>
  <a href="/update">Go to Update Page</a>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  Serial.println("Booting...");

  // Membuat WiFi Access Point
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  // Route untuk halaman utama (root URL)
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
  // Kosongkan untuk mode Async
}
