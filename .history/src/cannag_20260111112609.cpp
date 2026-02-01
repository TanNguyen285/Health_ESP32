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
// OLED_ADDR sẽ được tự nhận; không cần khai báo địa chỉ tĩnh
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
bool oledPresent = false;
uint8_t oledAddress = 0;

// Quét I2C và thử khởi tạo OLED tại các địa chỉ tìm được (nếu có).
void detectAndInitOLED() {
  Serial.println("Quet I2C va tim thiet bi...");
  uint8_t foundAddrs[20];
  int foundCount = 0;
  for (uint8_t addr = 1; addr < 127 && foundCount < 20; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      foundAddrs[foundCount++] = addr;
      Serial.printf("  - Thiet bi I2C o dia chi 0x%02X\n", addr);
    }
  }

  // Thử khởi tạo lần lượt bằng các địa chỉ phát hiện được
  for (int i = 0; i < foundCount; ++i) {
    uint8_t a = foundAddrs[i];
    Serial.printf("Thu khoi tao OLED tai 0x%02X...\n", a);
    if (display.begin(SSD1306_SWITCHCAPVCC, a)) {
      oledPresent = true;
      oledAddress = a;
      Serial.printf("OLED khoi tao thanh cong o 0x%02X\n", a);
      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("CAn HX711");
      display.display();
      return;
    } else {
      Serial.printf("OLED khong phan hoi tai 0x%02X\n", a);
    }
  }

  // Nếu không tìm thấy (hoặc không thành công), thử các địa chỉ phổ biến
  uint8_t common[] = {0x3C, 0x3D};
  for (uint8_t a : common) {
    Serial.printf("Thu khoi tao OLED tai dia chi pho bien 0x%02X...\n", a);
    if (display.begin(SSD1306_SWITCHCAPVCC, a)) {
      oledPresent = true;
      oledAddress = a;
      Serial.printf("OLED khoi tao thanh cong o 0x%02X\n", a);
      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("CAn HX711");
      display.display();
      return;
    }
  }

  Serial.println("Khong tim thay hoac khoi tao OLED that bai. Kiem tra day SDA/SCL va nguon.");
  oledPresent = false;
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
  Serial.begin(115200);
  delay(1000);

  Wire.begin(21, 22);

  // Tự động quét và khởi tạo OLED (không cần gõ địa chỉ tay)
  detectAndInitOLED();

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

    /* ----- OLED: LUÔN HIỂN THỊ (nếu có) ----- */
    if (now - lastOLED >= OLED_INTERVAL_MS) {
      lastOLED = now;

      if (oledPresent) {
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
      } else {
        // Không có OLED; in nhẹ lên Serial để biết trạng thái
        Serial.print("Tr: ");
        Serial.print(weightSmooth, 1);
        Serial.println(" g (OLED ko ket noi)");
      }
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
