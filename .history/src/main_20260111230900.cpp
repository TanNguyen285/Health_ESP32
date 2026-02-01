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
#define OLED_REFRESH_MS 200   // chu kỳ vẽ OLED (ms)


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
#define CALIBRATION_FACTOR 1000.0f
#define NGUONG_NHIEU   11.f
#define NGUONG_CO_VAT  12.f
#define TG_DO_CAN_MS   3000      // 3 giây
#define SO_MAU_DOC     3


// Nhiệt độ & nhịp tim
#define NGUONG_NHIET   4.0
#define IR_THRESHOLD   200000
#define TG_DO_NHIET_MS 3000
#define TG_DO_BPM_MS   15000
#define MAX_BEATS 50

// HC-SR04
/* ================= CHIEU CAO ================= */
#define TRIG_PIN 14
#define ECHO_PIN 17

#define CHIEU_CAO_CAM_BIEN 200.0f
#define NGUONG_PHAT_HIEN_CC 100.0f
#define TG_DO_CC_MS 5000


/* ================= TRẠNG THÁI ================= */
bool dangDoNhiet = false;
bool dangDoBPM   = false;
bool inCountdownScreen = false;
bool daCoVat = false;


/* ================= BIẾN ================= */
//Oled
unsigned long tLastOLED = 0;

// Cân
float canHienTai = 0, canLoc = 0, tongCan = 0, ketQuaCan = 0;
int soMauCan = 0, demOnDinh = 0;
unsigned long tBatDauCan = 0;

// Nhiet
float ketQuaNhiet = 0;
unsigned long tBatDauNhiet = 0;
float tongNhiet = 0;
int soMauNhiet = 0;

// BPM
int ketQuaBPM = 0;
unsigned long tBatDauBPM = 0;
uint32_t beatTimes[MAX_BEATS];
int beatCount = 0;
 
//HC-SR04
enum TrangThaiChieuCao {
    CC_CHO_NGUOI,
    CC_DANG_DO,
    CC_HIEN
};

TrangThaiChieuCao trangThaiCC = CC_CHO_NGUOI;

float chieuCaoKetQua = 0;
float tongKC = 0;
int soMauKC = 0;

unsigned long tBatDauCC = 0;
bool daCoNguoiCC = false;


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
  json += "\"weight_kg\":" + String(weight_g<0 ? ketQuaCan/1000.0f : weight_g/1000.0f, 3) + ",";
  json += "\"nhietdo\":" + String(ketQuaNhiet,1) + ",";
  json += "\"bpm\":" + String(ketQuaBPM) + ",";
  json += ",\"chieucao\":" + String(chieuCaoKetQua,1);
  json += "\"time\":" + String(millis());
  json += "}";

  http.PUT(json);
  http.end();
}

/* ================= OLED ================= */
void showMainScreen()
{
  oled.clearDisplay();

  oled.setTextSize(1);
  oled.setCursor(0,0);
  oled.print("Can: ");
  oled.print(ketQuaCan,1);
  oled.println(" g");

  oled.setCursor(0,16);
  oled.print("Nhiet: ");
  oled.print(ketQuaNhiet,1);
  oled.println(" C");

  oled.setCursor(0,32);
  oled.print("BPM: ");
  oled.println(ketQuaBPM);

  oled.setCursor(0,48);
  oled.print("Cao: ");
  oled.print(chieuCaoKetQua,1);
  oled.println(" cm");

  oled.display();
}


void showCountdownScreen(const char* name, int giay)
{
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0,0);
  oled.println(name);

  oled.setTextSize(3);
  oled.setCursor(40,22);
  oled.print(giay);

  oled.setTextSize(1);
  oled.setCursor(0,55);
  oled.println("Dang do...");
  oled.display();
}

/* ================= Chiều cao ================= */
float docKhoangCach()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return 999;

  return duration * 0.034f / 2.0f;
}

void capNhatChieuCao()
{
    float kc = docKhoangCach();
    bool coNguoi = (kc < NGUONG_PHAT_HIEN_CC);

    switch (trangThaiCC)
    {
        case CC_CHO_NGUOI:
            if (coNguoi && !daCoNguoiCC)
            {
                trangThaiCC = CC_DANG_DO;
                tBatDauCC = millis();
                tongKC = 0;
                soMauKC = 0;
                inCountdownScreen = true;
                Serial.println("Bat dau do chieu cao");
            }
            break;

        case CC_DANG_DO:
        {
            unsigned long daQua = millis() - tBatDauCC;
            int conLai = (TG_DO_CC_MS - daQua + 999) / 1000;
            if (conLai < 0) conLai = 0;
            tongKC += kc;
            soMauKC++;

            if (daQua >= TG_DO_CC_MS)
            {
                float kcTB = tongKC / soMauKC;
                chieuCaoKetQua = CHIEU_CAO_CAM_BIEN - kcTB;

                trangThaiCC = CC_HIEN;
                inCountdownScreen = false;

                Serial.printf("Chieu cao: %.1f cm\n", chieuCaoKetQua);
            }
            break;
        }

        case CC_HIEN:
            if (!coNguoi && daCoNguoiCC)
            {
                trangThaiCC = CC_CHO_NGUOI;
                Serial.println("Nguoi roi khoi cam bien chieu cao");
            }
            break;
    }

    daCoNguoiCC = coNguoi;
}

