// ESP32 Smart City sketch (revised pin mapping)
// Remapped pins to avoid conflicts on ESP32-CAM boards and added /alert/on endpoint.

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Preferences.h>

const char* WIFI_SSID = "Van Huy";
const char* WIFI_PASSWORD = "123456789";

// =========================
// ESP32-CAM PIN CONFIG (revised)
// =========================

// Buzzer
const int BUZZER_PIN = 25; // moved to GPIO25 (safe PWM-capable pin)

// Flash LED của ESP32-CAM
const int LED_PIN = 4; // keep on 4 (camera flash LED)

// =========================
// TB6612FNG MOTOR DRIVER
// =========================

// Motor A
const int AIN1 = 13;
const int AIN2 = 14;
const int PWMA = 15;

// Motor B (remapped to avoid pin 16 duplicate)
const int BIN1 = 27;
const int BIN2 = 26;
const int PWMB = 12;

// Standby (remapped off GPIO4)
const int STBY = 33;

// IR sensor (moved to GPIO34 input-only)
const int IR_SENSOR_PIN = 34;

// Emergency button (GPIO0 - keep but beware of boot mode if held)
const int BUTTON_PIN = 0;

const int BUZZER_CHANNEL = 0;
const int MOTOR_CHANNEL_A = 1;
const int MOTOR_CHANNEL_B = 2; 

WebServer server(80);
Preferences prefs;

enum AlertLevel {
  ALERT_NONE = 0,
  ALERT_WARNING = 1,
  ALERT_DANGER = 2,
};

struct EventEntry {
  unsigned long ts;
  String type;
  String detail;
};

static const uint8_t MAX_EVENTS = 20;
EventEntry eventLog[MAX_EVENTS];
uint8_t eventCount = 0;
uint8_t eventHead = 0;

bool vehicleRunning = true;
bool vehicleStopped = false;
bool nightMode = false;
bool alertActive = false;
AlertLevel currentAlertLevel = ALERT_NONE;

unsigned long alertStartedAt = 0;
unsigned long alertLastFiredAt = 0;
unsigned long alertCooldownMs = 5000;
unsigned long autoResumeMs = 8000;
unsigned long warningAfterMs = 1200;
unsigned long dangerAfterMs = 2500;
unsigned long alertDurationMs = 2500;

unsigned long closedDurationMs = 0;
unsigned long lastLoopTs = 0;
unsigned long lastNotificationTs = 0;

String remoteWebhookUrl = "";

void addEvent(const String& type, const String& detail) {
  eventLog[eventHead] = {millis(), type, detail};
  eventHead = (eventHead + 1) % MAX_EVENTS;
  if (eventCount < MAX_EVENTS) {
    eventCount++;
  }
}

String alertLevelToString(AlertLevel level) {
  switch (level) {
    case ALERT_WARNING: return "warning";
    case ALERT_DANGER: return "danger";
    default: return "none";
  }
}

AlertLevel stringToAlertLevel(const String& value) {
  if (value == "warning") return ALERT_WARNING;
  if (value == "danger") return ALERT_DANGER;
  return ALERT_NONE;
}

void setBuzzer(bool enabled, int frequency = 1000) {
  if (enabled) {
    ledcWriteTone(BUZZER_CHANNEL, frequency);
    ledcWrite(BUZZER_CHANNEL, 128);
  } else {
    ledcWriteTone(BUZZER_CHANNEL, 0);
    ledcWrite(BUZZER_CHANNEL, 0);
  }
}

void setVehicleMotor(bool run) {

  vehicleRunning = run;
  vehicleStopped = !run;

  if (run) {

    // Motor A
    digitalWrite(AIN1, HIGH);
    digitalWrite(AIN2, LOW);

    // Motor B
    digitalWrite(BIN1, HIGH);
    digitalWrite(BIN2, LOW);

    ledcWrite(MOTOR_CHANNEL_A, 255);
    ledcWrite(MOTOR_CHANNEL_B, 255);

  } else {

    // Stop Motor A
    digitalWrite(AIN1, LOW);
    digitalWrite(AIN2, LOW);

    // Stop Motor B
    digitalWrite(BIN1, LOW);
    digitalWrite(BIN2, LOW);

    ledcWrite(MOTOR_CHANNEL_A, 0);
    ledcWrite(MOTOR_CHANNEL_B, 0);
  }
}

