#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

/* ================= OLED ================= */

#define OLED_RONG 128
#define OLED_CAO  64

Adafruit_SSD1306 oled(OLED_RONG, OLED_CAO, &Wire, -1);

/* ================= SENSOR ================= */

Adafruit_MLX90614 camBienNhiet;

/* ================= CẤU HÌNH ================= */

#define NGUONG_TAY     3.0
#define THOI_GIAN_DO   3000
#define NHIET_MIN      32.0
#define NHIET_MAX      40.0
#define TRE_PHAT_HIEN  500

/* ================= WIFI ================= */

const char* WIFI_TEN = "HUNG3A";
const char* WIFI_MK  = "Nguyenmaihung1108@";

/* ================= FIREBASE ================= */

const char* FIREBASE_URL =
  "https://testnhiet28-default-rtdb.firebaseio.com/nhietdo.json";

/* ================= BIẾN ================= */

bool coTay = false;
bool dangDo = false;

unsigned long thoiDiemBatDau = 0;
unsigned long lanPhatHienCuoi = 0;

float tongNhiet = 0;
int soMau = 0;
float ketQuaCuoi = 0;

/* ================= FIREBASE ================= */

void guiFirebase(float nhietDo)
{
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi chua ket noi");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect("testnhiet28-default-rtdb.firebaseio.com", 443)) {
    Serial.println("Khong ket noi duoc Firebase");
    return;
  }

  HTTPClient http;
  http.setTimeout(15000);
  http.begin(client, FIREBASE_URL);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"trungbinhnhiet\":" + String(nhietDo, 1) + ",";
  json += "\"time\":" + String(millis());
  json += "}";

  int httpCode = http.PUT(json);

  Serial.print("Firebase HTTP code: ");
  Serial.println(httpCode);
  Serial.println(http.getString());

  http.end();
}

/* ================= OLED ================= */

void hienThiKetQua(float nhietDo)
{
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("Body Temp");

  oled.setTextSize(3);
  oled.setCursor(0, 18);
  oled.print(nhietDo, 1);
  oled.println(" C");

  oled.display();
}

void hienThiLoi()
{
  oled.clearDisplay();
  oled.setTextSize(2);
  oled.setCursor(0, 20);
  oled.println("Invalid");
  oled.display();
}

/* ================= LOGIC ================= */

bool phatHienTay()
{
  float nhietMoiTruong = camBienNhiet.readAmbientTempC();
  float nhietVatThe   = camBienNhiet.readObjectTempC();
  return (nhietVatThe - nhietMoiTruong) >= NGUONG_TAY;
}

void batDauDo()
{
  dangDo = true;
  thoiDiemBatDau = millis();
  tongNhiet = 0;
  soMau = 0;
  Serial.println("Bat dau do");
}

void ketThucDo()
{
  dangDo = false;

  if (soMau == 0) return;   // CHẶN NaN

  float trungBinh = tongNhiet / soMau;
  ketQuaCuoi = trungBinh;

  guiFirebase(trungBinh);

  if (trungBinh < NHIET_MIN || trungBinh > NHIET_MAX)
    hienThiLoi();
  else
    hienThiKetQua(trungBinh);
}

void capNhatDo()
{
  float nhiet = camBienNhiet.readObjectTempC();
  tongNhiet += nhiet;
  soMau++;

  unsigned long daQua = millis() - thoiDiemBatDau;

  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("Dang do...");

  oled.setTextSize(2);
  oled.setCursor(0, 18);
  oled.print(nhiet, 1);
  oled.println(" C");

  oled.setTextSize(1);
  oled.setCursor(0, 50);
  oled.print(daQua / 1000);
  oled.print("s");

  oled.display();

  if (daQua >= THOI_GIAN_DO) {
    ketThucDo();
  }
}

/* ================= SETUP ================= */

void setup()
{
  Serial.begin(115200);

  Wire.begin(21, 22);
  Wire.setClock(100000);
  delay(100);

  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED loi");
    while (1);
  }
  oled.setTextColor(SSD1306_WHITE);

  if (!camBienNhiet.begin()) {
    Serial.println("MLX90614 loi");
    while (1);
  }

  WiFi.begin(WIFI_TEN, WIFI_MK);
  Serial.print("Dang ket noi WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi OK");

  hienThiKetQua(ketQuaCuoi);
}

/* ================= LOOP ================= */

void loop()
{
  bool thayTay = phatHienTay();
  unsigned long bayGio = millis();

  if (thayTay && !coTay && (bayGio - lanPhatHienCuoi > TRE_PHAT_HIEN)) {
    coTay = true;
    lanPhatHienCuoi = bayGio;
    batDauDo();
  }

  if (!thayTay && coTay) {
    coTay = false;
    dangDo = false;
    hienThiKetQua(ketQuaCuoi);
    return;
  }

  if (dangDo) {
    capNhatDo();
  }
}
