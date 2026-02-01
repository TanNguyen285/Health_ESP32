#include <Arduino.h>
#include "HX711.h"

/* ================= PIN ================= */
#define HX_DOUT 25
#define HX_CLK  18

/* ================= CẤU HÌNH ================= */
#define LP_ALPHA            0.2f
#define NGUONG_NHIEU_RAW    20
#define NGUONG_CO_VAT_RAW   80

HX711 scale;

/* ================= BIẾN ================= */
long offset = 0;
float canLoc = 0;

/* ================= SETUP ================= */
void setup()
{
    Serial.begin(9600);
    delay(200);

    scale.begin(HX_DOUT, HX_CLK);

    Serial.println("=== HX711 BASIC FILTER + THRESHOLD ===");
    Serial.println("Warm-up...");

    delay(3000);

    offset = scale.read_average(30);
    canLoc = 0;

    Serial.print("OFFSET = ");
    Serial.println(offset);
}

/* ================= LOOP ================= */
void loop()
{
    long raw = scale.read() - offset;

    // ===== LOW PASS =====
    canLoc = LP_ALPHA * raw + (1.0f - LP_ALPHA) * canLoc;

    // ===== LOẠI NHIỄU =====
    if (abs(canLoc) < NGUONG_NHIEU_RAW)
        canLoc = 0;

    // ===== PHÁT HIỆN VẬT =====
    bool coVat = (abs(canLoc) > NGUONG_CO_VAT_RAW);

    Serial.print("RAW: ");
    Serial.print(raw);
    Serial.print(" | LOC: ");
    Serial.print(canLoc, 1);
    Serial.print(" | CO VAT: ");
    Serial.println(coVat ? "YES" : "NO");

    delay(100);
}