/* ================= CÂN ================= */
enum TrangThaiCan {
    CAN_CHO_VAT,
    CAN_DANG_DO,
    CAN_HIEN
};
TrangThaiCan trangThaiCan = CAN_CHO_VAT;

void capNhatCan()
{
    if (!scale.is_ready()) return;

    canHienTai = -scale.get_units(SO_MAU_DOC);
    if (fabs(canHienTai) < NGUONG_NHIEU) canHienTai = 0;

    canLoc = 0.2 * canHienTai + 0.8 * canLoc;

    bool coVat = (canLoc > NGUONG_CO_VAT);

    switch (trangThaiCan)
    {
        case CAN_CHO_VAT:
            // CHỈ kích hoạt khi vừa đặt vật
            if (coVat && !daCoVat)
            {
                trangThaiCan = CAN_DANG_DO;
                tBatDauCan = millis();
                tongCan = 0;
                soMauCan = 0;
                inCountdownScreen = true;
                Serial.println("Bat dau do can");
            }
            break;

        case CAN_DANG_DO:
        {
            unsigned long daQua = millis() - tBatDauCan;
            int conLai = (TG_DO_CAN_MS - daQua + 999) / 1000;
            if (conLai < 0) conLai = 0;

            tongCan += canLoc;
            soMauCan++;

            if (daQua >= TG_DO_CAN_MS)
            {
                ketQuaCan = tongCan / soMauCan;
                trangThaiCan = CAN_HIEN;
                inCountdownScreen = false;
                guiFirebase(ketQuaCan);
                Serial.printf("Can xong: %.2f g\n", ketQuaCan);
            }
            break;
        }

        case CAN_HIEN:
            // Chỉ reset khi vật rời cân
            if (!coVat && daCoVat)
            {
                trangThaiCan = CAN_CHO_VAT;
                canLoc = 0;
                Serial.println("Vat roi, san sang do tiep");
            }
            break;
    }

    daCoVat = coVat;
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
    inCountdownScreen=true;
  }

  unsigned long daQua = millis()-tBatDauNhiet;
  int conLai = (TG_DO_NHIET_MS - daQua + 999)/1000;
  if(conLai<0) conLai=0;

  tongNhiet += mlx.readObjectTempC();
  soMauNhiet++;

  if(daQua>=TG_DO_NHIET_MS){
    ketQuaNhiet = tongNhiet/soMauNhiet;
    dangDoNhiet=false;
    guiFirebase();
    inCountdownScreen=false;
  }
}

/* ================= NHỊP TIM ================= */
bool phatHienTayBPM(uint32_t ir){
    static uint32_t irMin = 0xFFFFFFFF;
    static uint32_t irMax = 0;
    static unsigned long lastReset = 0;

    if (millis() - lastReset > 1500) {
        irMin = 0xFFFFFFFF;
        irMax = 0;
        lastReset = millis();
    }

    irMin = min(irMin, ir);
    irMax = max(irMax, ir);

    return (irMax - irMin) > 5000;
}


