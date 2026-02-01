#include <Arduino.h>
#include <math.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "HX711.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

/* ================= CẤU HÌNH OLED ================= */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x57   // ĐỊA CHỈ I2C THỰC TẾ CỦA BẠN
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

/* ================= HX711 ================= */
#define HX_DOUT 25
#define HX_CLK  18
HX711 scale;

/* ================= THAM SỐ CÂN ================= */
#define CALIBRATION_FACTOR 145000L   // hệ số hiệu chuẩn
#define STABLE_THRESHOLD   1.0f      // ngưỡng thay đổi (gram)
#define EMA_ALPHA          0.2f      // làm mượt (0.0 – 1.0)

/* ================= WIFI ================= */
const char* WIFI_SSID = "HUNG3A";
const char* WIFI_PASS = "Nguyenmaihung1108@";

/* ================= FIREBASE ================= */
const char* FIREBASE_URL =
  "https://testnhiet28-default-rtdb.firebaseio.com/health.json";

/* ================= THỜI GIAN ================= */
#define OLED_INTERVAL_MS      300
#define FIREBASE_INTERVAL_MS  1000
#define WIFI_RETRY_MS         5000

/* ================= BIẾN TRẠNG THÁI ================= */
float weightSmooth = 0;
float lastOLEDWeight = NAN;
float lastFirebaseWeight = NAN;

unsigned long lastOLED = 0;
unsigned long lastFirebase = 0;
unsigned long lastWifiTry = 0;

/* ================= OLED ================= */
void initOLED() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("❌ OLED KHÔNG KHỞI TẠO ĐƯỢC");
    while (1);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("CAN HX711");
  display.display();
}

/* ================= WIFI ================= */
void connectWiFi() {
  Serial.print("Dang ket noi WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi da ket noi");
}

/* ================= FIREBASE ================= */
void guiFirebase(float weight) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, FIREBASE_URL);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"weight\":" + String(weight, 1);
  json += "}";

  int code = http.PUT(json);
  Serial.print("Firebase HTTP: ");
  Serial.println(code);
  http.end();
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(9600);
  delay(1000);

  Wire.begin(21, 22);

  // Quét I2C để kiểm tra OLED
  Serial.println("Quet I2C...");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("Tim thay I2C: 0x");
      Serial.println(addr, HEX);
    }
  }

  initOLED();

  scale.begin(HX_DOUT, HX_CLK);
  scale.set_scale(CALIBRATION_FACTOR);
  delay(2000);
  scale.tare();
  Serial.println("Da tare can");

  connectWiFi();
}

/* ================= LOOP ================= */
void loop() {
  unsigned long now = millis();

  /* ----- TỰ KẾT NỐI LẠI WIFI ----- */
  if (WiFi.status() != WL_CONNECTED &&
      now - lastWifiTry > WIFI_RETRY_MS) {
    lastWifiTry = now;
    WiFi.reconnect();
  }

  /* ----- ĐỌC CÂN ----- */
  if (scale.is_ready()) {
    float raw = scale.get_units(5);

    // EMA làm mượt
    if (isnan(weightSmooth)) weightSmooth = raw;
    weightSmooth = EMA_ALPHA * raw + (1.0f - EMA_ALPHA) * weightSmooth;

    /* ----- OLED: LUÔN HIỂN THỊ ----- */
    if (now - lastOLED >= OLED_INTERVAL_MS) {
      lastOLED = now;

      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("WiFi: ");
      display.println(WiFi.status() == WL_CONNECTED ? "OK" : "NO");

      display.setTextSize(2);
      display.setCursor(0, 20);
      display.print(weightSmooth, 1);
      display.print(" g");

      display.display();
    }

    /* ----- FIREBASE: CHỈ GỬI KHI THAY ĐỔI ----- */
    if ((isnan(lastFirebaseWeight) ||
        fabs(weightSmooth - lastFirebaseWeight) >= STABLE_THRESHOLD) &&
        now - lastFirebase >= FIREBASE_INTERVAL_MS) {

      lastFirebase = now;
      lastFirebaseWeight = weightSmooth;

      Serial.print("Gui Firebase: ");
      Serial.println(weightSmooth, 1);
      guiFirebase(weightSmooth);
    }
  }

  delay(10);
}
