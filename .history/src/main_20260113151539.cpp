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
unsigned long tBatDauKhongVat = 0;
#define TG_XAC_NHAN_KHONG_VAT 5000   // ms

/* ========= HX711 ========= */
#define LP_ALPHA        0.2f
#define NGUONG_NHIEU    500
#define NGUONG_CO_VAT   10000
#define TG_DO_CAN_MS    5000    // 5 GIÂY ĐẾM NGƯỢC

float cali = 0.000079f;   // kg / raw

long hx_offset = 0;
long hx_raw = 0;

float canLoc = 0;
float tongCan = 0;
int soMauCan = 0;

float ketQuaCan = 0;      // kg
bool daCoVat = false;

unsigned long tBatDauCan = 0;



/* ================= MLX90614 ================= */
Adafruit_MLX90614 mlx;

/* ================= MAX30102 ================= */
MAX30105 max30102;

/* ================= WIFI + FIREBASE ================= */
const char* WIFI_SSID = "LagCT";
const char* WIFI_PASS = "28052004";
const char* FIREBASE_URL =
  "https://testnhiet28-default-rtdb.firebaseio.com/health.json";

/* ================= CẤU HÌNH ================= */


// Nhiệt độ 
#define NGUONG_NHIET   4.0
#define TG_DO_NHIET_MS 3000

// BPM
// ================= BPM =================
#define IR_THRESHOLD        40000     // có tay
#define PEAK_THRESHOLD      350       // ngưỡng bắt đỉnh (đã phù hợp log)
#define TG_DO_BPM_MS        30000     // 30 giây
#define MAX_BEATS           100
#define MIN_BEAT_INTERVAL   300       // ms (chặn nhịp giả)

bool dangDoBPM = false;
int ketQuaBPM = 0;

unsigned long tBatDauBPM = 0;
unsigned long lastBeatTime = 0;

uint32_t beatTimes[MAX_BEATS];
int beatCount = 0;      // CHỈ ĐẾM BEAT THẬT

// Lọc IR
float irDC = 0;
float irPrev = 0;
bool daBoBeatGia = false;


/* ================= TRẠNG THÁI ================= */
bool dangDoNhiet = false;
bool inCountdownScreen = false;


/* ================= BIẾN ================= */

// Nhiet
float ketQuaNhiet = 0;
unsigned long tBatDauNhiet = 0;
float tongNhiet = 0;
int soMauNhiet = 0;

//HC-SR04
/* ================= CHIỀU CAO ================= */
#define TRIG_PIN 17
#define ECHO_PIN 14
#define BTN_CHIEU_CAO 27

#define CHIEU_CAO_CAM_BIEN 180.0f   // cm
#define TG_DO_CC_MS 3000            // thời gian đo trung bình

enum TrangThaiChieuCao {
    CC_CHO_NUT,
    CC_DANG_DO,
    CC_HIEN
};

TrangThaiChieuCao trangThaiCC = CC_CHO_NUT;

float chieuCaoKetQua = 0;
float tongKC = 0;
int soMauKC = 0;
unsigned long tBatDauCC = 0;


/* ================= FIREBASE ================= */
void guiFirebase(float weight_g = -1)
{
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, FIREBASE_URL);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"weight_kg\":" + String(ketQuaCan, 3) + ",";
  json += "\"nhietdo\":" + String(ketQuaNhiet, 1) + ",";
  json += "\"bpm\":" + String(ketQuaBPM) + ",";
  json += "\"chieucao\":" + String(chieuCaoKetQua, 1) + ",";
  json += "\"time\":" + String(millis());
  json += "}";

  int httpCode = http.PUT(json);
  Serial.println(httpCode);
  Serial.println(http.getString());

  http.end();
}

