#include <Arduino.h>
#include <Wire.h>
#include "common.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

/* ================= OLED ================= */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

/* ================= HC-SR04 ================= */
#define TRIG_PIN 13
#define ECHO_PIN 12

#define CHIEU_CAO_CAM_BIEN 200.0   // cm
#define THOI_GIAN_DO 5000          // 5s
#define NGUONG_PHAT_HIEN 100       // cm

/* ================= WIFI ================= */
const char* WIFI_TEN = "Minh Trí";
const char* WIFI_MK  = "26112004aa";

/* ================= FIREBASE ================= */
const char* FIREBASE_URL =
"https://minh-tri-78ae1-default-rtdb.firebaseio.com/height.json";

/* ================= BIẾN ================= */
bool coVat = false;
bool dangDo = false;

unsigned long batDauDo = 0;
float tongKhoangCach = 0;
int soMau = 0;
float chieuCaoCuoi = 0;

/* ================= ĐO KHOẢNG CÁCH ================= */
float docKhoangCach()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return 999;

  return duration * 0.034 / 2;
}

/* ================= FIREBASE ================= */
void guiFirebase(float chieuCao)
{
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, FIREBASE_URL);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"chieucao\":" + String(chieuCao, 1) + ",";
  json += "\"time\":" + String(millis());
  json += "}";

  http.PUT(json);
  http.end();
}

/* ================= OLED ================= */
void hienThiDangDo(float kc, unsigned long time)
{
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0,0);
  oled.println("Dang do...");

  oled.setTextSize(2);
  oled.setCursor(0,18);
  oled.print(kc,1);
  oled.print(" cm");

  oled.setTextSize(1);
  oled.setCursor(0,50);
  oled.print(time/1000);
  oled.print("/5s");

  oled.display();
}

void hienThiKetQua(float chieuCao)
{
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0,0);
  oled.println("Chieu cao");

  oled.setTextSize(3);
  oled.setCursor(0,18);
  oled.print(chieuCao,1);
  oled.print(" cm");

  oled.display();
}

/* ================= SETUP ================= */
void setupHeight()
{
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Hiển thị khởi tạo (OLED đã khởi tạo ở main)
  hienThiKetQua(chieuCaoCuoi);
}

/* ================= UPDATE (được gọi từ main loop) ================= */
void updateHeight()
{
  float kc = docKhoangCach();
  bool thayVat = (kc < NGUONG_PHAT_HIEN);

  // Phát hiện người
  if (thayVat && !coVat)
  {
    coVat = true;
    dangDo = true;
    batDauDo = millis();
    tongKhoangCach = 0;
    soMau = 0;
  }

  // Bỏ ra
  if (!thayVat && coVat)
  {
    coVat = false;
    dangDo = false;
    hienThiKetQua(chieuCaoCuoi);
    return;
  }

  // Đang đo
  if (dangDo)
  {
    tongKhoangCach += kc;
    soMau++;

    unsigned long daQua = millis() - batDauDo;
    hienThiDangDo(kc, daQua);

    if (daQua >= THOI_GIAN_DO)
    {
      float kcTB = tongKhoangCach / soMau;
      chieuCaoCuoi = CHIEU_CAO_CAM_BIEN - kcTB;

      guiFirebase(chieuCaoCuoi);
      hienThiKetQua(chieuCaoCuoi);

      dangDo = false;
    }
  }
}
