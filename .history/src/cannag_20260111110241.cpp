#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "HX711.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>

/* ================= CONFIG ================= */
constexpr int SCREEN_WIDTH = 128;
constexpr int SCREEN_HEIGHT = 64;
constexpr uint8_t OLED_ADDR = 0x3C;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

constexpr uint8_t DOUT_PIN = 25;
constexpr uint8_t CLK_PIN  = 18;
HX711 scale;

// chỉnh theo cân của bạn — có thể điều chỉnh lúc chạy
float calibrationFactor = 145000.0f;

// persistent tuning variables (can be changed via Serial)
float weightOffset = 0.0f;        // zero offset (g)
float stableThreshold = 0.5f;     // minimum change (g) to report/refresh
float emaAlpha = 0.2f;            // smoothing factor (0..1)
float smoothedWeight = 0.0f;      // EMA state
float lastReportedWeight = 0.0f;  // last weight we reported

Preferences prefs;

// WiFi
const char* WIFI_SSID = "HUNG3A";
const char* WIFI_PASS = "Nguyenmaihung1108@";

// Firebase
const char* FIREBASE_URL = "https://testnhiet28-default-rtdb.firebaseio.com/health.json";

// Intervals
constexpr unsigned long OLED_INTERVAL_MS = 300;
constexpr unsigned long FIREBASE_INTERVAL_MS = 3000;
constexpr unsigned long WIFI_RECONNECT_INTERVAL_MS = 5000;
constexpr unsigned long HX711_NOT_READY_PRINT_MS = 2000;

/* ================= STATE ================= */
unsigned long lastUpdate = 0;
unsigned long lastFirebase = 0;
unsigned long lastWifiAttempt = 0;
unsigned long lastNotReadyPrint = 0;
float lastWeight = 0.0f;

/* ================= HELPERS ================= */
void initDisplay() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED init failed");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("HX711 + Firebase");
    display.display();
  }
}

void initScale() {
  scale.begin(DOUT_PIN, CLK_PIN);
  scale.set_scale(calibrationFactor);

  Serial.println("Taring scale...");
  delay(2000);
  scale.tare();
  Serial.println("Tare done");
}

void initWiFi() {
  Serial.printf("Connecting to WiFi '%s'...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    Serial.print('.');
    delay(500);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed (will retry in loop)");
  }
}

bool sendFirebase(float weight) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Skip Firebase: WiFi not connected");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure(); // Accept any cert (for Firebase with self-hosted or test)

  HTTPClient http;
  if (!http.begin(client, FIREBASE_URL)) {
    Serial.println("HTTP begin failed");
    return false;
  }
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"weight\":" + String(weight, 1) + ",";
  json += "\"time\":" + String(millis());
  json += "}";

  int httpCode = http.PUT(json);
  if (httpCode > 0) {
    Serial.printf("Firebase -> HTTP %d\n", httpCode);
    String payload = http.getString();
    if (payload.length()) Serial.print("Payload: "), Serial.println(payload);
    http.end();
    return (httpCode >= 200 && httpCode < 300);
  } else {
    Serial.printf("Firebase error: %s (code %d)\n", http.errorToString(httpCode).c_str(), httpCode);
    http.end();
    return false;
  }
}

void updateOLED(float weight) {
  display.clearDisplay();

  // Small top line for status
  display.setTextSize(1);
  display.setCursor(0, 0);
  if (WiFi.status() == WL_CONNECTED) {
    display.print("WiFi: ");
    display.print(WiFi.localIP());
  } else {
    display.print("WiFi: -");
  }

  // Big weight
  display.setTextSize(2);
  display.setCursor(0, 18);
  display.print(weight, 1);
  display.print(" g");

  // Bottom line: calibration and settings
  display.setTextSize(1);
  display.setCursor(0, 54);
  display.print("C:"); display.print((long)calibrationFactor);
  display.print(" T:"); display.print(stableThreshold, 1);
  display.print(" A:"); display.print(emaAlpha, 2);

  display.display();
}

// Command helpers: tare/save settings/print
void tareAndSave() {
  // Use library tare to zero internal offset, and clear external offset
  scale.tare();
  weightOffset = 0.0f;
  prefs.putFloat("offset", weightOffset);
  Serial.println("Tare performed and offset saved (0)");
}

void printSettings() {
  Serial.println("--- Scale settings ---");
  Serial.print("Calibration: "); Serial.println(calibrationFactor);
  Serial.print("Offset: "); Serial.println(weightOffset);
  Serial.print("Threshold: "); Serial.println(stableThreshold);
  Serial.print("EMA alpha: "); Serial.println(emaAlpha);
  Serial.println("Type commands: 't' tare, 'c <value>' set cal, 'd <value>' set threshold, 'a <0-1>' set alpha, 'save' persist settings");
}

