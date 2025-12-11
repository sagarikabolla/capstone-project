// ESP32 SEN-HZ21WA monitor - Web UI (uses WebServer)
// Configure: adjust SSID/PASS below

#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "Galaxy F34 5G 14FB";
const char* password = "Sagarika@2003";

const int FLOW_PIN = 25;            // GPIO pin connected to sensor Yellow
volatile unsigned long pulseCount = 0;
volatile unsigned long lastPulseMicros = 0;

// Settings
const unsigned long MIN_PULSE_INTERVAL_US = 300UL; // ignore pulses closer than this (tune)
const float CALIBRATION = 7.5f;    // starting value; calibrate with known volume
const unsigned long MEASURE_MS = 1000UL; // measurement interval
bool USE_INTERNAL_PULLUP = false;  // set true if you do NOT add external 10k to 3.3V

WebServer server(80);

void IRAM_ATTR onPulse() {
  unsigned long now = micros();
  if (now - lastPulseMicros >= MIN_PULSE_INTERVAL_US) {
    pulseCount++;
    lastPulseMicros = now;
  }
}

String buildHtml(float flow, double total) {
  String html = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>Water Footprint</title>";
  html += "<style>body{font-family:Arial;text-align:center;background:#0b1220;color:#fff;padding:20px} .card{display:inline-block;padding:18px;border-radius:12px;background:linear-gradient(135deg,#1e3c72,#2a5298);min-width:260px} .val{font-size:28px;font-weight:700;margin:8px 0}</style></head><body>";
  html += "<div class='card'><h2>💧 Water Footprint</h2>";
  html += "<div>Flow</div><div class='val'>" + String(flow,2) + " L/min</div>";
  html += "<div>Total</div><div class='val'>" + String(total,3) + " L</div>";
  html += "<div style='font-size:12px;margin-top:8px'>IP: " + WiFi.localIP().toString() + "</div>";
  html += "<script>setInterval(()=>fetch('/data').then(r=>r.json()).then(j=>{document.querySelectorAll('.val')[0].innerText = j.flow.toFixed(2)+' L/min'; document.querySelectorAll('.val')[1].innerText = j.total.toFixed(3)+' L';}),1500);</script>";
  html += "</div></body></html>";
  return html;
}

void handleRoot() {
  // compute values for display on request (we store latest measured in globals)
  extern float lastFlowRate;
  extern double lastTotalLiters;
  server.send(200, "text/html", buildHtml(lastFlowRate, lastTotalLiters));
}

void handleData() {
  extern float lastFlowRate;
  extern double lastTotalLiters;
  String json = "{\"flow\":" + String(lastFlowRate,4) + ",\"total\":" + String(lastTotalLiters,6) + "}";
  server.send(200, "application/json", json);
}

// globals for measured values
float lastFlowRate = 0.0f;
double lastTotalLiters = 0.0;

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("Starting SEN-HZ21WA monitor...");

  if (USE_INTERNAL_PULLUP) {
    pinMode(FLOW_PIN, INPUT_PULLUP);
  } else {
    pinMode(FLOW_PIN, INPUT); // expecting external 10k pull-up to 3.3V if wired
  }
  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), onPulse, RISING);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("✅ WiFi connected! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("⚠ WiFi not connected (timeout).");
  }

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.begin();
}

unsigned long lastMeasure = 0;
float smoothFlow = 0.0f;
const float SMOOTH_ALPHA = 0.3f;

void loop() {
  server.handleClient();

  unsigned long now = millis();
  if (now - lastMeasure >= MEASURE_MS) {
    noInterrupts();
    unsigned long p = pulseCount;
    pulseCount = 0;
    interrupts();

    float freq = (float)p * (1000.0f / (float)MEASURE_MS); // pulses/sec
    float flowLpm = freq / CALIBRATION; // L/min
    double litersThis = (double)flowLpm / 60.0; // L per MEASURE_MS (here per sec)
    lastTotalLiters += litersThis;

    // smoothing
    smoothFlow = (SMOOTH_ALPHA * flowLpm) + ((1.0f - SMOOTH_ALPHA) * smoothFlow);
    lastFlowRate = smoothFlow;
    lastTotalLiters = lastTotalLiters;

    Serial.print("Pulses: "); Serial.print(p);
    Serial.print("  Flow(L/min): "); Serial.print(flowLpm,3);
    Serial.print("  Smoothed: "); Serial.print(smoothFlow,3);
    Serial.print("  Total(L): "); Serial.println(lastTotalLiters,4);

    lastMeasure = now;
  }
}