void stopVehicle(const String& reason) {
  setVehicleMotor(false);
  addEvent("vehicle_stop", reason);
}

void startVehicle(const String& reason) {
  setVehicleMotor(true);
  addEvent("vehicle_start", reason);
}

void sendWebhook(const String& eventType, const String& detail) {
  if (remoteWebhookUrl.length() == 0) {
    return;
  }

  HTTPClient http;
  http.begin(remoteWebhookUrl);
  http.addHeader("Content-Type", "application/json");

  String body = "{";
  body += "\"device\":\"ESP32\",";
  body += "\"event\":\"" + eventType + "\",";
  body += "\"detail\":\"" + detail + "\",";
  body += "\"alertLevel\":\"" + alertLevelToString(currentAlertLevel) + "\",";
  body += "\"vehicleRunning\":" + String(vehicleRunning ? "true" : "false");
  body += "}";

  http.POST(body);
  http.end();
}

void triggerAlert(AlertLevel level, const String& source) {
  unsigned long now = millis();
  if (now - alertLastFiredAt < alertCooldownMs) {
    return;
  }

  currentAlertLevel = level;
  alertActive = true;
  alertStartedAt = now;
  alertLastFiredAt = now;

  if (level == ALERT_WARNING) {
    digitalWrite(LED_PIN, HIGH);
    setBuzzer(true, nightMode ? 800 : 1000);
  } else if (level == ALERT_DANGER) {

    // bật LED
    digitalWrite(LED_PIN, HIGH);

    // dừng xe
    stopVehicle("drowsiness");

    // còi hú cảnh báo nguy hiểm
    for (int i = 0; i < 5; i++) {

      setBuzzer(true, 2000);
      delay(200);

      setBuzzer(false);
      delay(150);
    }
  }

  addEvent("alert", source + ":" + alertLevelToString(level));
  sendWebhook("alert", source);
  lastNotificationTs = now;
}

void clearAlert(const String& reason) {
  alertActive = false;
  currentAlertLevel = ALERT_NONE;
  digitalWrite(LED_PIN, LOW);
  setBuzzer(false);
  addEvent("alert_clear", reason);
}

bool readIgnitionSensor() {
  return digitalRead(BUTTON_PIN) == HIGH;
}

bool readObstacleSensor() {
  // Low means obstacle detected on many IR modules; adjust to your hardware.
  return digitalRead(IR_SENSOR_PIN) == LOW;
}

void updateAntiFalseAlarm(unsigned long dt) {
  bool closedDetected = readObstacleSensor();
  if (closedDetected) {
    closedDurationMs += dt;
  } else {
    if (closedDurationMs > dt * 2) {
      closedDurationMs -= dt * 2;
    } else {
      closedDurationMs = 0;
    }
  }

  if (closedDurationMs >= dangerAfterMs) {
    triggerAlert(ALERT_DANGER, "sustained_close");
  } else if (closedDurationMs >= warningAfterMs) {
    triggerAlert(ALERT_WARNING, "short_close");
  }
}

String escapeJson(const String& value) {
  String out;
  for (size_t i = 0; i < value.length(); i++) {
    char c = value[i];
    if (c == '"' || c == '\\') {
      out += '\\';
    }
    out += c;
  }
  return out;
}

String buildStatusJson() {
  String json = "{";
  json += "\"success\":true,";
  json += "\"alertActive\":" + String(alertActive ? "true" : "false") + ",";
  json += "\"alertLevel\":\"" + alertLevelToString(currentAlertLevel) + "\",";
  json += "\"vehicleRunning\":" + String(vehicleRunning ? "true" : "false") + ",";
  json += "\"vehicleStopped\":" + String(vehicleStopped ? "true" : "false") + ",";
  json += "\"nightMode\":" + String(nightMode ? "true" : "false") + ",";
  json += "\"closedDurationMs\":" + String(closedDurationMs) + ",";
  json += "\"uptimeMs\":" + String(millis()) + ",";
  json += "\"wifi\":\"" + escapeJson(WiFi.isConnected() ? WiFi.localIP().toString() : String("disconnected")) + "\"";
  json += "}";
  return json;
}

