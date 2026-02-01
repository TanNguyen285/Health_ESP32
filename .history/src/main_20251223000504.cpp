#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ========= WIFI =========
#include <WiFi.h>

// ========= FIREBASE (REST – KHÔNG AUTH) =========
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ================= OLED =================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ================= SENSOR =================
Adafruit_MLX90614 mlx;

// ================= CẤU HÌNH =================
#define HAND_THRESHOLD 30.0
#define TEMP_MIN 32.0
#define TEMP_MAX 40.0
#define MEASURE_TIME 10000
// =========================================

// ========= WIFI INFO =========
const char* WIFI_SSID = "HUNG3A";
const char* WIFI_PASSWORD = "Nguyenmaihung1108@";

// ========= FIREBASE URL =========
// KHÔNG cần apiKey
const char* FIREBASE_URL =
  "https://testnhiet28-default-rtdb.firebaseio.com/temperature.json";

// ================= BIẾN LOGIC =================
bool handPresent = false;
bool measuring = false;

unsigned long startTime = 0;
float sumTemp = 0;
int sampleCount = 0;

float lastResult = 0;

// ================= GỬI FIREBASE =================
void sendToFirebase(float value)
{
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skip Firebase");
    return;
  }

  // Use a secure client for HTTPS; setInsecure() is OK for testing
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, FIREBASE_URL)) {
    Serial.println("HTTP begin failed");
    return;
  }
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"value\":" + String(value, 1) + ",";
  json += "\"time\":" + String(millis());
  json += "}";

  // Use POST to append a new child (push); use PUT if you want to overwrite the node
  int httpCode = http.POST(json);
  Serial.print("Firebase HTTP code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    String payload = http.getString();
    Serial.print("Firebase response: ");
    Serial.println(payload);
  } else {
    Serial.print("Firebase error: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}

// ================= OLED =================
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
  Serial.begin(9600);
  Wire.begin(21, 22);

  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);

  if (!mlx.begin())
  {
    oled.println("Sensor Error!");
    oled.display();
    while (1);
  }

  // WIFI
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  oled.println("Connecting WiFi...");
  oled.display();

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  oled.clearDisplay();

  showResult(lastResult);
}

// ================= LOOP =================
void loop()
{
  float detectTemp = mlx.readObjectTempC();
  bool detected = (detectTemp > HAND_THRESHOLD);

  // 1️⃣ Tay đưa vào
  if (detected && !handPresent)
  {
    handPresent = true;
    measuring = true;

    startTime = millis();
    sumTemp = 0;
    sampleCount = 0;

    Serial.println("Hand detected");
  }

  // 2️⃣ Tay rút ra
  if (!detected && handPresent)
  {
    handPresent = false;
    measuring = false;

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
    oled.setTextSize(1);
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

      // 👉 GỬI FIREBASE (KHÔNG AUTH)
      sendToFirebase(avg);

      if (avg < TEMP_MIN || avg > TEMP_MAX)
        showInvalid();
      else
        showResult(avg);
    }
  }
}
