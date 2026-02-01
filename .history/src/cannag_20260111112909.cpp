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
#define OLED_ADDR 0x3C   // ĐÚNG theo code cũ của bạn
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

/* ================= HX711 ================= */
#define HX_DOUT 25
#define HX_CLK  18
HX711 scale;

/* ================= CẤU HÌNH CÂN ================= */
#define CALIBRATION_FACTOR 145000.0f   // hệ số hiệu chuẩn
#define NGUONG_CO_VAT      5.0f        // > 5g coi là có vật
#define NGUONG_ON_DINH     0.5f        // dao động cho phép (g)
#define TG_ON_DINH_MS      2000        // ổn định 2 giây

/* ================= WIFI + FIREBASE ================= */
const char* WIFI_SSID = "HUNG3A";
const char* WIFI_PASS = "Nguyenmaihung1108@";
const char* FIREBASE_URL =
  "https://testnhiet28-default-rtdb.firebaseio.com/health.json";

/* ================= TRẠNG THÁI ================= */
enum TrangThaiCan {
  KHONG_CO_VAT,
  CHO_ON_DINH,
  DA_ON_DINH
};

TrangThaiCan trangThai = KHONG_CO_VAT;

/* ================= BIẾN ================= */
float canHienTai = 0;
float canTruoc   = 0;
float canOnDinh  = 0;
unsigned long tBatDauOnDinh = 0;
bool daGuiFirebase = false;

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
  json += "\"weight\":" + String(weight,1) + ",";
  json += "\"time\":" + String(millis());
  json += "}";

  http.PUT(json);
  http.end();

  Serial.println(">> Da gui Firebase");
}

/* ================= OLED ================= */
void hienThiOLED(const char* dong1, float weight)
{
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0,0);
  oled.println(dong1);

  oled.setTextSize(2);
  oled.setCursor(0,20);
  oled.print(weight,1);
  oled.println(" g");

  oled.display();
}

/* ================= SETUP ================= */
void setup()
{
  Serial.begin(9600);
  Wire.begin(21,22);

  // OLED
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
  {
    Serial.println("Loi OLED");
    while (1);
  }
  oled.setTextColor(SSD1306_WHITE);

  // HX711
  scale.begin(HX_DOUT, HX_CLK);
  scale.set_scale(CALIBRATION_FACTOR);
  delay(1000);
  scale.tare();

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  hienThiOLED("San sang", 0);
}

/* ================= LOOP ================= */
void loop()
{
  if (!scale.is_ready()) return;

  canHienTai = scale.get_units(5);

  switch (trangThai)
  {
    case KHONG_CO_VAT:
      hienThiOLED("Dat vat len", canHienTai);

      if (canHienTai > NGUONG_CO_VAT)
      {
        trangThai = CHO_ON_DINH;
        tBatDauOnDinh = millis();
        canTruoc = canHienTai;
        daGuiFirebase = false;
        Serial.println(">> Phat hien vat");
      }
      break;

    case CHO_ON_DINH:
      hienThiOLED("Dang on dinh", canHienTai);

      if (fabs(canHienTai - canTruoc) <= NGUONG_ON_DINH)
      {
        if (millis() - tBatDauOnDinh >= TG_ON_DINH_MS)
        {
          canOnDinh = canHienTai;
          trangThai = DA_ON_DINH;
          Serial.println(">> Can on dinh");
        }
      }
      else
      {
        tBatDauOnDinh = millis();
        canTruoc = canHienTai;
      }
      break;

    case DA_ON_DINH:
      hienThiOLED("Ket qua", canOnDinh);

      if (!daGuiFirebase)
      {
        guiFirebase(canOnDinh);
        daGuiFirebase = true;
      }

      if (canHienTai < NGUONG_CO_VAT)
      {
        trangThai = KHONG_CO_VAT;
        Serial.println(">> Lay vat ra");
      }
      break;
  }

  delay(100);
}
