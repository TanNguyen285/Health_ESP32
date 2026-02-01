#include <Arduino.h>
#include "HX711.h"

/* ========= CHÂN HX711 ========= */
#define HX_DOUT 25
#define HX_CLK  18

HX711 scale;

/* ========= CẤU HÌNH ========= */
#define NGUONG_NHIEU     200      // loại nhiễu nhỏ
#define NGUONG_CO_VAT   1000      // có vật (ÂM)
#define LP_ALPHA        0.2f      // lọc low-pass

/* ========= CALIBRATION ========= */
// sau này đổi số này để ra gram / kg
float cali = 1.0f;

/* ========= BIẾN ========= */
long offset = 0;
float canLoc = 0;

void setup()
{
    Serial.begin(9600);
    delay(1000);

    scale.begin(HX_DOUT, HX_CLK);

    Serial.println("=== TEST HX711 ===");

    // Đợi HX711 sẵn sàng
    while (!scale.is_ready())
    {
        Serial.println("HX711 chua san sang");
        delay(500);
    }

    // ===== TARE =====
    Serial.println("Dang tare...");
    long sum = 0;
    for (int i = 0; i < 100; i++)
    {
        sum += scale.read();
        delay(10);
    }
    offset = sum / 100;

    Serial.print("OFFSET = ");
    Serial.println(offset);
}


void loop()
{
    if (!scale.is_ready())
    {
        Serial.println("HX711 chua san sang");
        delay(200);
        return;
    }

    long raw = scale.read() - offset;

    canLoc = LP_ALPHA * raw + (1.0f - LP_ALPHA) * canLoc;

    if (abs(canLoc) < NGUONG_NHIEU)
        canLoc = 0;

    bool coVat = (canLoc < -NGUONG_CO_VAT);

    float weightKg = -canLoc * cali;

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
