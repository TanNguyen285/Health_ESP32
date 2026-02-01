#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "HX711.h"
#include "MAX30105.h"
#include "heartRate.h"
#include <Adafruit_MLX90614.h>
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

/* ================= MLX90614 ================= */
Adafruit_MLX90614 mlx;

/* ================= MAX30102 ================= */
MAX30105 max30102;

/* ================= WIFI + FIREBASE ================= */
const char* WIFI_SSID = "HUNG3A";
const char* WIFI_PASS = "Nguyenmaihung1108@";
const char* FIREBASE_URL =
  "https://testnhiet28-default-rtdb.firebaseio.com/health.json";

/* ================= CẤU HÌNH ================= */
// Cân
#define CALIBRATION_FACTOR 1500.0f
#define NGUONG_NHIEU   1.f
#define NGUONG_CO_VAT  2.f
#define TG_DO_CAN_MS   5000      // Thay đổi: đo cân 5 giây
#define SO_MAU_DOC     3

// Nhiệt độ & nhịp tim
#define NGUONG_NHIET   3.0
#define IR_THRESHOLD   200000
#define TG_DO_NHIET    5000      // đo nhiệt 5 giây
#define TG_DO_BPM      15000     // đo nhịp tim 15 giây

/* ================= TRẠNG THÁI ================= */
// Cân
enum TrangThaiCan { CHO_VAT, DANG_DO, HIEN_KET_QUA };
TrangThaiCan trangThaiCan = CHO_VAT;

// Nhiệt + BPM
bool dangDoNhiet = false;
bool dangDoBPM = false;

/* ================= BIẾN ================= */
// Cân
float canHienTai = 0, canLoc = 0, tongCan = 0, ketQuaCan = 0;
int soMau = 0, demOnDinh = 0;
unsigned long tBatDauCan = 0;
bool daGuiFirebaseCan = false;

// Nhiet + BPM
float ketQuaNhiet = 0;
int ketQuaBPM = 0;

/* ================= FIREBASE ================= */
void guiFirebase(float weight_g=-1)
{
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, FIREBASE_URL);
  http.addHeader("Content-Type", "application/json");

  String json = "{";

  if(weight_g >= 0){
    float weight_kg = weight_g / 1000.0f;
    json += "\"weight_kg\":" + String(weight_kg,3) + ",";
  }

  json += "\"nhietdo\":" + String(ketQuaNhiet,1) + ",";
  json += "\"bpm\":" + String(ketQuaBPM) + ",";
  json += "\"time\":" + String(millis());
  json += "}";

  http.PUT(json);
  http.end();
}

/* ================= OLED ================= */
void hienThiCan(const char* dong1, float value)
{
  float value_kg = value / 1000.0f;
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0,0);
  oled.println(dong1);

  oled.setTextSize(2);
  oled.setCursor(0,20);
  oled.print(value,1);
  oled.println(" g");

  oled.setTextSize(1);
  oled.setCursor(0,48);
  oled.print(value_kg,3);
  oled.println(" kg");

  oled.display();
}

void hienThiDemNguoc(const char* dong, int giay)
{
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0,0);
  oled.println(dong);

  oled.setTextSize(3);
  oled.setCursor(52,22);
  oled.print(giay);

  oled.setTextSize(1);
  oled.setCursor(0,55);
  oled.println("Dang do...");

  oled.display();
}

void hienThiKetQuaNhietBPM()
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

