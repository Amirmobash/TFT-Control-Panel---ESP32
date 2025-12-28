// =============================================
//  KabelMarker Receiver (OLED + Button) - FIXED DELAY
//  Board: NodeMCU 1.0 (ESP-12E)
//  Author: Amir Mobasheraghdam - www.nivta.de
// =============================================

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <ArduinoJson.h>

// ----- Display config -----
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// ----- WiFi / Laser side -----
const char* WIFI_SSID = "KabelLaser";
const char* WIFI_PASS = "12345678";
const char* LASER_HOST = "192.168.4.1";

// ----- Mark button -----
#define PIN_MARK_BUTTON D7

// ----- State -----
long last_mm = -1;
bool wifiConnected = false;
bool laserOK = false;
unsigned long lastSuccessData = 0;
bool showingNoData = false;

unsigned long lastQueryMs = 0;
unsigned long lastButtonMs = 0;
bool lastButtonState = HIGH;

// ===== Helper functions =====
void drawCentered(const String &txt, int y, uint8_t size = 1) {
  if(size == 2) {
    display.setFont(u8g2_font_ncenB14_tr);
  } else {
    display.setFont(u8g2_font_6x13_tr);
  }
  int16_t x = (128 - display.getStrWidth(txt.c_str())) / 2;
  if(x < 0) x = 0;
  display.setCursor(x, y);
  display.print(txt);
}

// ===== WiFi connect =====
void connectWiFi() {
  display.clearBuffer();
  drawCentered("CONNECTING", 24);
  drawCentered("to WiFi...", 40);
  display.sendBuffer();
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(500);
  }
  wifiConnected = (WiFi.status() == WL_CONNECTED);
  
  if(wifiConnected) {
    display.clearBuffer();
    drawCentered("CONNECTED!", 30, 2);
    display.sendBuffer();
    delay(1000);
  }
}

// ===== request distance from ESP32 =====
bool requestDistance() {
  if (!wifiConnected) return false;

  WiFiClient client;
  HTTPClient http;
  
  String url = "http://" + String(LASER_HOST) + "/json";
  http.begin(client, url);
  http.setTimeout(2000);
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      last_mm = doc["mm"];
      laserOK = doc["ok"];
      
      if (laserOK && last_mm > 0) {
        lastSuccessData = millis();
        showingNoData = false;
        return true;
      }
    }
  }
  
  http.end();
  laserOK = false;
  return false;
}

// ===== send MARK command =====
bool sendMarkCommand() {
  if (!wifiConnected) return false;
  
  WiFiClient client;
  HTTPClient http;
  
  String url = "http://" + String(LASER_HOST) + "/mark";
  http.begin(client, url);
  http.setTimeout(5000);
  
  int httpCode = http.GET();
  http.end();
  
  return (httpCode == HTTP_CODE_OK);
}

// ===== draw main screen =====
void drawMainScreen() {
  display.clearBuffer();

  if (!wifiConnected) {
    drawCentered("NO WIFI", 30, 2);
    display.sendBuffer();
    return;
  }

  // اگر داده معتبر داریم و کمتر از 6 ثانیه از آخرین داده گذشته
  if (laserOK && last_mm > 0 && (millis() - lastSuccessData < 6000)) {
    // Display distance +32
    float m = (last_mm + 32) / 1000.0;
    char buf[16];
    dtostrf(m, 5, 3, buf);

    display.setFont(u8g2_font_logisoso24_tr);
    int16_t x = (128 - display.getStrWidth(buf)) / 2;
    display.setCursor(x, 35);
    display.print(buf);
    
    display.setFont(u8g2_font_6x13_tr);
    display.setCursor(96, 45);
    display.print("m");

    // نمایش اطلاعات پایین
    display.setCursor(0, 62);
    display.print(String(last_mm) + "mm +32=" + String(last_mm + 32) + "mm");
    
    showingNoData = false;
  } 
  else {
    // فقط بعد از 6 ثانیه NO DATA نشان بده
    drawCentered("NO DATA", 30, 2);
    showingNoData = true;
  }
  
  display.sendBuffer();
}

// ===== splash screen =====
void showSplash() {
  display.clearBuffer();
  display.setFont(u8g2_font_ncenB14_tr);
  drawCentered("KABEL", 25, 2);
  drawCentered("MARKER", 45, 2);
  display.setFont(u8g2_font_6x13_tr);
  drawCentered("by 636", 60);
  display.sendBuffer();
  delay(3000);
}

// ===== SETUP =====
void setup() {
  pinMode(PIN_MARK_BUTTON, INPUT_PULLUP);

  display.begin();
  display.clearBuffer();
  display.sendBuffer();

  showSplash();
  connectWiFi();

  if (!wifiConnected) {
    display.clearBuffer();
    drawCentered("WIFI FAIL", 30, 2);
    display.sendBuffer();
    delay(2000);
  }
  
  lastSuccessData = millis(); // شروع تایمر
}

// ===== LOOP =====
void loop() {
  unsigned long now = millis();

  // درخواست داده هر 500ms
  if (now - lastQueryMs > 500) {
    requestDistance();
    drawMainScreen();
    lastQueryMs = now;
  }

  // خواندن دکمه
  bool btn = digitalRead(PIN_MARK_BUTTON);
  if (btn != lastButtonState && (now - lastButtonMs) > 50) {
    lastButtonState = btn;
    lastButtonMs = now;

    if (btn == LOW) {
      // فقط اگر داده معتبر داریم و کمتر از 6 ثانیه گذشته اجازه مارک بده
      if (laserOK && last_mm > 0 && (millis() - lastSuccessData < 6000)) {
        display.clearBuffer();
        drawCentered("MARK", 30, 2);
        display.sendBuffer();
        sendMarkCommand();
        delay(300);
        drawMainScreen();
      } else {
        // اگر داده معتبر نیست، پیام بده
        display.clearBuffer();
        drawCentered("NO DATA", 30, 2);
        drawCentered("Wait...", 50);
        display.sendBuffer();
        delay(1000);
        drawMainScreen();
      }
    }
  }

  // بروزرسانی خودکار صفحه هر ثانیه
  static unsigned long lastDisplayUpdate = 0;
  if (now - lastDisplayUpdate > 1000) {
    drawMainScreen();
    lastDisplayUpdate = now;
  }
}