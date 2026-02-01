#include <Arduino.h>
#include <Wire.h>

/* ===== OLED ===== */
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

/* ===== MLX90614 ===== */
#include <Adafruit_MLX90614.h>

/* ===== MAX30102 ===== */
#include "MAX30105.h"
#include "heartRate.h"

/* ===== WIFI + FIREBASE ===== */
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

/* ================= OLED ================= */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

/* ================= MLX ================= */
Adafruit_MLX90614 mlx;

/* ================= MAX ================= */
MAX30105 max30102;
#define IR_THRESHOLD 50000
#define THOI_GIAN_DO_NHIPTIM 60000  // 60s

/* ================= WIFI ================= */
const char* WIFI_TEN = "HUNG3A";
const char* WIFI_MK  = "Nguyenmaihung1108@";

/* ================= FIREBASE ================= */
const char* FIREBASE_URL =
  "https://testnhiet28-default-rtdb.firebaseio.com/health.json";

/* ================= BIẾN ================= */
bool coTay = false;
bool dangDo = false;

unsigned long batDauDo = 0;

/* ---- NHIỆT ---- */
float tongNhiet = 0;
int soMauNhiet = 0;
float nhietTB = 0;

/* ---- NHỊP TIM ---- */
#define MAX_SAMPLES 300
int bpmSamples[MAX_SAMPLES];
int soMauBPM = 0;
int bpmHienTai = 0;
int bpmTruoc = 0;

/* ================= FIREBASE ================= */
void guiFirebase(float nhiet, int bpm)
{
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect("testnhiet28-default-rtdb.firebaseio.com", 443))
    return;

  HTTPClient http;
  http.setTimeout(15000);
  http.begin(client, FIREBASE_URL);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"nhietdo\":" + String(nhiet,1) + ",";
  json += "\"bpm\":" + String(bpm) + ",";
  json += "\"time\":" + String(millis());
  json += "}";

  http.PUT(json);
  http.end();
}

/* ================= OLED ================= */
void hienThiDangDo(float nhiet, int bpm, int percent)
{
  oled.clearDisplay();

  oled.setTextSize(1);
  oled.setCursor(0,0);
  oled.println("Dang do...");

  oled.setCursor(0,12);
  oled.print("Nhiet: ");
  oled.print(nhiet,1);
  oled.println(" C");

  oled.setCursor(0,24);
  oled.print("BPM: ");
  oled.println(bpm);

  oled.setCursor(0,50);
  oled.print(percent);
  oled.print("%");

  oled.display();
}

void hienThiKetQua(float nhiet, int bpm)
{
  oled.clearDisplay();

  oled.setTextSize(1);
  oled.setCursor(0,0);
  oled.println("Ket qua");

  oled.setTextSize(2);
  oled.setCursor(0,16);
  oled.print(nhiet,1);
  oled.println(" C");

  oled.setCursor(0,40);
  oled.print(bpm);
  oled.println(" BPM");

  oled.display();
}

/* ================= SETUP ================= */
void setup()
{
  Serial.begin(115200);

  Wire.begin(21,22);
  Wire.setClock(100000);
  delay(100);

  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) while (1);
  oled.setTextColor(SSD1306_WHITE);

  if (!mlx.begin()) while (1);

  if (!max30102.begin(Wire, I2C_SPEED_STANDARD)) while (1);
  max30102.setup(60, 4, 2, 100, 411, 4096);

  WiFi.begin(WIFI_TEN, WIFI_MK);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  hienThiKetQua(0, 0);
}

/* ================= LOOP ================= */
void loop()
{
  max30102.check();

  while (max30102.available())
  {
    uint32_t irValue = max30102.getIR();
    max30102.nextSample();

    bool thayTay = irValue > IR_THRESHOLD;

    /* ===== PHÁT HIỆN TAY ===== */
    if (thayTay && !coTay)
    {
      coTay = true;
      dangDo = true;
      batDauDo = millis();

      tongNhiet = 0;
      soMauNhiet = 0;
      soMauBPM = 0;
      bpmHienTai = 0;
      bpmTruoc = 0;
    }

    /* ===== RÚT TAY ===== */
    if (!thayTay && coTay)
    {
      coTay = false;
      dangDo = false;

      if (soMauNhiet > 0 && soMauBPM > 0)
      {
        nhietTB = tongNhiet / soMauNhiet;

        int tong = 0;
        for (int i=0;i<soMauBPM;i++) tong += bpmSamples[i];
        int bpmTB = tong / soMauBPM;

        guiFirebase(nhietTB, bpmTB);
        hienThiKetQua(nhietTB, bpmTB);
      }
      return;
    }

    /* ===== ĐANG ĐO ===== */
    if (dangDo)
    {
      /* ---- NHIỆT ---- */
      float nhiet = mlx.readObjectTempC();
      tongNhiet += nhiet;
      soMauNhiet++;

      /* ---- NHỊP TIM ---- */
      if (checkForBeat(irValue))
      {
        static uint32_t lastBeat = 0;
        uint32_t now = millis();
        int bpm = 60000 / (now - lastBeat);
        lastBeat = now;

        if (bpm > 30 && bpm < 180)
        {
          if (soMauBPM == 0 || abs(bpm - bpmTruoc) <= 5)
          {
            bpmHienTai = bpm;
            if (soMauBPM < MAX_SAMPLES)
              bpmSamples[soMauBPM++] = bpm;
          }
          bpmTruoc = bpm;
        }
      }

      int percent = ((millis() - batDauDo) * 100) / THOI_GIAN_DO_NHIPTIM;
      if (percent > 100) percent = 100;

      hienThiDangDo(nhiet, bpmHienTai, percent);

      if (millis() - batDauDo >= THOI_GIAN_DO_NHIPTIM)
        dangDo = false;
    }
  }
}
