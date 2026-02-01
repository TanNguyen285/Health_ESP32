#include <Arduino.h>
#include "HX711.h"

/* ================= HX711 ================= */
#define HX_DOUT 25
#define HX_CLK  18

HX711 scale;

/* ================= CẤU HÌNH ================= */
// BẮT ĐẦU với hệ số giả, sẽ chỉnh sau
float CALIBRATION_FACTOR = 1.0;

/* ================= SETUP ================= */
void setup()
{
    Serial.begin(9600);
    delay(1000);

    Serial.println("=== TEST HX711 ===");

    scale.begin(HX_DOUT, HX_CLK);

    if (!scale.is_ready())
    {
        Serial.println("HX711 KHONG SAN SANG");
        while (1);
    }

    Serial.println("HX711 OK");
    Serial.println("Dang tare... KHONG DAT VAT");

    delay(3000);
    scale.tare();   // xóa offset

    Serial.println("Tare xong");
    Serial.println("Dat VAT CHUAN len can (VD: 500g, 1000g)");
}

/* ================= LOOP ================= */
void loop()
{
    if (!scale.is_ready())
        return;

    // ĐỌC RAW
    long raw = scale.read_average(10);

    // ĐỌC ĐÃ SCALE
    float value = scale.get_units(10);

    Serial.print("RAW: ");
    Serial.print(raw);

    Serial.print(" | VALUE: ");
    Serial.print(value, 2);

    Serial.print(" | CAL: ");
    Serial.println(CALIBRATION_FACTOR, 6);

    delay(500);
}
