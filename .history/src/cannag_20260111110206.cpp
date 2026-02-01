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

// chỉnh theo cân của bạn (const để tránh thay đổi vô ý)
constexpr float CALIBRATION_FACTOR = 145000.0f;

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
  scale.set_scale(CALIBRATION_FACTOR);

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
  display.setCursor(0, 20);
  display.print(weight, 1);
  display.print(" g");
  display.display();
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

  initDisplay();
  initScale();
  initWiFi();

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

  if (scale.is_ready()) {
    float weight = scale.get_units(5); // average of 5 samples
    lastWeight = weight;

    // Serial output every OLED_INTERVAL_MS so monitor isn't spammed
    if ((now - lastUpdate) >= OLED_INTERVAL_MS) {
      lastUpdate = now;

      // Print to Serial Monitor
      Serial.print("Weight: ");
      Serial.print(weight, 1);
      Serial.println(" g");

      // Update OLED
      updateOLED(weight);
    }

    // Send to Firebase periodically
    if ((now - lastFirebase) >= FIREBASE_INTERVAL_MS) {
      lastFirebase = now;
      bool ok = sendFirebase(weight);
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
