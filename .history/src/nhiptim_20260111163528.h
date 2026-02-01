#include <Arduino.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "common.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

/* ================= MAX30102 ================= */
MAX30105 sensor;
#define IR_THRESHOLD 50000
#define THOI_GIAN_DO 60000   // 60s

/* ================= BIẾN ================= */
bool coTay = false;
bool dangDo = false;

unsigned long batDauDo = 0;
unsigned long thoiGianDaDo = 0;

const int MAX_SAMPLES = 300;
int bpmSamples[MAX_SAMPLES];
int soMau = 0;

int bpmHienTai = 0;
int bpmTruoc = 0;

/* ================= FIREBASE ================= */
void guiFirebase(int bpm)
{
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, FIREBASE_URL);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"bpm\":" + String(bpm) + ",";
  json += "\"time\":" + String(millis());
  json += "}";

  http.PUT(json);
  http.end();
}

/* ================= OLED ================= */
void hienThiDangDo(int bpm, int percent)
{
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0,0);
  oled.println("Dang do...");

  oled.setTextSize(2);
  oled.setCursor(0,18);
  oled.print("BPM: ");
  oled.println(bpm);

  oled.setTextSize(1);
  oled.setCursor(0,50);
  oled.print(percent);
  oled.print("%");

  oled.display();
}

void hienThiKetQua(int bpm)
{
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0,0);
  oled.println("Nhip tim");

  oled.setTextSize(3);
  oled.setCursor(0,18);
  oled.print(bpm);
  oled.println(" BPM");

  oled.display();
}

/* ================= SETUP ================= */
void setup()
{
  Serial.begin(115200);
  Wire.begin(21,22);

  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oled.setTextColor(SSD1306_WHITE);

  if (!sensor.begin(Wire, I2C_SPEED_FAST))
  {
    oled.clearDisplay();
    oled.println("MAX30102 FAIL");
    oled.display();
    while (1);
  }

  sensor.setup(60, 4, 2, 100, 411, 4096);

  WiFi.begin(WIFI_TEN, WIFI_MK);
  while (WiFi.status() != WL_CONNECTED)
    delay(500);

  hienThiKetQua(0);
}

/* ================= LOOP ================= */
void loop()
{
  sensor.check();

  while (sensor.available())
  {
    uint32_t irValue = sensor.getIR();
    sensor.nextSample();

    bool thayTay = (irValue > IR_THRESHOLD);

    // Phát hiện tay
    if (thayTay && !coTay)
    {
      coTay = true;
      dangDo = true;
      batDauDo = millis();
      soMau = 0;
      bpmHienTai = 0;
      bpmTruoc = 0;
    }

    // Bỏ tay
    if (!thayTay && coTay)
    {
      coTay = false;
      dangDo = false;

      if (soMau > 0)
      {
        int tong = 0;
        for (int i = 0; i < soMau; i++) tong += bpmSamples[i];
        int bpmTB = tong / soMau;

        guiFirebase(bpmTB);
        hienThiKetQua(bpmTB);
      }
      return;
    }

    // Đang đo
    if (dangDo)
    {
      if (checkForBeat(irValue))
      {
        static uint32_t lastBeat = 0;
        uint32_t now = millis();
        int bpm = 60000 / (now - lastBeat);
        lastBeat = now;

        if (bpm > 30 && bpm < 180)
        {
          if (soMau == 0 || abs(bpm - bpmTruoc) <= 5)
          {
            bpmHienTai = bpm;
            if (soMau < MAX_SAMPLES)
              bpmSamples[soMau++] = bpm;
          }
          bpmTruoc = bpm;
        }
      }

      thoiGianDaDo = millis() - batDauDo;
      int percent = (thoiGianDaDo * 100) / THOI_GIAN_DO;
      if (percent > 100) percent = 100;

      hienThiDangDo(bpmHienTai, percent);

      if (thoiGianDaDo >= THOI_GIAN_DO)
      {
        dangDo = false;
      }
    }
  }
}
