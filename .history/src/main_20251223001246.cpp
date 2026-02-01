#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ================= OLED =================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ================= SENSOR =================
Adafruit_MLX90614 mlx;

// ================= CONFIG =================
#define HAND_DELTA     3.0
#define MEASURE_TIME  10000
#define TEMP_MIN      32.0
#define TEMP_MAX      40.0
#define DETECT_DELAY  500

// ================= WIFI =================
const char* WIFI_SSID = "HUNG3A";
const char* WIFI_PASSWORD = "Nguyenmaihung1108@";

// ================= FIREBASE =================
const char* FIREBASE_URL =
  "https://testnhiet28-default-rtdb.firebaseio.com/temperature.json";

// ================= STATE =================
bool handPresent = false;
bool measuring = false;

unsigned long startTime = 0;
unsigned long lastDetectTime = 0;

float sumTemp = 0;
int sampleCount = 0;
float lastResult = 0;

// =================================================
// ================= FIREBASE ======================
// =================================================
void sendToFirebase(float value)
{
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, FIREBASE_URL);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"value\":" + String(value, 1) + ",";
  json += "\"time\":" + String(millis());
  json += "}";

  http.PUT(json);
  http.end();
}

// =================================================
// ================= OLED ==========================
// =================================================
void showResult(float value)
{
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("Body Temp");

  oled.setTextSize(3);
  oled.setCursor(0, 18);
  oled.print(value, 1);
  oled.println(" C");

  oled.display();
}

void showInvalid()
{
  oled.clearDisplay();
  oled.setTextSize(2);
  oled.setCursor(0, 20);
  oled.println("Invalid");
  oled.display();
}

// =================================================
// ================= MEASURE =======================
// =================================================
bool detectHand()
{
  float ambient = mlx.readAmbientTempC();
  float object  = mlx.readObjectTempC();

  return (object - ambient) >= HAND_DELTA;
}

void startMeasure()
{
  measuring = true;
  startTime = millis();
  sumTemp = 0;
  sampleCount = 0;

  Serial.println("Hand detected - Start measuring");
}

void finishMeasure()
{
  measuring = false;

  float avg = sumTemp / sampleCount;
  lastResult = avg;

  sendToFirebase(avg);

  if (avg < TEMP_MIN || avg > TEMP_MAX)
    showInvalid();
  else
    showResult(avg);
}

void updateMeasuring()
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
    finishMeasure();
  }
}

// =================================================
// ================= SETUP =========================
// =================================================
void setup()
{
  Serial.begin(9600);
  Wire.begin(21, 22);

  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oled.setTextColor(SSD1306_WHITE);

  mlx.begin();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
    delay(500);

  showResult(lastResult);
}

// =================================================
// ================= LOOP ==========================
// =================================================
void loop()
{
  bool detected = detectHand();
  unsigned long now = millis();

  // Tay đưa vào
  if (detected && !handPresent && (now - lastDetectTime > DETECT_DELAY))
  {
    handPresent = true;
    lastDetectTime = now;
    startMeasure();
  }

  // Tay rút ra
  if (!detected && handPresent)
  {
    handPresent = false;
    measuring = false;
    showResult(lastResult);
    return;
  }

  // Đang đo
  if (measuring)
  {
    updateMeasuring();
  }
}