void handleSerialCommands() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd == "t" || cmd == "tare") {
    tareAndSave();
  } else if (cmd.startsWith("c ") || cmd.startsWith("cal ")) {
    int idx = cmd.indexOf(' ');
    float v = cmd.substring(idx + 1).toFloat();
    if (v > 0) {
      calibrationFactor = v;
      scale.set_scale(calibrationFactor);
      Serial.print("Calibration set: "); Serial.println(calibrationFactor);
    }
  } else if (cmd.startsWith("d ")) {
    float v = cmd.substring(2).toFloat();
    if (v >= 0) {
      stableThreshold = v;
      Serial.print("Threshold set: "); Serial.println(stableThreshold);
    }
  } else if (cmd.startsWith("a ")) {
    float v = cmd.substring(2).toFloat();
    if (v > 0 && v <= 1.0f) {
      emaAlpha = v;
      Serial.print("EMA alpha set: "); Serial.println(emaAlpha);
    }
  } else if (cmd == "save") {
    prefs.putFloat("cal", calibrationFactor);
    prefs.putFloat("offset", weightOffset);
    prefs.putFloat("thres", stableThreshold);
    prefs.putFloat("alpha", emaAlpha);
    Serial.println("Settings persisted to flash");
  } else if (cmd == "p" || cmd == "print") {
    printSettings();
  } else {
    Serial.print("Unknown command: "); Serial.println(cmd);
  }
}

// Simple I2C scanner to help debug OLED presence
void scanI2C() {
  Serial.println("I2C scan starting...");
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    uint8_t error = Wire.endTransmission();
    if (error == 0) {
      Serial.printf(" I2C device found at 0x%02X\n", addr);
      ++found;
    } else if (error == 4) {
      Serial.printf(" I2C unknown error at 0x%02X\n", addr);
    }
  }
  if (!found) Serial.println(" No I2C devices found");
  Serial.println("I2C scan done");
}

/* ================= SETUP ================= */
void setup() {
  // Use 115200 so logs appear if Serial Monitor uses default baud
  Serial.begin(115200);
  delay(1000);

  // I2C (ESP32 default SDA=21, SCL=22)
  Wire.begin(21, 22);

  // Diagnostic I2C scan to confirm OLED address
  scanI2C();

  // Load persisted settings
  prefs.begin("scale", false);
  calibrationFactor = prefs.getFloat("cal", calibrationFactor);
  weightOffset = prefs.getFloat("offset", 0.0f);
  stableThreshold = prefs.getFloat("thres", stableThreshold);
  emaAlpha = prefs.getFloat("alpha", emaAlpha);

  initDisplay();
  initScale();
  initWiFi();

  // show current settings
  printSettings();

  lastUpdate = millis();
  lastFirebase = millis();
  lastWifiAttempt = millis();
}

/* ================= LOOP ================= */
void loop() {
  unsigned long now = millis();

  // Try reconnecting WiFi occasionally if disconnected
  if (WiFi.status() != WL_CONNECTED && (now - lastWifiAttempt) >= WIFI_RECONNECT_INTERVAL_MS) {
    Serial.println("WiFi not connected: attempting reconnect...");
    WiFi.reconnect();
    lastWifiAttempt = now;
  }

  // Always check for serial commands
  handleSerialCommands();

  if (scale.is_ready()) {
    float raw = scale.get_units(5); // average of 5 samples
    float actual = raw - weightOffset;

    // Initialize EMA on first read
    if (fabs(smoothedWeight) < 1e-6f) smoothedWeight = actual;
    smoothedWeight = emaAlpha * actual + (1.0f - emaAlpha) * smoothedWeight;

    // Rate-limited OLED/Serial update, but only when change exceeds threshold
    if ((now - lastUpdate) >= OLED_INTERVAL_MS) {
      lastUpdate = now;

      if (fabs(smoothedWeight - lastReportedWeight) >= stableThreshold) {
        lastReportedWeight = smoothedWeight;

        // Print to Serial Monitor
        Serial.print("Weight: ");
        Serial.print(lastReportedWeight, 1);
        Serial.println(" g");

        // Update OLED
        updateOLED(lastReportedWeight);
      }
    }

    // Send to Firebase periodically (send smoothed weight)
    if ((now - lastFirebase) >= FIREBASE_INTERVAL_MS) {
      lastFirebase = now;
      bool ok = sendFirebase(smoothedWeight);
      Serial.print("Firebase send: ");
      Serial.println(ok ? "OK" : "FAILED");
    }

  } else {
    // Print not-ready message at a controlled rate
    if ((now - lastNotReadyPrint) >= HX711_NOT_READY_PRINT_MS) {
      Serial.println("HX711 not ready");
      lastNotReadyPrint = now;
    }
  }

  // Small delay to yield CPU and keep loop responsive
  delay(10);
}
