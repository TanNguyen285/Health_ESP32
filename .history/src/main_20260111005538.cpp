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

/* ================= SENSOR ================= */
Adafruit_MLX90614 mlx;
MAX30105 max30102;

/* ================= WIFI ================= */
const char* WIFI_TEN = "HUNG3A";
const char* WIFI_MK  = "Nguyenmaihung1108@";

/* ================= FIREBASE ================= */
const char* FIREBASE_URL =
  "https://testnhiet28-default-rtdb.firebaseio.com/health.json";

/* ================= CẤU HÌNH ================= */
#define NGUONG_NHIET   3.0
// Lowered threshold for IR finger detection — many MAX30102 modules return smaller IR values
#define IR_THRESHOLD  5000

#define TG_DO_NHIET   3000   // 3s
#define TG_DO_BPM     15000   // 6s

/* ================= KẾT QUẢ ================= */
float ketQuaNhiet = 0;
int   ketQuaBPM   = 0;

/* ================= TRẠNG THÁI ================= */
bool dangDoNhiet = false;
bool dangDoBPM   = false;

/* ================= FIREBASE ================= */
void guiFirebase()
{
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();
  if (!client.connect("testnhiet28-default-rtdb.firebaseio.com", 443)) return;

  HTTPClient http;
  http.setTimeout(15000);
  http.begin(client, FIREBASE_URL);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"nhietdo\":" + String(ketQuaNhiet,1) + ",";
  json += "\"bpm\":" + String(ketQuaBPM) + ",";
  json += "\"time\":" + String(millis());
  json += "}";

  http.PUT(json);
  http.end();
}

/* ================= OLED ================= */
void hienThiKetQua()
{
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0,0);
  oled.println("Ket qua");

  oled.setTextSize(2);
  oled.setCursor(0,18);
  oled.print(ketQuaNhiet,1);
  oled.println(" C");

  oled.setCursor(0,42);
  oled.print(ketQuaBPM);
  oled.println(" BPM");

  oled.display();
}

void hienThiDemNguoc(const char* ten, int giay)
{
  oled.clearDisplay();

  oled.setTextSize(1);
  oled.setCursor(0,0);
  oled.println(ten);

  oled.setTextSize(3);
  oled.setCursor(54,22);
  oled.print(giay);

  oled.setTextSize(1);
  oled.setCursor(0,55);
  oled.println("Dang do...");

  oled.display();
}

/* ================= PHÁT HIỆN ================= */
bool phatHienTayNhiet()
{
  float amb = mlx.readAmbientTempC();
  float obj = mlx.readObjectTempC();
  return (obj - amb) >= NGUONG_NHIET;
}

bool phatHienTayBPM(uint32_t ir)
{
  return ir > IR_THRESHOLD;
}

/* ================= ĐO NHIỆT ================= */
void doNhiet()
{
  static unsigned long t0 = 0;
  static float tong = 0;
  static int mau = 0;

  if (!dangDoNhiet)
  {
    dangDoNhiet = true;
    t0 = millis();
    tong = 0;
    mau = 0;
  }

  unsigned long daQua = millis() - t0;
  int conLai = (TG_DO_NHIET - daQua + 999) / 1000;
  if (conLai < 0) conLai = 0;

  hienThiDemNguoc("Do nhiet", conLai);

  tong += mlx.readObjectTempC();
  mau++;

  if (daQua >= TG_DO_NHIET)
  {
    ketQuaNhiet = tong / mau;
    dangDoNhiet = false;
    guiFirebase();
    hienThiKetQua();
  }
}

/* ================= ĐO BPM ================= */
void doNhipTim(uint32_t ir)
{
  static unsigned long t0 = 0;
  static uint32_t lastBeat = 0;

  if (!dangDoBPM)
  {
    dangDoBPM = true;
    t0 = millis();
    lastBeat = 0;
  }

  unsigned long daQua = millis() - t0;
  int conLai = (TG_DO_BPM - daQua + 999) / 1000;
  if (conLai < 0) conLai = 0;

  hienThiDemNguoc("Do nhip tim", conLai);

  if (checkForBeat(ir))
  {
    uint32_t now = millis();
    if (lastBeat == 0)
    {
      // First detected beat — initialize and wait for next beat to compute BPM
      lastBeat = now;
      Serial.println("First beat detected");
    }
    else
    {
      uint32_t interval = now - lastBeat;
      if (interval > 0)
      {
        int bpm = 60000 / interval;
        lastBeat = now;

        if (bpm > 30 && bpm < 180)
        {
          ketQuaBPM = bpm;
          Serial.printf("BPM=%d (interval=%lums)\n", bpm, interval);
        }
        else
        {
          Serial.printf("BPM out of range: %d (interval=%lums)\n", bpm, interval);
        }
      }
    }
  }

  if (daQua >= TG_DO_BPM)
  {
    dangDoBPM = false;
    guiFirebase();
    hienThiKetQua();
  }
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

  hienThiKetQua();
}

/* ================= LOOP ================= */
void loop()
{
  max30102.check();

  if (max30102.available())
  {
    uint32_t ir = max30102.getIR();
    max30102.nextSample();

    Serial.printf("IR=%lu\n", ir);

    if (!dangDoNhiet && phatHienTayBPM(ir))
      doNhipTim(ir);
  }

  if (!dangDoBPM && phatHienTayNhiet())
    doNhiet();
}
