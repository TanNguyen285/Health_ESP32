#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "HX711.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

/* ================= OLED ================= */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

/* ================= HX711 ================= */
#define DOUT 25
#define CLK  26
HX711 scale;

// chỉnh theo cân của bạn
float calibration_factor = 145000;

/* ================= WIFI ================= */
const char* WIFI_TEN = "Minh Khoi";
const char* WIFI_MK  = "1234@4321";

/* ================= FIREBASE ================= */
const char* FIREBASE_URL =
"https://minh-tri-78ae1-default-rtdb.firebaseio.com/weight.json";

/* ================= BIẾN ================= */
unsigned long lastUpdate = 0;
unsigned long lastFirebase = 0;
float canCuoi = 0;

/* ================= FIREBASE ================= */
void guiFirebase(float weight)
{
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, FIREBASE_URL);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"weight\":" + String(weight, 1) + ",";
  json += "\"time\":" + String(millis());
  json += "}";

  http.PUT(json);
  http.end();
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);
  delay(1000);

  // I2C ESP32
  Wire.begin(21, 22);

  /* ===== OLED ===== */
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED fail");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0,0);
    display.println("HX711 + Firebase");
    display.display();
  }

  /* ===== HX711 ===== */
  scale.begin(DOUT, CLK);
  scale.set_scale(calibration_factor);

  Serial.println("Dang tare...");
  delay(2000);
  scale.tare();
  Serial.println("Tare xong");

  /* ===== WIFI ===== */
  WiFi.begin(WIFI_TEN, WIFI_MK);
  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

/* ================= LOOP ================= */
void loop() {
  if (scale.is_ready()) {
    float weight = scale.get_units(5);
    canCuoi = weight;

    Serial.print("Can: ");
    Serial.print(weight, 1);
    Serial.println(" g");

    /* ===== OLED mỗi 300ms ===== */
    if (millis() - lastUpdate > 300) {
      lastUpdate = millis();

      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(0, 20);
      display.print(weight, 1);
      display.print(" g");
      display.display();
    }

    /* ===== FIREBASE mỗi 3s ===== */
    if (millis() - lastFirebase > 3000) {
      lastFirebase = millis();
      guiFirebase(canCuoi);
    }

  } else {
    Serial.println("HX711 not ready");
  }

  delay(200);}