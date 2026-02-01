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
#define NGUONG_CO_VAT      5.0f     // >5g coi là có vật
#define NGUONG_ON_DINH     0.5f     // dao động cho phép
#define TG_DO_CAN_MS      3000      // đo trong 3s

/* ================= WIFI + FIREBASE ================= */
const char* WIFI_SSID = "HUNG3A";
const char* WIFI_PASS = "Nguyenmaihung1108@";
const char* FIREBASE_URL =
  "https://testnhiet28-default-rtdb.firebaseio.com/health.json";

/* ================= TRẠNG THÁI ================= */
bool dangDoCan = false;
bool daGuiFirebase = false;

/* ================= BIẾN ================= */
float ketQuaCan = 0;
float canHienTai = 0;
float tongCan = 0;
int soMau = 0;
unsigned long tBatDauDo = 0;

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
  oled.setCursor(50,22);
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

  canHienTai = scale.get_units(5);
  Serial.printf("Can: %.2f g\n", canHienTai);

  /* ===== PHÁT HIỆN ĐẶT VẬT ===== */
  if (!dangDoCan && canHienTai > NGUONG_CO_VAT)
  {
    dangDoCan = true;
    daGuiFirebase = false;
    tongCan = 0;
    soMau = 0;
    tBatDauDo = millis();

    Serial.println(">> Bat dau can");
  }

  /* ===== ĐANG ĐO ===== */
  if (dangDoCan)
  {
    unsigned long daQua = millis() - tBatDauDo;
    int conLai = (TG_DO_CAN_MS - daQua + 999) / 1000;
    if (conLai < 0) conLai = 0;

    hienThiDemNguoc(conLai);

    tongCan += canHienTai;
    soMau++;

    if (daQua >= TG_DO_CAN_MS)
    {
      ketQuaCan = tongCan / soMau;
      dangDoCan = false;

      Serial
