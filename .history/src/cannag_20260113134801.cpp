#include <Arduino.h>
#include "HX711.h"

/* ================= PIN ================= */
#define HX_DOUT 25
#define HX_CLK  18

/* ================= CẤU HÌNH ================= */
#define MA_N    10       // số mẫu trung bình
#define ALPHA   0.25f    // low-pass (0.2–0.3 là đẹp)
#define NGUONG_NHIEU_RAW 50
#define NGUONG_CO_VAT_RAW 150


HX711 scale;

/* ================= BIẾN ================= */
long maBuf[MA_N];
int maIdx = 0;
bool maFull = false;

float lpValue = 0;

/* ================= ĐỌC + LỌC ================= */
float readFilteredHX711()
{
    long raw = scale.read();

    // ===== MOVING AVERAGE =====
    maBuf[maIdx++] = raw;
    if (maIdx >= MA_N) {
        maIdx = 0;
        maFull = true;
    }
    if (!maFull) return lpValue;

    long sum = 0;
    for (int i = 0; i < MA_N; i++)
        sum += maBuf[i];
    float avg = (float)sum / MA_N;

    // ===== LOW PASS =====
    lpValue = ALPHA * avg + (1.0f - ALPHA) * lpValue;

    return lpValue;
}

/* ================= SETUP ================= */
void setup()
{
    Serial.begin(9600);

    pinMode(HX_CLK, OUTPUT);
    digitalWrite(HX_CLK, LOW);

    scale.begin(HX_DOUT, HX_CLK);
    delay(500);

    // khởi tạo giá trị ban đầu
    lpValue = scale.read_average(20);

    Serial.println("=== HX711 BASIC FILTER TEST ===");
}

/* ================= LOOP ================= */
void loop()
{
    float val = readFilteredHX711();
    Serial.println(val, 1);
    delay(50);
}