/* ================= CÂN ================= */
void capNhatCan()
{
  if(!scale.is_ready()) return;

  canHienTai = -scale.get_units(SO_MAU_DOC);
  if(fabs(canHienTai) < NGUONG_NHIEU) canHienTai = 0;
  canLoc = 0.2 * canHienTai + 0.8 * canLoc;

  switch(trangThaiCan)
  {
    case CHO_VAT:
      hienThiCan("Dat vat len", canLoc);
      if(canLoc > NGUONG_CO_VAT){
        demOnDinh++;
        if(demOnDinh >= 5){
          trangThaiCan = DANG_DO;
          tBatDauCan = millis();
          tongCan = 0; soMau = 0; daGuiFirebaseCan=false; demOnDinh=0;
        }
      } else demOnDinh = 0;
      break;

    case DANG_DO:
      {
        unsigned long daQua = millis() - tBatDauCan;
        int conLai = (TG_DO_CAN_MS - daQua + 999)/1000;
        if(conLai<0) conLai=0;

        hienThiDemNguoc("Dang do can", conLai);
        tongCan += canLoc;
        soMau++;
        if(daQua >= TG_DO_CAN_MS){
          ketQuaCan = tongCan / soMau;
          trangThaiCan = HIEN_KET_QUA;
        }
      }
      break;

    case HIEN_KET_QUA:
      hienThiCan("Ket qua", ketQuaCan);
      if(!daGuiFirebaseCan){ guiFirebase(ketQuaCan); daGuiFirebaseCan=true; }
      if(canLoc < NGUONG_CO_VAT){ ketQuaCan=0; trangThaiCan=CHO_VAT; }
      break;
  }
}

/* ================= NHIỆT ================= */
bool phatHienTayNhiet()
{
  float amb = mlx.readAmbientTempC();
  float obj = mlx.readObjectTempC();
  return (obj - amb) >= NGUONG_NHIET;
}

void doNhiet()
{
  static unsigned long t0 = 0;
  static float tong = 0;
  static int mau = 0;

  if(!dangDoNhiet){
    dangDoNhiet = true; t0 = millis(); tong=0; mau=0;
  }

  unsigned long daQua = millis() - t0;
  int conLai = (TG_DO_NHIET - daQua + 999)/1000;
  if(conLai < 0) conLai=0;

  hienThiDemNguoc("Do nhiet", conLai);
  tong += mlx.readObjectTempC();
  mau++;

  if(daQua >= TG_DO_NHIET){
    ketQuaNhiet = tong / mau;
    dangDoNhiet=false;
    guiFirebase();
    hienThiKetQuaNhietBPM();
  }
}

/* ================= NHỊP TIM ================= */
bool phatHienTayBPM(uint32_t ir){ return ir > IR_THRESHOLD; }

void doNhipTim(uint32_t ir)
{
  static unsigned long t0=0;
  static uint32_t lastBeat=0;

  if(!dangDoBPM){ dangDoBPM=true; t0=millis(); lastBeat=0; }

  unsigned long daQua = millis()-t0;
  int conLai = (TG_DO_BPM - daQua + 999)/1000;
  if(conLai<0) conLai=0;

  hienThiDemNguoc("Do nhip tim", conLai);

  if(checkForBeat(ir)){
    uint32_t now = millis();
    if(lastBeat==0){ lastBeat=now; }
    else{
      uint32_t interval = now-lastBeat;
      if(interval>0){
        int bpm = 60000/interval;
        lastBeat=now;
        if(bpm>30 && bpm<180) ketQuaBPM=bpm;
      }
    }
  }

  if(daQua >= TG_DO_BPM){ dangDoBPM=false; guiFirebase(); hienThiKetQuaNhietBPM(); }
}

/* ================= SETUP ================= */
void setup()
{
  Serial.begin(9600);
  Wire.begin(21,22);
  Wire.setClock(100000);
  delay(100);

  // OLED
  oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  oled.setTextColor(SSD1306_WHITE);

  // HX711
  scale.begin(HX_DOUT,HX_CLK);
  scale.set_scale(CALIBRATION_FACTOR);
  scale.tare();
  delay(1000);

  // MLX90614
  if(!mlx.begin()) while(1);

  // MAX30102
  if(!max30102.begin(Wire, I2C_SPEED_STANDARD)) while(1);
  max30102.setup(60,4,2,100,411,4096);

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while(WiFi.status()!=WL_CONNECTED) delay(500);

  hienThiKetQuaNhietBPM();
}

/* ================= LOOP ================= */
void loop()
{
  // Cân
  capNhatCan();

  // MAX30102
  max30102.check();
  if(max30102.available()){
    uint32_t ir = max30102.getIR();
    max30102.nextSample();
    if(!dangDoNhiet && phatHienTayBPM(ir)) doNhipTim(ir);
  }

  // MLX90614
  if(!dangDoBPM && phatHienTayNhiet()) doNhiet();

  delay(80);
}