String buildEventsJson() {
  String json = "[";
  for (uint8_t i = 0; i < eventCount; i++) {
    uint8_t index = (eventHead + MAX_EVENTS - eventCount + i) % MAX_EVENTS;
    const EventEntry& entry = eventLog[index];
    if (i > 0) json += ",";
    json += "{";
    json += "\"ts\":" + String(entry.ts) + ",";
    json += "\"type\":\"" + escapeJson(entry.type) + "\",";
    json += "\"detail\":\"" + escapeJson(entry.detail) + "\"";
    json += "}";
  }
  json += "]";
  return json;
}

String dashboardHtml() {
  String html;
  html += "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>ESP32 Smart City Dashboard</title>";
  html += "<style>body{font-family:Arial,sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:20px} .card{background:#111827;border:1px solid #334155;border-radius:14px;padding:16px;margin-bottom:14px} button{padding:10px 14px;border:0;border-radius:10px;margin:4px;cursor:pointer} .ok{background:#22c55e} .warn{background:#f59e0b} .danger{background:#ef4444} .muted{color:#94a3b8} pre{white-space:pre-wrap;word-break:break-word}</style>";
  html += "</head><body>";
  html += "<h2>ESP32 Smart City Drowsiness</h2>";
  html += "<div class='card'><div id='status'>Loading...</div><div class='muted'>This dashboard works directly on the ESP32.</div></div>";
  html += "<div class='card'>";
  html += "<button class='ok' onclick=\"fetch('/vehicle/start')\">Start Vehicle</button>";
  html += "<button class='danger' onclick=\"fetch('/vehicle/stop')\">Stop Vehicle</button>";
  html += "<button class='warn' onclick=\"fetch('/alert/warning')\">Test Warning</button>";
  html += "<button class='danger' onclick=\"fetch('/alert/danger')\">Test Danger</button>";
  html += "<button onclick=\"fetch('/night/toggle')\">Toggle Night Mode</button>";
  html += "</div>";
  html += "<div class='card'><h3>Recent Events</h3><pre id='events'>...</pre></div>";
  html += "<script>async function refresh(){let s=await fetch('/status').then(r=>r.json());document.getElementById('status').innerHTML='Vehicle: <b>'+ (s.vehicleRunning?'RUNNING':'STOPPED') +'</b> | Alert: <b>'+s.alertLevel+'</b> | Night: <b>'+s.nightMode+'</b> | Closed ms: <b>'+s.closedDurationMs+'</b>';let e=await fetch('/events').then(r=>r.json());document.getElementById('events').textContent=JSON.stringify(e,null,2);} setInterval(refresh,1500); refresh();</script>";
  html += "</body></html>";
  return html;
}

void handleRoot() {
  server.send(200, "text/html", dashboardHtml());
}

void handleStatus() {
  server.send(200, "application/json", buildStatusJson());
}

void handleEvents() {
  server.send(200, "application/json", buildEventsJson());
}

void handleAlertWarning() {
  triggerAlert(ALERT_WARNING, "manual_test");
  server.send(200, "application/json", "{\"success\":true,\"alert\":\"warning\"}");
}

void handleAlertDanger() {
  triggerAlert(ALERT_DANGER, "manual_test");
  server.send(200, "application/json", "{\"success\":true,\"alert\":\"danger\"}");
}

// Compatibility: map /alert/on to danger (used as legacy path)
void handleAlertOn() {
  triggerAlert(ALERT_DANGER, "legacy_on");
  server.send(200, "application/json", "{\"success\":true,\"alert\":\"danger\"}");
}

void handleAlertOff() {
  clearAlert("manual_clear");
  server.send(200, "application/json", "{\"success\":true,\"alert\":\"off\"}");
}

void handleVehicleStop() {
  stopVehicle("manual_stop");
  server.send(200, "application/json", "{\"success\":true,\"vehicle\":\"stopped\"}");
}

void handleVehicleStart() {
  startVehicle("manual_start");
  server.send(200, "application/json", "{\"success\":true,\"vehicle\":\"running\"}");
}

void handleNightToggle() {
  nightMode = !nightMode;
  prefs.putBool("nightMode", nightMode);
  addEvent("night_mode", nightMode ? "on" : "off");
  server.send(200, "application/json", String("{\"success\":true,\"nightMode\":") + (nightMode ? "true" : "false") + "}");
}

