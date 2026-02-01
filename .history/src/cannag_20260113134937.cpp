#include <Arduino.h>
#include "HX711.h"

/* ================= PIN ================= */
#define HX_DOUT 25
#define HX_CLK  18

/* ================= CẤU HÌNH LỌC ================= */
#define MA_N    10        // số mẫu trung bình (5–15)
#define ALPHA   0.25f     // low-pass (0.2–0.3)

/* ================= HX711 ================= */
HX711 scale;

/* ================= BIẾN ================= */
long offset = 0;

// Moving average
long maBuf[MA_N];
int maIdx = 0;
bool maFull = false;

// Low-pass
float lpValue = 0;

/* ================= HÀM LỌC ================= */
float readHX711Filtered()
{
    long raw = scale.read() - offset;

    // ===== MOVING AVERAGE =====
    maBuf[maIdx++] = raw;
    if (maIdx >= MA_N) {
        maIdx = 0;
        maFull = true;
    }

    if (!maFull)
        return lpValue;

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
    delay(200);

    pinMode(HX_CLK, OUTPUT);
    digitalWrite(HX_CLK, LOW);

    scale.begin(HX_DOUT, HX_CLK);

    Serial.println("=== HX711 BASIC FILTER TEST ===");
    Serial.println("Dang cho HX711 on dinh...");

    // ===== WARM-UP =====
    delay(3000);

    // ===== TARE =====
    offset = scale.read_average(50);
    lpValue = 0;

    Serial.print("OFFSET = ");
    Serial.println(offset);

    Serial.println("RAW | FILTERED");
}

/* ================= LOOP ================= */
void loop()
{
    if (!scale.is_ready())
    {
        Serial.println("HX711 chua san sang");
        delay(200);
        return;
    }

    long raw = scale.read() - offset;
    float filtered = readHX711Filtered();

    Serial.print(raw);
    Serial.print(" | ");
    Serial.println(filtered, 1);

    delay(50);
}
