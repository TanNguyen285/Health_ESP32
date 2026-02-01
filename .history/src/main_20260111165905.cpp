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
#define TG_DO_CAN_MS   3000      // 3 giây
#define SO_MAU_DOC     3

// Nhiệt độ & nhịp tim
#define NGUONG_NHIET   3.0
#define IR_THRESHOLD   200000
#define TG_DO_NHIET_MS 3000
#define TG_DO_BPM_MS   15000

/* ================= TRẠNG THÁI ================= */
enum TrangThaiCan { CHO_VAT, DANG_DO, HIEN_KET_QUA };
TrangThaiCan trangThaiCan = CHO_VAT;

bool dangDoNhiet = false;
bool dangDoBPM   = false;

/* ================= BIẾN ================= */
// Cân
float canHienTai = 0, canLoc = 0, tongCan = 0, ketQuaCan = 0;
int soMauCan = 0, demOnDinh = 0;
unsigned long tBatDauCan = 0;
bool daGuiFirebaseCan = false;

// Nhiet + BPM
float ketQuaNhiet = 0;
int ketQuaBPM = 0;
unsigned long tBatDauNhiet = 0;
unsigned long tBatDauBPM   = 0;
float tongNhiet = 0;
int soMauNhiet = 0;
uint32_t lastBeat = 0;

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
  if(weight_g>=0){
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
void hienThiOLED()
{
  oled.clearDisplay();

  // Cân
  oled.setTextSize(1);
  oled.setCursor(0,0);
  oled.print("Can: ");
  oled.print(ketQuaCan,1);
  oled.println(" g");

  // Nhiệt độ
  oled.setTextSize(1);
  oled.setCursor(0,18);
  oled.print("Nhiet: ");
  oled.print(ketQuaNhiet,1);
  oled.println(" C");

  // BPM
  oled.setTextSize(1);
  oled.setCursor(0,36);
  oled.print("BPM: ");
  oled.println(ketQuaBPM);

  oled.display();
}

int prevCountdownCan = -1;
int prevCountdownNhiet = -1;
int prevCountdownBPM = -1;

void hienThiOLEDCountdown()
{
  // Cân
  int countCan = (trangThaiCan==DANG_DO) ? (TG_DO_CAN_MS - (millis()-tBatDauCan)+999)/1000 : 0;
  if(countCan != prevCountdownCan){
    oled.fillRect(90,0,30,16,SSD1306_BLACK); // xóa vùng cũ
    oled.setTextSize(2);
    oled.setCursor(90,0);
    oled.print(countCan);
    prevCountdownCan = countCan;
  }

  // Nhiệt
  int countNhiet = (dangDoNhiet) ? (TG_DO_NHIET_MS - (millis()-tBatDauNhiet)+999)/1000 : 0;
  if(countNhiet != prevCountdownNhiet){
    oled.fillRect(90,18,30,16,SSD1306_BLACK);
    oled.setTextSize(2);
    oled.setCursor(90,18);
    oled.print(countNhiet);
    prevCountdownNhiet = countNhiet;
  }

  // BPM
  int countBPM = (dangDoBPM) ? (TG_DO_BPM_MS - (millis()-tBatDauBPM)+999)/1000 : 0;
  if(countBPM != prevCountdownBPM){
    oled.fillRect(90,36,30,16,SSD1306_BLACK);
    oled.setTextSize(2);
    oled.setCursor(90,36);
    oled.print(countBPM);
    prevCountdownBPM = countBPM;
  }

  oled.display();
}


/* ================= CÂN ================= */
void capNhatCan()
{
  if(!scale.is_ready()) return;

  canHienTai = -scale.get_units(SO_MAU_DOC);
  if(fabs(canHienTai) < NGUONG_NHIEU) canHienTai=0;

  // EMA
  canLoc = 0.2*canHienTai + 0.8*canLoc;

  switch(trangThaiCan)
  {
    case CHO_VAT:
      if(canLoc>NGUONG_CO_VAT){
        demOnDinh++;
        if(demOnDinh>=5){
          trangThaiCan=DANG_DO;
          tBatDauCan=millis();
          tongCan=0; soMauCan=0; daGuiFirebaseCan=false; demOnDinh=0;
        }
      } else demOnDinh=0;
      break;

    case DANG_DO:
      {
        unsigned long daQua = millis()-tBatDauCan;
        int conLai = (TG_DO_CAN_MS - daQua + 999)/1000;
        if(conLai<0) conLai=0;
        hienThiOLEDCountdown();

        tongCan += canLoc;
        soMauCan++;
        if(daQua>=TG_DO_CAN_MS){
          ketQuaCan = tongCan/soMauCan;
          trangThaiCan=HIEN_KET_QUA;
        }
      }
      break;

    case HIEN_KET_QUA:
      if(!daGuiFirebaseCan){ guiFirebase(ketQuaCan); daGuiFirebaseCan=true; }
      if(canLoc<NGUONG_CO_VAT){ ketQuaCan=0; trangThaiCan=CHO_VAT; }
      break;
  }
}

/* ================= NHIỆT ================= */
bool phatHienTayNhiet()
{
  float amb=mlx.readAmbientTempC();
  float obj=mlx.readObjectTempC();
  return (obj-amb)>=NGUONG_NHIET;
}

void doNhiet()
{
  if(!dangDoNhiet){
    dangDoNhiet=true;
    tBatDauNhiet=millis();
    tongNhiet=0; soMauNhiet=0;
  }

  unsigned long daQua = millis()-tBatDauNhiet;
  int conLai = (TG_DO_NHIET_MS - daQua + 999)/1000;
  if(conLai<0) conLai=0;
  hienThiOLEDCountdown();

  tongNhiet += mlx.readObjectTempC();
  soMauNhiet++;

  if(daQua>=TG_DO_NHIET_MS){
    ketQuaNhiet = tongNhiet/soMauNhiet;
    dangDoNhiet=false;
    guiFirebase();
  }
}

/* ================= NHỊP TIM ================= */
bool phatHienTayBPM(uint32_t ir){ return ir>IR_THRESHOLD; }

void doNhipTim(uint32_t ir)
{
  if(!dangDoBPM){ dangDoBPM=true; tBatDauBPM=millis(); lastBeat=0; }

  unsigned long daQua = millis()-tBatDauBPM;
  int conLai = (TG_DO_BPM_MS - daQua + 999)/1000;
  if(conLai<0) conLai=0;
  hienThiOLEDCountdown();

  if(checkForBeat(ir)){
    uint32_t now = millis();
    if(lastBeat==0) lastBeat=now;
    else{
      uint32_t interval = now-lastBeat;
      if(interval>0){
        int bpm = 60000/interval;
        lastBeat=now;
        if(bpm>30 && bpm<180) ketQuaBPM=bpm;
      }
    }
  }

  if(daQua>=TG_DO_BPM_MS){
    dangDoBPM=false;
    guiFirebase();
  }
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
  oled.clearDisplay();
  oled.display();

  // HX711
  scale.begin(HX_DOUT,HX_CLK);
  scale.set_scale(CALIBRATION_FACTOR);
  scale.tare();
  delay(1000);

  // MLX90614
  if(!mlx.begin()){ while(1); }

  // MAX30102
  if(!max30102.begin(Wire, I2C_SPEED_STANDARD)){ while(1); }
  max30102.setup(60,4,2,100,411,4096);

  // WiFi
  WiFi.begin(WIFI_SSID,WIFI_PASS);
  while(WiFi.status()!=WL_CONNECTED) delay(500);

  hienThiOLED();
}

/* ================= LOOP ================= */
void loop()
{
  // ================= Cân ====================
  capNhatCan();

  // ================= Nhịp tim =================
  max30102.check();
  if(max30102.available()){
    uint32_t ir=max30102.getIR();
    max30102.nextSample();
    if(!dangDoNhiet) // block khi đang đo nhiệt
      if(phatHienTayBPM(ir)) doNhipTim(ir);
  }

  // ================= Nhiệt =================
  if(!dangDoBPM) // block khi đang đo BPM
    if(phatHienTayNhiet()) doNhiet();

  // Hiển thị tổng hợp
  hienThiOLED();

  delay(80); // delay nhỏ để không block Serial
}