/* ================= OLED ================= */
void showMainScreen()
{
  oled.clearDisplay();

  oled.setTextSize(1);
  oled.setCursor(0,0);
  oled.print("Can: ");
  oled.print(ketQuaCan, 3);
  oled.println(" kg");


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
// ================= CHIỀU CAO ================= */
float docKhoangCach()
{
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    long duration = pulseIn(ECHO_PIN, HIGH, 30000);
    if (duration == 0) return -1;

    float kc = duration * 0.034f / 2.0f; // cm

    if (kc <= 0 || kc > CHIEU_CAO_CAM_BIEN)
        return -1;

    return kc;
}
bool nutChieuCaoDuocNhan()
{
    static bool nutTruoc = HIGH;
    bool nutHienTai = digitalRead(BTN_CHIEU_CAO);

    bool vuaNhan = (nutTruoc == HIGH && nutHienTai == LOW);
    nutTruoc = nutHienTai;

    return vuaNhan;
}

void capNhatChieuCao()
{
    float kc = docKhoangCach();

    switch (trangThaiCC)
    {
        case CC_CHO_NUT:
            if (nutChieuCaoDuocNhan())
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

            showCountdownScreen("Do Chieu Cao", conLai);

            if (kc > 0)
            {
                tongKC += kc;
                soMauKC++;
            }

            if (daQua >= TG_DO_CC_MS)
            {
                if (soMauKC > 0)
                {
                    float kcTB = tongKC / soMauKC;
                    chieuCaoKetQua = CHIEU_CAO_CAM_BIEN - kcTB;

                    if (chieuCaoKetQua < 0) chieuCaoKetQua = 0;
                    if (chieuCaoKetQua > CHIEU_CAO_CAM_BIEN)
                        chieuCaoKetQua = CHIEU_CAO_CAM_BIEN;

                    Serial.printf("Chieu cao = %.1f cm\n", chieuCaoKetQua);
                }
                else
                {
                    chieuCaoKetQua = 0;
                    Serial.println("Khong co mau hop le");
                }

                trangThaiCC = CC_HIEN;
                inCountdownScreen = false;
            }
            break;
        }

        case CC_HIEN:
            if (nutChieuCaoDuocNhan())
            {
                trangThaiCC = CC_CHO_NUT;
                Serial.println("San sang do chieu cao tiep");
            }
            break;
    }
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

    // ===== ĐỌC RAW (ĐẢO DẤU) =====
    long raw = -scale.read();
    hx_raw = raw - hx_offset;

    if (abs(hx_raw) < NGUONG_NHIEU)
        hx_raw = 0;

    // ===== LỌC =====
    canLoc = LP_ALPHA * hx_raw + (1.0f - LP_ALPHA) * canLoc;

    bool coVatRaw = abs(hx_raw) > NGUONG_CO_VAT;
    bool khongVatRaw = abs(hx_raw) < NGUONG_NHIEU;

    // ===== XÁC NHẬN KHÔNG VẬT 5s =====
    if (khongVatRaw)
    {
        if (tBatDauKhongVat == 0)
            tBatDauKhongVat = millis();
    }
    else
    {
        tBatDauKhongVat = 0;
    }

    bool khongVatOnDinh =
        (tBatDauKhongVat != 0) &&
        (millis() - tBatDauKhongVat >= TG_XAC_NHAN_KHONG_VAT);

    // =================================================
    // ================= STATE MACHINE =================
    // =================================================
    switch (trangThaiCan)
    {
        // ===== CHỜ ĐẶT VẬT =====
        case CAN_CHO_VAT:
            if (coVatRaw)
            {
                trangThaiCan = CAN_DANG_DO;
                tBatDauCan = millis();
                tongCan = 0;
                soMauCan = 0;
                inCountdownScreen = true;

                Serial.println("[CAN] Bat dau do 5s");
            }
            break;

        // ===== ĐANG ĐO 5s =====
        case CAN_DANG_DO:
        {
            unsigned long daQua = millis() - tBatDauCan;
            int conLai = (TG_DO_CAN_MS - daQua + 999) / 1000;
            if (conLai < 0) conLai = 0;

            showCountdownScreen("Do Can", conLai);

            tongCan += canLoc;
            soMauCan++;

            if (daQua >= TG_DO_CAN_MS)
            {
                float rawTB = tongCan / soMauCan;
                ketQuaCan = rawTB * cali;

                trangThaiCan = CAN_HIEN;
                inCountdownScreen = false;

                Serial.printf(
                    "[CAN] XONG | rawTB=%.1f | kg=%.3f\n",
                    rawTB, ketQuaCan
                );

                guiFirebase();
            }
            break;
        }

        // ===== HIỂN KẾT QUẢ – CHỜ VẬT RỜI =====
        case CAN_HIEN:
            if (khongVatOnDinh)
            {
                trangThaiCan = CAN_CHO_VAT;

                // RESET HOÀN TOÀN
                canLoc = 0;
                tongCan = 0;
                soMauCan = 0;
                tBatDauKhongVat = 0;

                Serial.println("[CAN] Vat da roi 5s -> San sang can tiep");
            }
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
    inCountdownScreen=true;
  }

  unsigned long daQua = millis()-tBatDauNhiet;
  int conLai = (TG_DO_NHIET_MS - daQua + 999)/1000;
  if(conLai<0) conLai=0;

  showCountdownScreen("Do Nhiet", conLai);

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
// Các biến lọc tín hiệu
bool phatHienTayBPM(uint32_t ir)
{
    return ir > IR_THRESHOLD;
}
void doNhipTim(uint32_t irRaw)
{
    // ====== BẮT ĐẦU ĐO ======
    if (!dangDoBPM)
    {
        dangDoBPM = true;
        tBatDauBPM = millis();
        lastBeatTime = 0;
        beatCount = 0;
        daBoBeatGia = false;

        irDC = irRaw;     // khởi tạo DC → tránh xung giả
        irPrev = 0;

        inCountdownScreen = true;
       
    }

    unsigned long now = millis();
    unsigned long daQua = now - tBatDauBPM;

    int conLai = (TG_DO_BPM_MS - daQua + 999) / 1000;
    if (conLai < 0) conLai = 0;

    showCountdownScreen("Do BPM", conLai);

    // ====== HIGH-PASS FILTER ======
    irDC = 0.95f * irDC + 0.05f * irRaw;
    float irAC = irRaw - irDC;

    // ====== PHÁT HIỆN ĐỈNH ======
    unsigned long interval = now - lastBeatTime;
    if (irAC > PEAK_THRESHOLD &&
        interval > MIN_BEAT_INTERVAL)
    {
        // BEAT GIẢ ĐẦU TIÊN → BỎ HOÀN TOÀN
        if (!daBoBeatGia)
        {
            daBoBeatGia = true;
            lastBeatTime = now;
            irPrev = PEAK_THRESHOLD - 1; // cho phép phát hiện rising edge tiếp theo

            // Nếu spike lớn, xóa DC để tránh đuôi dài
            if (irAC > 10 * PEAK_THRESHOLD) {
                irDC = irRaw;
                Serial.println("Large spike detected, resetting irDC");
            }

            Serial.printf("First beat (ignored) t=%lu irAC=%.1f\n", now, irAC);
        }
        else if (beatCount < MAX_BEATS)
        {
            beatTimes[beatCount++] = now;
            lastBeatTime = now;

        }
    }

    irPrev = irAC;

    // ====== KẾT THÚC ĐO ======
    if (daQua >= TG_DO_BPM_MS)
    {
        dangDoBPM = false;
        inCountdownScreen = false;

        if (beatCount >= 2)
        {
            unsigned long duration =
                beatTimes[beatCount - 1] - beatTimes[0];

            ketQuaBPM = (beatCount - 1) * 60000.0 / duration;

        }
        else
        {
            ketQuaBPM = 0;
        }

        Serial.printf("BPM 30s = %d\n", ketQuaBPM);
        guiFirebase();
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
 scale.begin(HX_DOUT, HX_CLK);
scale.set_scale();   // scale = 1, dùng cali thủ công
scale.tare();
hx_offset = scale.read();


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
pinMode(BTN_CHIEU_CAO, INPUT_PULLUP);

// WIFI
WiFi.begin(WIFI_SSID, WIFI_PASS);

unsigned long t0 = millis();
while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
    delay(200);
}
}

/* ================= LOOP ================= */
void loop()
{
    // ===== HIỂN THỊ =====
    if (!inCountdownScreen)
        showMainScreen();

    // ===== XÁC ĐỊNH HỆ THỐNG CÓ ĐANG ĐO KHÔNG =====
    bool heThongDangDo =
        (dangDoBPM) ||
        (dangDoNhiet) ||
        (trangThaiCC == CC_DANG_DO) ||
        (trangThaiCan == CAN_DANG_DO);

    // =================================================
    // ===== NẾU ĐANG ĐO → CHỈ CẬP NHẬT CÁI ĐANG ĐO =====
    // =================================================

    if (trangThaiCan == CAN_DANG_DO)
    {
        capNhatCan();
        delay(50);
        return;
    }

    if (trangThaiCC == CC_DANG_DO)
    {
        capNhatChieuCao();
        delay(50);
        return;
    }

    if (dangDoBPM)
    {
        max30102.check();
        if (max30102.available())
        {
            uint32_t ir = max30102.getIR();
            max30102.nextSample();
            doNhipTim(ir);
        }
        delay(50);
        return;
    }

    if (dangDoNhiet)
    {
        doNhiet();
        delay(50);
        return;
    }

    // =================================================
    // ===== HỆ THỐNG RẢNH – CHO PHÉP BẮT ĐẦU ĐO =====
    // =================================================

    // ====== 1. CÂN (ƯU TIÊN CAO NHẤT) ======
    capNhatCan();

    // ====== 2. CHIỀU CAO (CHỈ KHI NHẤN NÚT) ======
    if (trangThaiCan == CAN_CHO_VAT)
    {
        capNhatChieuCao();
    }

    // ====== 3. BPM ======
    max30102.check();
    if (max30102.available())
    {
        uint32_t ir = max30102.getIR();
        max30102.nextSample();

        if (phatHienTayBPM(ir))
        {
            doNhipTim(ir);
        }
    }

    // ====== 4. NHIỆT ======
    if (phatHienTayNhiet())
    {
        doNhiet();
    }

    delay(50);
}
    