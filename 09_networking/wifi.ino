#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ESP32Servo.h>

// ---- user settings ----
const char* WIFI_SSID = "MAKERSPACE";
const char* WIFI_PASS = "12345678";
const int   SERVO_PIN = 14;
// -----------------------

WebServer server(80);
WebSocketsServer ws(81);
Servo servo1;
int angleDeg = 90;

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Servo</title>
<style>body{font-family:system-ui;margin:2rem}input{width:100%}</style>
</head><body>
<h1>Servo</h1>
<input id="s" type="range" min="0" max="180" value="90">
<p id="v">90°</p>
<script>
const s=document.getElementById('s'), v=document.getElementById('v');
const ws=new WebSocket(`ws://${location.hostname}:81/`);
s.oninput=()=>{ v.textContent=s.value+'°'; if(ws.readyState===1) ws.send('ANGLE:'+s.value); };
</script>
</body></html>
)HTML";

void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }

void onWs(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
  if (type == WStype_TEXT) {
    String msg((char*)payload, len);
    if (msg.startsWith("ANGLE:")) {
      int v = msg.substring(6).toInt();
      v = constrain(v, 0, 180);
      angleDeg = v;
      servo1.write(angleDeg);
    }
  }
}

void connectWiFiOrAP() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Connecting to %s ...\n", WIFI_SSID);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { // ~20s
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Connected: %s (%s)\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
    Serial.printf("Password: %s\n", WIFI_PASS);
    Serial.printf("Open  http://%s/   WS ws://%s:81/\n",
                  WiFi.localIP().toString().c_str(), WiFi.localIP().toString().c_str());
  } else {
    Serial.println("STA failed. Starting AP...");
    WiFi.mode(WIFI_AP);
    const char* AP_SSID = "ESP32-Servo";
    const char* AP_PASS = "12345678";
    WiFi.softAP(AP_SSID, AP_PASS);
    delay(200);
    IPAddress ip = WiFi.softAPIP();
    Serial.printf("AP: %s  IP: %s  pass: %s\n", AP_SSID, ip.toString().c_str(), AP_PASS);
    Serial.printf("Open  http://%s/   WS ws://%s:81/\n", ip.toString().c_str(), ip.toString().c_str());
  }
}

void setup() {
  Serial.begin(9600);
  delay(150);

  connectWiFiOrAP();

  servo1.setPeriodHertz(50);
  servo1.attach(SERVO_PIN, 500, 2400);
  servo1.write(angleDeg);

  server.on("/", handleRoot);
  server.begin();

  ws.begin();
  ws.onEvent(onWs);

  Serial.println("HTTP :80 and WS :81 running.");
}

void loop() {
  server.handleClient();
  ws.loop();
}
