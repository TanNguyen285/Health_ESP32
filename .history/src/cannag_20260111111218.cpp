#include <Arduino.h>
#include <math.h>
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

// Các biến điều chỉnh (có thể thay đổi qua Serial)
float weightOffset = 0.0f;        // bù zero (g)
float stableThreshold = 0.5f;     // ngưỡng thay đổi (g) để báo/cập nhật
float emaAlpha = 0.2f;            // hệ số làm mượt (0..1)
float smoothedWeight = 0.0f;      // trạng thái EMA
float lastReportedWeight = 0.0f;  // trọng lượng lần báo trước đó

Preferences prefs;

// WiFi
const char* WIFI_SSID = "HUNG3A";
const char* WIFI_PASS = "Nguyenmaihung1108@";

// Firebase
const char* FIREBASE_URL = "https://testnhiet28-default-rtdb.firebaseio.com/health.json";

// Khoảng thời gian (ms)
constexpr unsigned long OLED_INTERVAL_MS = 300;
constexpr unsigned long FIREBASE_MIN_INTERVAL_MS = 1000; // tối thiểu giữa 2 lần gửi
constexpr unsigned long WIFI_RECONNECT_INTERVAL_MS = 5000;
constexpr unsigned long HX711_NOT_READY_PRINT_MS = 2000;

/* ================= STATE ================= */
unsigned long lastUpdate = 0;
unsigned long lastFirebase = 0;
unsigned long lastWifiAttempt = 0;
unsigned long lastNotReadyPrint = 0;
float lastWeight = 0.0f;
float lastFirebaseWeight = NAN; // trọng lượng lần gửi cuối (để so sánh thay đổi)

/* ================= HELPERS ================= */
void initDisplay() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED khởi tạo thất bại");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Cân HX711");
    display.display();
  }
}

void initScale() {
  scale.begin(DOUT_PIN, CLK_PIN);
  scale.set_scale(calibrationFactor);

  Serial.println("Đang tare...");
  delay(2000);
  scale.tare();
  Serial.println("Tare xong");
}

void initWiFi() {
  Serial.printf("Đang kết nối WiFi '%s'...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    Serial.print('.');
    delay(500);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi đã kết nối");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nKhông kết nối WiFi (sẽ thử lại)");
  }
}

bool sendFirebase(float weight) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Bỏ gửi Firebase: WiFi chưa kết nối");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure(); // chấp nhận mọi chứng chỉ (dùng cho test)

  HTTPClient http;
  if (!http.begin(client, FIREBASE_URL)) {
    Serial.println("HTTP bắt đầu thất bại");
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
    if (payload.length()) Serial.print("Phản hồi: "), Serial.println(payload);
    http.end();
    return (httpCode >= 200 && httpCode < 300);
  } else {
    Serial.printf("Lỗi Firebase: %s (mã %d)\n", http.errorToString(httpCode).c_str(), httpCode);
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

  // Dòng cuối: hiệu chuẩn và cài đặt tóm tắt
  display.setTextSize(1);
  display.setCursor(0, 54);
  display.print("H:"); display.print((long)calibrationFactor);
  display.print(" Ng:"); display.print(stableThreshold, 1);
  display.print(" A:"); display.print(emaAlpha, 2);

  display.display();
}

// Các lệnh điều khiển: tare/lưu/cấu hình
void tareAndSave() {
  // Tare bằng thư viện và đặt offset = 0, lưu trạng thái
  scale.tare();
  weightOffset = 0.0f;
  prefs.putFloat("offset", weightOffset);
  Serial.println("Đã tare và lưu offset = 0");
}

void printSettings() {
  Serial.println("--- Cấu hình cân ---");
  Serial.print("Hệ số hiệu chuẩn: "); Serial.println(calibrationFactor);
  Serial.print("Độ lệch (offset): "); Serial.println(weightOffset);
  Serial.print("Ngưỡng (g): "); Serial.println(stableThreshold);
  Serial.print("Alpha EMA: "); Serial.println(emaAlpha);
  Serial.println("Nhập lệnh: 't' tare, 'c <value>' đặt hệ số hiệu chỉnh, 'd <value>' đặt ngưỡng, 'a <0-1>' đặt alpha, 'save' lưu cấu hình");
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
      Serial.print("Đã đặt hệ số: "); Serial.println(calibrationFactor);
    }
  } else if (cmd.startsWith("d ")) {
    float v = cmd.substring(2).toFloat();
    if (v >= 0) {
      stableThreshold = v;
      Serial.print("Đã đặt ngưỡng: "); Serial.println(stableThreshold);
    }
  } else if (cmd.startsWith("a ")) {
    float v = cmd.substring(2).toFloat();
    if (v > 0 && v <= 1.0f) {
      emaAlpha = v;
      Serial.print("Đã đặt alpha EMA: "); Serial.println(emaAlpha);
    }
  } else if (cmd == "save") {
    prefs.putFloat("cal", calibrationFactor);
    prefs.putFloat("offset", weightOffset);
    prefs.putFloat("thres", stableThreshold);
    prefs.putFloat("alpha", emaAlpha);
    Serial.println("Đã lưu cấu hình vào flash");
  } else if (cmd == "p" || cmd == "print") {
    printSettings();
  } else {
    Serial.print("Lệnh không rõ: "); Serial.println(cmd);
  }
}

// Bộ quét I2C đơn giản để kiểm tra thiết bị
void scanI2C() {
  Serial.println("Bắt đầu quét I2C...");
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    uint8_t error = Wire.endTransmission();
    if (error == 0) {
      Serial.printf(" Thiết bị I2C phát hiện ở 0x%02X\n", addr);
      ++found;
    } else if (error == 4) {
      Serial.printf(" Lỗi I2C không xác định ở 0x%02X\n", addr);
    }
  }
  if (!found) Serial.println(" Không tìm thấy thiết bị I2C");
  Serial.println("Quét I2C xong");
} 

/* ================= SETUP ================= */
void setup() {
  // Dùng 115200 để log rõ trên Serial Monitor
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
    Serial.println("WiFi chưa kết nối: đang thử kết nối lại...");
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

        // In ra Serial Monitor
        Serial.print("Trọng lượng: ");
        Serial.print(lastReportedWeight, 1);
        Serial.println(" g");

        // Update OLED
        updateOLED(lastReportedWeight);
      }
    }

    // Gửi Firebase chỉ khi trọng lượng thay đổi vượt ngưỡng (và đảm bảo khoảng cách thời gian tối thiểu)
    if (fabs(smoothedWeight - lastFirebaseWeight) >= stableThreshold && (now - lastFirebase) >= FIREBASE_MIN_INTERVAL_MS) {
      lastFirebase = now;
      bool ok = sendFirebase(smoothedWeight);
      if (ok) lastFirebaseWeight = smoothedWeight;
      Serial.print("Gửi Firebase: ");
      Serial.println(ok ? "THÀNH CÔNG" : "THẤT BẠI");
    }

  } else {
    // Print not-ready message at a controlled rate
    if ((now - lastNotReadyPrint) >= HX711_NOT_READY_PRINT_MS) {
      Serial.println("HX711 chưa sẵn sàng");
      lastNotReadyPrint = now;
    }
  }

  // Small delay to yield CPU and keep loop responsive
  delay(10);
}