void handleConfig() {
  if (server.hasArg("warningMs")) warningAfterMs = server.arg("warningMs").toInt();
  if (server.hasArg("dangerMs")) dangerAfterMs = server.arg("dangerMs").toInt();
  if (server.hasArg("cooldownMs")) alertCooldownMs = server.arg("cooldownMs").toInt();
  if (server.hasArg("resumeMs")) autoResumeMs = server.arg("resumeMs").toInt();
  if (server.hasArg("webhook")) remoteWebhookUrl = server.arg("webhook");
  prefs.putUInt("warningMs", warningAfterMs);
  prefs.putUInt("dangerMs", dangerAfterMs);
  prefs.putUInt("cooldownMs", alertCooldownMs);
  prefs.putUInt("resumeMs", autoResumeMs);
  prefs.putString("webhook", remoteWebhookUrl);
  addEvent("config", "updated");
  server.send(200, "application/json", buildStatusJson());
}

void handleWebhookTest() {
  sendWebhook("test", "webhook_test");
  server.send(200, "application/json", "{\"success\":true,\"event\":\"webhook_test_sent\"}");
}

void loadSettings() {
  prefs.begin("smartcity", false);
  nightMode = prefs.getBool("nightMode", false);
  warningAfterMs = prefs.getUInt("warningMs", warningAfterMs);
  dangerAfterMs = prefs.getUInt("dangerMs", dangerAfterMs);
  alertCooldownMs = prefs.getUInt("cooldownMs", alertCooldownMs);
  autoResumeMs = prefs.getUInt("resumeMs", autoResumeMs);
  remoteWebhookUrl = prefs.getString("webhook", "");
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
 // TB6612FNG

pinMode(AIN1, OUTPUT);
pinMode(AIN2, OUTPUT);
pinMode(PWMA, OUTPUT);

pinMode(BIN1, OUTPUT);
pinMode(BIN2, OUTPUT);
pinMode(PWMB, OUTPUT);

pinMode(STBY, OUTPUT);

// bật driver
digitalWrite(STBY, HIGH);
  pinMode(IR_SENSOR_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

 ledcSetup(BUZZER_CHANNEL, 2000, 8);
ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);

// PWM Motor A
ledcSetup(MOTOR_CHANNEL_A, 1000, 8);
ledcAttachPin(PWMA, MOTOR_CHANNEL_A);

// PWM Motor B
ledcSetup(MOTOR_CHANNEL_B, 1000, 8);
ledcAttachPin(PWMB, MOTOR_CHANNEL_B);

  loadSettings();
  setVehicleMotor(true);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }

  Serial.println();
  Serial.print("Connected. IP: ");
  Serial.println(WiFi.localIP());

  addEvent("boot", WiFi.localIP().toString());

  server.on("/", handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/events", HTTP_GET, handleEvents);
  server.on("/alert/warning", HTTP_GET, handleAlertWarning);
  server.on("/alert/danger", HTTP_GET, handleAlertDanger);
  server.on("/alert/on", HTTP_GET, handleAlertOn); // legacy compatibility
  server.on("/alert/off", HTTP_GET, handleAlertOff);
  server.on("/vehicle/stop", HTTP_GET, handleVehicleStop);
  server.on("/vehicle/start", HTTP_GET, handleVehicleStart);
  server.on("/night/toggle", HTTP_GET, handleNightToggle);
  server.on("/config", HTTP_POST, handleConfig);
  server.on("/webhook/test", HTTP_GET, handleWebhookTest);
  server.begin();
}

void loop() {
  server.handleClient();

  unsigned long now = millis();
  unsigned long dt = (lastLoopTs == 0) ? 0 : (now - lastLoopTs);
  lastLoopTs = now;

  updateAntiFalseAlarm(dt);

  if (alertActive && (now - alertStartedAt >= alertDurationMs)) {
    clearAlert("timeout");
  }

  if (vehicleStopped && !alertActive && (now - alertLastFiredAt >= autoResumeMs)) {
    startVehicle("auto_resume");
  }

  if (digitalRead(BUTTON_PIN) == LOW) {
    // Button pressed: immediate emergency stop.
    stopVehicle("button");
    triggerAlert(ALERT_DANGER, "button");
  }
}
