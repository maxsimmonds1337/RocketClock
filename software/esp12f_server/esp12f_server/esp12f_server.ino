#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>

const char* SSID     = "YOUR_SSID";
const char* PASSWORD = "YOUR_PASSWORD";

ESP8266WebServer server(80);

void setup() {
  Serial.begin(115200);

  LittleFS.begin();

  WiFi.begin(SSID, PASSWORD);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected: " + WiFi.localIP().toString());

  // Serve static files from LittleFS (UI)
  server.serveStatic("/", LittleFS, "/index.html");

  // TODO: add API routes here, e.g.:
  // server.on("/mode", HTTP_POST, handleSetMode);
  // server.on("/text", HTTP_POST, handleSetText);
  // server.on("/alarm", HTTP_POST, handleSetAlarm);

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
}
