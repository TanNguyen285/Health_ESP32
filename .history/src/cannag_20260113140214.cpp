#include <Arduino.h>
#include "HX711.h"

/* ========= PIN ========= */
#define HX_DOUT 25
#define HX_CLK  18

HX711 scale;

/* ========= THAM SỐ ========= */
#define LP_ALPHA        0.2f     // lọc cơ bản
#define NGUONG_NHIEU    300      // raw, loại rung nhẹ
#define NGUONG_CO_VAT   2000     // raw, có vật (ÂM)
#define AUTO_TARE_MS    2000     // không vật 2s thì tare lại

/* ========= CALIBRATION ========= */
// 1kg ≈ -12680 raw  → cali = 1 / 12680
float cali = 0.000079f;   // kg / raw

/* ========= BIẾN ========= */
long offset = 0;
float canLoc = 0;
unsigned long tKhongVat = 0;

void tareHX711()
{
    long sum = 0;
    for (int i = 0; i < 30; i++)
    {
        sum += scale.read();
        delay(10);
    }
    offset = sum / 30;
    canLoc = 0;
}

/* ========= SETUP ========= */
void setup()
{
    Serial.begin(9600);
    delay(1000);

    scale.begin(HX_DOUT, HX_CLK);

    Serial.println("=== TEST HX711 FINAL ===");

    while (!scale.is_ready())
    {
        Serial.println("HX711 chua san sang");
        delay(500);
    }

    Serial.println("Dang tare lan dau...");
    tareHX711();

    Serial.print("OFFSET = ");
    Serial.println(offset);
}

/* ========= LOOP ========= */
void loop()
{
    if (!scale.is_ready())
    {
        Serial.println("HX711 chua san sang");
        delay(200);
        return;
    }

    /* ===== ĐỌC RAW ===== */
    long raw = scale.read() - offset;

    /* ===== LỌC LOW PASS ===== */
    canLoc = LP_ALPHA * raw + (1.0f - LP_ALPHA) * canLoc;

    /* ===== LOẠI NHIỄU ===== */
    if (abs(canLoc) < NGUONG_NHIEU)
        canLoc = 0;

    /* ===== PHÁT HIỆN VẬT (ÂM) ===== */
    bool coVat = (canLoc < -NGUONG_CO_VAT);

    /* ===== AUTO TARE KHI KHÔNG VẬT ===== */
    if (!coVat)
    {
        if (tKhongVat == 0)
            tKhongVat = millis();

        if (millis() - tKhongVat >= AUTO_TARE_MS)
        {
            tareHX711();
            tKhongVat = 0;
            Serial.println(">>> AUTO TARE <<<");
        }
    }
    else
    {
        tKhongVat = 0;
    }

    /* ===== GIÁ TRỊ KG (HIỂN THỊ DƯƠNG) ===== */
    float weightKg = coVat ? (-canLoc * cali) : 0.0f;

    /* ===== LOG ===== */
    Serial.print("RAW: ");
    Serial.print(raw);
    Serial.print(" | LOC: ");
    Serial.print(canLoc, 1);
    Serial.print(" | CO VAT: ");
    Serial.print(coVat ? "YES" : "NO");
    Serial.print(" | KG: ");
    Serial.println(weightKg, 3);

    delay(100);
}
