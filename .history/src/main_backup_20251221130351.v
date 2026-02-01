#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================= OLED =================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ================= SENSOR =================
Adafruit_MLX90614 mlx;

// ================= CẤU HÌNH =================
#define HAND_THRESHOLD 30.0   // > 30°C xem là có tay
#define TEMP_MIN 32.0
#define TEMP_MAX 40.0
#define MEASURE_TIME 10000    // 10 giây
// =========================================

bool handPresent = false;
bool measuring   = false;

unsigned long startTime = 0;
float sumTemp = 0.0;
int sampleCount = 0;

float lastResult = 0.0;
// ================= HIỂN THỊ KẾT QUẢ =================
void showResult(float value)
{
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.println("Body Temp:");

    oled.setTextSize(3);
    oled.setCursor(0, 18);
    oled.print(value, 1);
    oled.println(" C");

    oled.display();
}

// ================= INVALID =================
void showInvalid()
{
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.println("Invalid!");

    oled.setTextSize(2);
    oled.setCursor(0, 20);
    oled.println("Try Again");

    oled.display();
}


// ================= SETUP =================
void setup()
{
    Wire.begin(21, 22);
    Serial.begin(9600);

    oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);

    if (!mlx.begin())
    {
        oled.println("Sensor Error!");
        oled.display();
        while (true) {}
    }

    showResult(lastResult);
}

// ================= LOOP =================
void loop()
{
    // 🔍 Chỉ đọc MLX để phát hiện tay
    float detectTemp = mlx.readObjectTempC();
    bool detected = (detectTemp > HAND_THRESHOLD);

    // 1️⃣ Tay mới đưa vào
    if (detected && !handPresent)
    {
        handPresent = true;
        measuring = true;

        startTime = millis();
        sumTemp = 0.0;
        sampleCount = 0;

        Serial.println("Hand detected -> Start measuring");
    }

    // 2️⃣ Tay rút ra
    if (!detected && handPresent)
    {
        handPresent = false;
        measuring = false;

        Serial.println("Hand removed -> Hold value");
        showResult(lastResult);
        return;
    }

    // 3️⃣ Đang đo
    if (measuring)
    {
        float temp = mlx.readObjectTempC();

        sumTemp += temp;
        sampleCount++;

        unsigned long elapsed = millis() - startTime;

        oled.clearDisplay();
        oled.setCursor(0, 0);
        oled.println("Measuring...");

        oled.setTextSize(2);
        oled.setCursor(0, 18);
        oled.print(temp, 1);
        oled.println(" C");

        oled.setTextSize(1);
        oled.setCursor(0, 50);
        oled.print(elapsed / 1000);
        oled.print("/10s");

        oled.display();

        if (elapsed >= MEASURE_TIME)
        {
            measuring = false;

            float avg = sumTemp / sampleCount;
            lastResult = avg;

            Serial.print("AVG = ");
            Serial.println(avg);

            if (avg < TEMP_MIN || avg > TEMP_MAX)
            {
                showInvalid();
            }
            else
            {
                showResult(avg);
            }
        }
    }
}