void doNhipTim(uint32_t ir)
{
    // ===== LỌC SAMPLE RÁC =====
    if (ir < 5000) return;

    static unsigned long lastBeatTime = 0;
    static unsigned long tOnDinh = 0;
    static int bpmTmp = 0;

    // ===== BẮT ĐẦU ĐO =====
    if (!dangDoBPM) {
        dangDoBPM = true;
        tBatDauBPM = millis();
        beatCount = 0;
        tOnDinh = 0;
        lastBeatTime = 0;
        bpmTmp = 0;
        inCountdownScreen = true;

        // reset trạng thái thuật toán beat
        checkForBeat(0);
        checkForBeat(0);
        checkForBeat(0);

        Serial.println("Bat dau do BPM");
    }

    unsigned long now = millis();
    unsigned long daQua = now - tBatDauBPM;
    int conLai = (TG_DO_BPM_MS - daQua + 999) / 1000;
    if (conLai < 0) conLai = 0;

    // ===== PHÁT HIỆN BEAT =====
    if (checkForBeat(ir)) {
        if (lastBeatTime != 0) {
            unsigned long ibi = now - lastBeatTime;

            // lọc IBI hợp lệ (30–200 BPM)
            if (ibi > 300 && ibi < 2000) {
                bpmTmp = 60000 / ibi;
                beatTimes[beatCount % MAX_BEATS] = now;
                beatCount++;

                // waveform đã ổn định sau beat đầu tiên
                if (tOnDinh == 0)
                    tOnDinh = now;

                Serial.printf("Beat! BPM=%d\n", bpmTmp);
            }
        }
        lastBeatTime = now;
    }

    // ===== CHƯA ỔN ĐỊNH → KHÔNG KẾT THÚC =====
    if (tOnDinh == 0 || now - tOnDinh < 2000)
        return;

    // ===== HẾT THỜI GIAN ĐO =====
    if (daQua >= TG_DO_BPM_MS) {

        if (beatCount >= 2) {
            // lấy BPM trung bình từ các IBI hợp lệ
            unsigned long sumIBI = 0;
            int validBeats = 0;

            for (int i = 1; i < beatCount && i < MAX_BEATS; i++) {
                unsigned long ibi = beatTimes[i] - beatTimes[i - 1];
                if (ibi > 300 && ibi < 2000) {
                    sumIBI += ibi;
                    validBeats++;
                }
            }

            if (validBeats > 0)
                ketQuaBPM = 60000 / (sumIBI / validBeats);
            else
                ketQuaBPM = 0;

        } else {
            ketQuaBPM = 0;
        }

        Serial.printf("BPM measured: %d\n", ketQuaBPM);

        dangDoBPM = false;
        inCountdownScreen = false;
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
  showMainScreen();

  // HX711
  scale.begin(HX_DOUT,HX_CLK);
  scale.set_scale(CALIBRATION_FACTOR);
  scale.tare();
  delay(1000);

  // MLX90614
  if(!mlx.begin()){ while(1); }

  // MAX30102
  if(!max30102.begin(Wire, I2C_SPEED_STANDARD)){ while(1); }
max30102.setup(
  0x7F,   // LED brightness (tối đa)
  8,      // Sample average
  2,      // IR only
  100,    // Sample rate
  411,    // Pulse width
  16384   // ADC range (RẤT QUAN TRỌNG)
);

//HC-SR04
pinMode(TRIG_PIN, OUTPUT);
pinMode(ECHO_PIN, INPUT);

  // WiFi
  WiFi.begin(WIFI_SSID,WIFI_PASS);
  while(WiFi.status()!=WL_CONNECTED) delay(500);
}

/* ================= LOOP ================= */
void loop()
{
    // ===== 1. UPDATE OLED (CHẬM) =====
    if (millis() - tLastOLED > OLED_REFRESH_MS) {
        tLastOLED = millis();

        if (inCountdownScreen) {

            if (dangDoBPM) {
                int conLai = (TG_DO_BPM_MS - (millis() - tBatDauBPM) + 999) / 1000;
                if (conLai < 0) conLai = 0;
                showCountdownScreen("Do BPM", conLai);
            }
            else if (dangDoNhiet) {
                int conLai = (TG_DO_NHIET_MS - (millis() - tBatDauNhiet) + 999) / 1000;
                if (conLai < 0) conLai = 0;
                showCountdownScreen("Do Nhiet", conLai);
            }
            else if (trangThaiCan == CAN_DANG_DO) {
                int conLai = (TG_DO_CAN_MS - (millis() - tBatDauCan) + 999) / 1000;
                if (conLai < 0) conLai = 0;
                showCountdownScreen("Do Can", conLai);
            }
            else if (trangThaiCC == CC_DANG_DO) {
                int conLai = (TG_DO_CC_MS - (millis() - tBatDauCC) + 999) / 1000;
                if (conLai < 0) conLai = 0;
                showCountdownScreen("Do Chieu Cao", conLai);
            }
        }
        else {
            showMainScreen();
        }
    }

    // ===== 2. SENSOR & LOGIC (NHANH) =====

    capNhatCan();

    if (trangThaiCan == CAN_CHO_VAT &&
        !dangDoBPM && !dangDoNhiet)
    {
        capNhatChieuCao();
    }

    // ===== BPM =====
    max30102.check();
    while (max30102.available()) {
        uint32_t ir = max30102.getIR();
        max30102.nextSample();

        if (trangThaiCan == CAN_CHO_VAT &&
            trangThaiCC == CC_CHO_NGUOI &&
            !dangDoNhiet &&
            phatHienTayBPM(ir))
        {
            doNhipTim(ir);
        }
    }

    // ===== NHIỆT =====
    if (!dangDoBPM &&
        trangThaiCan == CAN_CHO_VAT &&
        trangThaiCC == CC_CHO_NGUOI)
    {
        if (phatHienTayNhiet())
            doNhiet();
    }

    delay(5); // nhỏ, tránh watchdog
}

