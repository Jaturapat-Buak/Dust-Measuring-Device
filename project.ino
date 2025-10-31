#include <WiFi.h>
#include <HTTPClient.h>
#include <driver/adc.h>

// ===== WiFi =====
const char* ssid = "ssrojphan";
const char* password = "haiii1234";
const char* GAS_URL = "https://script.google.com/macros/s/AKfycbyEPWSO9jlkaBM9hs4wEK4ctsdoj4kJaihP3s8cDLF-UDXqka9AUj1FxZgkIARBkdij/exec";

// ===== Sensor Pin =====
const int MEASURE_PIN = 34;   // AOUT (เหลือง)
const int LED_PIN     = 4;    // LED control (น้ำเงิน)

// ===== Timing =====
const int T_SAMPLE = 280;
const int T_DELTA  = 40;
const int T_SLEEP  = 9680;

// ===== Calibration =====
const float R_SCALE = 220.0 / 150.0;
const float EMA_ALPHA = 0.25;
const float BASELINE_ALPHA = 0.01;

// ===== Global Vars =====
float vBaseline = NAN;
float emaDustUg = NAN;

// ----------------------
// อ่านค่าแรงดัน 1 ครั้ง
float readOnceVolt() {
  digitalWrite(LED_PIN, LOW);
  delayMicroseconds(T_SAMPLE);
  int raw = analogRead(MEASURE_PIN);
  delayMicroseconds(T_DELTA);
  digitalWrite(LED_PIN, HIGH);
  delayMicroseconds(T_SLEEP);
  return raw * (3.3f / 4095.0f);
}

// หาค่ามัธยฐานจาก 5 ค่า
float median5(float a, float b, float c, float d, float e) {
  float x[5] = {a, b, c, d, e};
  for (int i = 1; i < 5; i++) {
    float k = x[i];
    int j = i - 1;
    while (j >= 0 && x[j] > k) {
      x[j + 1] = x[j];
      j--;
    }
    x[j + 1] = k;
  }
  return x[2];
}

// อ่านและกรองค่า Dust (µg/m³)
float readFilteredDustUg() {
  const int N = 10;
  float sumV = 0;
  for (int i = 0; i < N; ++i) {
    float m = median5(readOnceVolt(), readOnceVolt(), readOnceVolt(),
                      readOnceVolt(), readOnceVolt());
    float vDelta = max(m - vBaseline, 0.0f);
    sumV += vDelta * R_SCALE;
  }

  float vNorm = sumV / N;
  float dust_ug = max(0.17f * vNorm * 1000.0f, 0.0f);

  if (dust_ug < 15.0f)
    vBaseline = (1 - BASELINE_ALPHA) * vBaseline + BASELINE_ALPHA * (vBaseline + vNorm / R_SCALE);

  emaDustUg = isnan(emaDustUg) ? dust_ug : EMA_ALPHA * dust_ug + (1 - EMA_ALPHA) * emaDustUg;
  return emaDustUg;
}

// ===== ส่งค่าไป Google Sheet =====
void sendToSheet(float pm25, float pm10) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(GAS_URL);
  http.addHeader("Content-Type", "application/json");
  String payload = "{\"pm25\":" + String(pm25, 1) + ",\"pm10\":" + String(pm10, 1) + "}";
  int code = http.POST(payload);
  Serial.printf("Send: %s → Response %d\n", payload.c_str(), code);
  http.end();
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  analogSetWidth(12);
  analogSetAttenuation(ADC_11db);

  Serial.print("Connecting WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());

  // คาลิเบรต baseline
  delay(500);
  float acc = 0;
  for (int i = 0; i < 200; i++) acc += readOnceVolt();
  vBaseline = acc / 200.0;
  Serial.printf("Baseline Vout ~ %.3f V\n", vBaseline);
}

// ===== LOOP =====
void loop() {
  float pm = readFilteredDustUg();
  float PM25 = pm * 0.6;
  float PM10 = PM25 * 1.1;
  sendToSheet(PM25, PM10);
  delay(60000); // ส่งทุก 1 นาที
}
