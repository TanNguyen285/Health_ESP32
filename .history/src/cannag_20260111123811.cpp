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
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

/* ================= HX711 ================= */
#define HX_DOUT 25
#define HX_CLK  18
HX711 scale;

/* ================= CẤU HÌNH CÂN ================= */
#define CALIBRATION_FACTOR 145000.0f

#define NGUONG_NHIEU   0.3f    // < 0.3g coi như 0 (fix nhiễu 0.14–0.16g)
#define NGUONG_CO_VAT  1.0f    // > 1g chắc chắn có vật

#define TG_DO_CAN_MS  3000     // đo trong 3 giây
#define SO_MAU_DOC     3       // mỗi lần đọc HX711

/* ================= WIFI + FIREBASE ================= */
const char* WIFI_SSID = "HUNG3A";
const char* WIFI_PASS = "Nguyenmaihung1108@";
const char* FIREBASE_URL =
  "https://testnhiet28-default-rtdb.firebaseio.com/health.json";

/* ================= TRẠNG THÁI ================= */
enum TrangThaiCan {
  CHO_VAT,
  DANG_DO,
  HIEN_KET_QUA
};

TrangThaiCan trangThai = CHO_VAT;

/* ================= BIẾN ================= */
float canHienTai = 0;
float ketQuaCan  = 0;
float tongCan    = 0;
int   soMau      = 0;
unsigned long tBatDau = 0;
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
void hienThiCan(const char* dong1, float value)
{
  oled.clearDisplay();

  oled.setTextSize(1);
  oled.setCursor(0,0);
  oled.println(dong1);

  oled.setTextSize(2);
  oled.setCursor(0,20);
  oled.print(value,1);
  oled.println(" g");

  oled.display();
}

void hienThiDemNguoc(int giay)
{
  oled.clearDisplay();

  oled.setTextSize(1);
  oled.setCursor(0,0);
  oled.println("Dang can...");

  oled.setTextSize(3);
  oled.setCursor(52,22);
  oled.print(giay);

  oled.display();
}

/* ================= SETUP ================= */
void setup()
{
  Serial.begin(9600);
  Wire.begin(21,22);

  // OLED
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
    while (1);
  oled.setTextColor(SSD1306_WHITE);

  // HX711
  scale.begin(HX_DOUT, HX_CLK);
  scale.set_scale(CALIBRATION_FACTOR);
  delay(1000);
  scale.tare();

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  hienThiCan("San sang", 0);
}

/* ================= LOOP ================= */
void loop()
{
  if (!scale.is_ready()) return;

  canHienTai = scale.get_units(SO_MAU_DOC);

  // LỌC NHIỄU NỀN
  if (fabs(canHienTai) < NGUONG_NHIEU)
    canHienTai = 0;

  Serial.printf("Can hien tai: %.2f g\n", canHienTai);

  switch (trangThai)
  {
    case CHO_VAT:
      hienThiCan("Dat vat len", canHienTai);

      if (canHienTai > NGUONG_CO_VAT)
      {
        trangThai = DANG_DO;
        tBatDau = millis();
        tongCan = 0;
        soMau = 0;
        daGuiFirebase = false;

        Serial.println(">> Bat dau do can (3s)");
      }
      break;

    case DANG_DO:
    {
      unsigned long daQua = millis() - tBatDau;
      int conLai = (TG_DO_CAN_MS - daQua + 999) / 1000;
      if (conLai < 0) conLai = 0;

      hienThiDemNguoc(conLai);

      tongCan += canHienTai;
      soMau++;

      if (daQua >= TG_DO_CAN_MS)
      {
        ketQuaCan = tongCan / soMau;
        trangThai = HIEN_KET_QUA;

        Serial.printf(">> Ket qua: %.2f g\n", ketQuaCan);
      }
    }
    break;

    case HIEN_KET_QUA:
      hienThiCan("Ket qua", ketQuaCan);

      if (!daGuiFirebase)
      {
        guiFirebase(ketQuaCan);
        daGuiFirebase = true;
      }

      // Reset khi nhấc vật ra thật sự
      if (canHienTai < NGUONG_CO_VAT)
      {
        ketQuaCan = 0;
        trangThai = CHO_VAT;
        Serial.println(">> Lay vat ra");
      }
      break;
  }

  delay(80);
}
