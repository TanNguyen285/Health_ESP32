#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

/* =================================================
 * ================= OLED ==========================
 * ================================================= */

// NOTE: Chiều rộng màn OLED
#define OLED_RONG 128

// NOTE: Chiều cao màn OLED
#define OLED_CAO  64

Adafruit_SSD1306 oled(OLED_RONG, OLED_CAO, &Wire, -1);

/* =================================================
 * ================= SENSOR ========================
 * ================================================= */

Adafruit_MLX90614 camBienNhiet;

/* =================================================
 * ================= CẤU HÌNH ======================
 * ================================================= */

// NOTE: Chênh lệch nhiệt (tay - môi trường) để coi là có tay
#define NGUONG_TAY        3.0

// NOTE: Thời gian đo (ms)
#define THOI_GIAN_DO      3000

// NOTE: Nhiệt độ thấp nhất hợp lệ
#define NHIET_MIN         32.0

// NOTE: Nhiệt độ cao nhất hợp lệ
#define NHIET_MAX         40.0

// NOTE: Thời gian chống nhiễu phát hiện tay (ms)
#define TRE_PHAT_HIEN     500

/* =================================================
 * ================= WIFI ==========================
 * ================================================= */

// NOTE: Tên WiFi
const char* WIFI_TEN = "Minh Trí";

// NOTE: Mật khẩu WiFi
const char* WIFI_MK  = "26112004aa";

/* =================================================
 * ================= FIREBASE ======================
 * ================================================= */

// NOTE: Node Firebase ghi đè nhiệt độ
const char* FIREBASE_URL =
  "https://minh-tri-78ae1-default-rtdb.firebaseio.com/temperature.json";

/* =================================================
 * ================= BIẾN TRẠNG THÁI ==============
 * ================================================= */

// NOTE: Cờ cho biết hiện tại có tay trước cảm biến hay không
bool coTay = false;

// NOTE: Cờ cho biết hệ thống đang trong quá trình đo
bool dangDo = false;

// NOTE: Thời điểm bắt đầu đo
unsigned long thoiDiemBatDau = 0;

// NOTE: Thời điểm phát hiện tay gần nhất (chống spam)
unsigned long lanPhatHienCuoi = 0;

// NOTE: Tổng nhiệt độ để tính trung bình
float tongNhiet = 0;

// NOTE: Số mẫu nhiệt đã lấy
int soMau = 0;

// NOTE: Kết quả đo gần nhất
float ketQuaCuoi = 0;

/* =================================================
 * ================= FIREBASE ======================
 * ================================================= */

void guiFirebase(float nhietDo)
{
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, FIREBASE_URL);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"trungbinhnhiet\":" + String(nhietDo, 1) + ",";
  json += "\"time\":" + String(millis());
  json += "}";

  http.PUT(json);
  http.end();
}

/* =================================================
 * ================= OLED ==========================
 * ================================================= */

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

/* =================================================
 * ================= LOGIC ĐO ======================
 * ================================================= */

// NOTE: Phát hiện có tay hay không dựa trên chênh lệch nhiệt
bool phatHienTay()
{
  float nhietMoiTruong = camBienNhiet.readAmbientTempC();
  float nhietVatThe   = camBienNhiet.readObjectTempC();

  return (nhietVatThe - nhietMoiTruong) >= NGUONG_TAY;
}

// NOTE: Khởi động quá trình đo
void batDauDo()
{
  dangDo = true;
  thoiDiemBatDau = millis();
  tongNhiet = 0;
  soMau = 0;

  Serial.println("Bat dau do");
}

// NOTE: Kết thúc đo, xử lý kết quả
void ketThucDo()
{
  dangDo = false;

  float trungBinh = tongNhiet / soMau;
  ketQuaCuoi = trungBinh;

  guiFirebase(trungBinh);

  if (trungBinh < NHIET_MIN || trungBinh > NHIET_MAX)
    hienThiLoi();
  else
    hienThiKetQua(trungBinh);
}

// NOTE: Cập nhật quá trình đo theo thời gian
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

  if (daQua >= THOI_GIAN_DO)
  {
    ketThucDo();
  }
}

/* =================================================
 * ================= SETUP =========================
 * ================================================= */

void setup()
{
  Serial.begin(9600);
  Wire.begin(21, 22);

  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oled.setTextColor(SSD1306_WHITE);

  camBienNhiet.begin();

  WiFi.begin(WIFI_TEN, WIFI_MK);
  while (WiFi.status() != WL_CONNECTED)
    delay(500);

  hienThiKetQua(ketQuaCuoi);
}

/* =================================================
 * ================= LOOP ==========================
 * ================================================= */

void loop()
{
  bool thayTay = phatHienTay();
  unsigned long bayGio = millis();

  // Tay đưa vào
  if (thayTay && !coTay && (bayGio - lanPhatHienCuoi > TRE_PHAT_HIEN))
  {
    coTay = true;
    lanPhatHienCuoi = bayGio;
    batDauDo();
  }

  // Tay rút ra
  if (!thayTay && coTay)
  {
    coTay = false;
    dangDo = false;
    hienThiKetQua(ketQuaCuoi);
    return;
  }

  // Đang đo
  if (dangDo)
  {
    capNhatDo();
  }
}
