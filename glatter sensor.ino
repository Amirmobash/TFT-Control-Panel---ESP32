#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <math.h>

// ==================== Pin-Belegung (Deutsch) ====================
/*
  TFT (VSPI):
    SCK  = GPIO18  - SPI Takt
    MOSI = GPIO23  - SPI Datenausgang
    MISO = GPIO19  - SPI Dateneingang
    CS   = GPIO15  - Chip Select
    DC   = GPIO2   - Daten/Befehl
    RST  = GPIO4   - Reset

  Joystick:
    X    = GPIO35  - Analogachse X
    Y    = GPIO34  - Analogachse Y
    SW   = GPIO27  - Taste (mit Pull-up)

  Drucksensor (DFRobot analog):
    Gelb = GPIO33  - Analogausgang

  Relais:
    IN   = GPIO25  - Steuerung (Aktiv LOW)

  IR/Trigger:
    OUT  = GPIO32  - Digitaler Trigger (JETZT ACTIVE-LOW!)
*/

// ==================== TFT Verkabelung (ESP32 VSPI) ====================
#define TFT_SCK   18
#define TFT_MOSI  23
#define TFT_MISO  19
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST    4
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

// ==================== Pins ====================
#define JOY_Y   34
#define JOY_X   35
#define JOY_SW  27
#define PRESS_PIN 33
#define RELAY_PIN 25
#define RELAY_ON  LOW
#define RELAY_OFF HIGH
#define SENSOR_TRIG_PIN 32

// ==================== Bildschirm ====================
#define SCREEN_W 240
#define SCREEN_H 320

// ==================== Farbpalette (Orange-Theme) ====================
static inline uint16_t SWAP_RB(uint16_t c) {
  return (c & 0x07E0) | ((c & 0xF800) >> 11) | ((c & 0x001F) << 11);
}
#define ORANGE_BG_B     0x003F
#define ORANGE_DARK_B   0x02BF
#define ORANGE_MAIN_B   0x02DF
#define ORANGE_LIGHT_B  0x02FF
#define ORANGE_ACCENT_B 0x04FF
#define PANEL_BG_B      0x2104

#define ORANGE_BG     SWAP_RB(ORANGE_BG_B)
#define ORANGE_DARK   SWAP_RB(ORANGE_DARK_B)
#define ORANGE_MAIN   SWAP_RB(ORANGE_MAIN_B)
#define ORANGE_LIGHT  SWAP_RB(ORANGE_LIGHT_B)
#define ORANGE_ACCENT SWAP_RB(ORANGE_ACCENT_B)
#define PANEL_BG      SWAP_RB(PANEL_BG_B)

#define TEXT_COLOR    0xFFFF
#define BLACK_COLOR   0x0000
#define GREEN_COLOR   0x07E0
#define RED_COLOR     0xF800
#define LINE_COLOR    0x39E7
#define WAVE_COLOR    0x07FF  // Cyan für Oszilloskop

// ==================== Layout ====================
const int HEADER_H  = 44;
const int FOOTER_H  = 26;
const int CONTENT_Y = HEADER_H + 6;
const int CONTENT_H = SCREEN_H - HEADER_H - FOOTER_H - 12;

// ==================== Einstellungen (NVS) ====================
Preferences preferences;
#define NVS_NAMESPACE "W-glaetter"

struct Settings {
  float pressureThreshold;
  float pressureScale;
  int joyOnThreshold;
  int joyOffThreshold;
  int autoOnTime;
  int autoOffTime;
  int sensorDuration;
  float dividerRatio;
  float zeroVoltage;
};

Settings settings = {
  .pressureThreshold = 0.35f,
  .pressureScale = 2.0f,
  .joyOnThreshold = 650,
  .joyOffThreshold = 420,
  .autoOnTime = 5000,
  .autoOffTime = 2000,
  .sensorDuration = 10000,
  .dividerRatio = 1.0f,        // WICHTIG: 1.0 = kein Spannungsteiler
  .zeroVoltage = 0.0f
};

// ==================== Menü ====================
const char* menuItems[] = {" Sensor-Modus", " Joystick-Modus", " Automatik", " Druckanzeige"};
const uint8_t menuCount = sizeof(menuItems) / sizeof(menuItems[0]);
uint8_t currentSelection = 0;
int8_t activeMode = -1; // -1 = Menü

// ==================== Relais ====================
bool relayActive = false;
unsigned long relayTimer = 0;

// ==================== Joystick ====================
int centerX = 2048, centerY = 2048;
float fx = 2048, fy = 2048;
const int DEADZONE = 220;
const int MENU_THRESH = 520;
const float ALPHA = 0.25f;
bool menuNavArmed = true;
unsigned long lastMenuMove = 0;
bool joyHoldLatched = false;

// ==================== Taste ====================
unsigned long lastButtonPress = 0;
unsigned long lastClickTime = 0;
uint8_t buttonClickCount = 0;

// ==================== Drucksensor ====================
float pressVolt = 0.0f;
float pressUnits = 0.0f;
float pressFilt = -1.0f;  // WICHTIG: -1.0 für Initialisierung
bool pressOK = false;
int rawADC = 0;
bool pressCalibrating = false;
const float PRESS_ALPHA = 0.15f;
const float PRESS_HYST = 0.05f;

// ==================== Automatik-Modus ====================
bool autoState = false;
bool autoFirst = true;
unsigned long autoTimer = 0;

// ==================== Web Server ====================
WebServer server(80);
const char* apSSID = "Kabelglaetter";
const char* apPassword = "12345678";
IPAddress apIP(192, 168, 4, 1);

// ==================== Oszilloskop Animation ====================
const int WAVE_POINTS = 32;
int waveValues[WAVE_POINTS] = {0};
int waveIndex = 0;
unsigned long lastWaveUpdate = 0;
const int WAVE_UPDATE_MS = 50;

// ==================== Sensor (NEU) ====================
// Active-LOW Sensor: auslösen bei LOW
// Re-Trigger Sperre: erst wieder scharf, wenn Sensor zurück auf HIGH geht
bool sensorArmed = true;

// ==================== Hilfsfunktionen ====================
static inline int clampi(int v, int lo, int hi){ return v<lo?lo:(v>hi?hi:v); }
static inline float clampf(float v, float lo, float hi){ return v<lo?lo:(v>hi?hi:v); }
void setTxt(uint8_t sz, uint16_t c) { tft.setTextSize(sz); tft.setTextColor(c); }

void activateRelay(){
  digitalWrite(RELAY_PIN, RELAY_ON);
  relayActive = true;
}

void deactivateRelay(){
  digitalWrite(RELAY_PIN, RELAY_OFF);
  relayActive = false;
}

// ==================== Zeichnen ====================
void drawHeader(const char* title){
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, ORANGE_DARK);
  tft.drawFastHLine(0, HEADER_H-1, SCREEN_W, LINE_COLOR);

  setTxt(2, TEXT_COLOR);
  tft.setCursor(10, 8);
  tft.print("W-glaetter");

  setTxt(1, ORANGE_LIGHT);
  tft.setCursor(10, 28);
  tft.print(title);

  // Relais Status Punkt
  tft.fillRoundRect(SCREEN_W-40, 10, 30, 24, 10, PANEL_BG);
  tft.drawRoundRect(SCREEN_W-40, 10, 30, 24, 10, LINE_COLOR);
  tft.fillCircle(SCREEN_W-25, 22, 6, relayActive ? GREEN_COLOR : ORANGE_LIGHT);
}

void drawWaveform(){
  // Kleine Oszilloskop-Animation im Header (nicht überlappend)
  const int waveX = 125;
  const int waveY = 10;
  const int waveW = 70;
  const int waveH = 24;

  tft.fillRect(waveX, waveY, waveW, waveH, ORANGE_DARK);

  if(relayActive){
    for(int i = 0; i < WAVE_POINTS-1; i++){
      int x1 = waveX + (i * waveW) / WAVE_POINTS;
      int x2 = waveX + ((i+1) * waveW) / WAVE_POINTS;
      int y1 = waveY + waveH/2 + waveValues[i]/4;
      int y2 = waveY + waveH/2 + waveValues[i+1]/4;
      tft.drawLine(x1, y1, x2, y2, WAVE_COLOR);
    }
  } else {
    tft.drawFastHLine(waveX, waveY + waveH/2, waveW, LINE_COLOR);
  }
}

void drawFooter(const char* text){
  tft.fillRect(0, SCREEN_H-FOOTER_H, SCREEN_W, FOOTER_H, ORANGE_DARK);
  tft.drawFastHLine(0, SCREEN_H-FOOTER_H, SCREEN_W, LINE_COLOR);
  setTxt(1, TEXT_COLOR);
  tft.setCursor(8, SCREEN_H-FOOTER_H+9);
  tft.print(text);
}

int itemY(uint8_t idx){
  const int cardH = 40;
  const int gap   = 10;
  return CONTENT_Y + 12 + idx * (cardH + gap);
}

void drawCard(int x,int y,int w,int h,bool sel){
  uint16_t fill = sel ? ORANGE_ACCENT : ORANGE_MAIN;
  uint16_t bord = sel ? ORANGE_LIGHT  : LINE_COLOR;
  tft.fillRoundRect(x, y, w, h, 10, fill);
  tft.drawRoundRect(x, y, w, h, 10, bord);
}

void drawMenuBase(){
  tft.fillScreen(ORANGE_BG);
  drawHeader("Menü");
  tft.fillRoundRect(10, CONTENT_Y, SCREEN_W-20, CONTENT_H, 14, PANEL_BG);
  tft.drawRoundRect(10, CONTENT_Y, SCREEN_W-20, CONTENT_H, 14, LINE_COLOR);
  drawFooter("HOCH/RUNTER | 1:Eintreten | 3:Zurück");
}

void drawMenuItem(uint8_t i, bool selected){
  const int x = 18, w = SCREEN_W - 36, h = 40;
  int y = itemY(i);

  drawCard(x, y, w, h, selected);
  setTxt(2, selected ? BLACK_COLOR : TEXT_COLOR);
  tft.setCursor(x + 10, y + 12);
  tft.print(menuItems[i]);
}

void drawMainMenu(){
  drawMenuBase();
  for(uint8_t i=0;i<menuCount;i++){
    drawMenuItem(i, i==currentSelection);
  }
}

// ==================== Joystick ====================
void calibrateJoystick(){
  long sx = 0, sy = 0;
  const int N = 200;
  for(int i=0;i<N;i++){
    sx += analogRead(JOY_X);
    sy += analogRead(JOY_Y);
    delay(5);
  }
  centerX = sx / N;
  centerY = sy / N;
  fx = centerX;
  fy = centerY;
}

void updateJoyFilter(){
  int rx = analogRead(JOY_X);
  int ry = analogRead(JOY_Y);
  fx = fx * (1.0f - ALPHA) + rx * ALPHA;
  fy = fy * (1.0f - ALPHA) + ry * ALPHA;
}

int joyDX(){ return (int)fx - centerX; }
int joyDY(){ return (int)fy - centerY; }

void handleMainMenuNav(){
  int dx = joyDX();
  int dy = joyDY();

  if(abs(dx) < DEADZONE && abs(dy) < DEADZONE){
    menuNavArmed = true;
  }

  if(!menuNavArmed) return;
  if(millis() - lastMenuMove < 180) return;

  uint8_t oldSel = currentSelection;
  bool moved = false;

  if(dy < -MENU_THRESH){
    currentSelection = (currentSelection == 0) ? (menuCount - 1) : (currentSelection - 1);
    moved = true;
  } else if(dy > MENU_THRESH){
    currentSelection = (currentSelection + 1) % menuCount;
    moved = true;
  }

  if(moved){
    drawMenuItem(oldSel, false);
    drawMenuItem(currentSelection, true);
    menuNavArmed = false;
    lastMenuMove = millis();
  }
}

// ==================== Taste ====================
bool buttonPressedEdge(){
  bool pressed = (digitalRead(JOY_SW) == LOW);
  if(!pressed) return false;

  unsigned long now = millis();
  if(now - lastButtonPress > 250){
    if(now - lastClickTime < 600) {
      buttonClickCount++;
    } else {
      buttonClickCount = 1;
    }
    lastClickTime = now;
    lastButtonPress = now;
    return true;
  }
  return false;
}

bool tripleClickDetected(){
  return (buttonClickCount >= 3 && (millis() - lastClickTime) < 800);
}

bool isButtonHeld(uint16_t msHold){
  static bool wasPressed = false;
  static unsigned long pressStart = 0;

  bool pressed = (digitalRead(JOY_SW) == LOW);

  if(pressed && !wasPressed){
    wasPressed = true;
    pressStart = millis();
  }

  if(!pressed && wasPressed){
    wasPressed = false;
    if(millis() - pressStart < msHold){
      return false;
    }
  }

  if(pressed && wasPressed && (millis() - pressStart) > msHold){
    while(digitalRead(JOY_SW) == LOW) { delay(10); }
    wasPressed = false;
    return true;
  }

  return false;
}

// ==================== Drucksensor ====================
float readPressureVoltage(){
  const int N = 64;
  long sum = 0;
  for(int i=0;i<N;i++){
    sum += analogRead(PRESS_PIN);
    delayMicroseconds(80);
  }
  rawADC = (int)(sum / N);
  return (rawADC / 4095.0f) * 3.3f;
}

void calibratePressureZero(){
  pressCalibrating = true;

  tft.fillRoundRect(14, CONTENT_Y+105, SCREEN_W-28, 80, 12, ORANGE_ACCENT);
  tft.drawRoundRect(14, CONTENT_Y+105, SCREEN_W-28, 80, 12, ORANGE_LIGHT);
  setTxt(2, BLACK_COLOR);
  tft.setCursor(22, CONTENT_Y+120);
  tft.print("Kalibrierung...");
  setTxt(1, BLACK_COLOR);
  tft.setCursor(22, CONTENT_Y+145);
  tft.print("Sensor in offener Luft");

  const int N = 100;
  float sumV = 0;
  for(int i=0;i<N;i++){
    sumV += readPressureVoltage();
    delay(15);
  }

  settings.zeroVoltage = sumV / N;
  pressFilt = -1.0f;
  pressOK = false;

  preferences.putFloat("zeroVoltage", settings.zeroVoltage);

  pressCalibrating = false;
  delay(300);
}

void updatePressure(){
  if(pressCalibrating) return;

  pressVolt = readPressureVoltage();

  float sensorVoltage = pressVolt / settings.dividerRatio;
  float dV = sensorVoltage - settings.zeroVoltage;
  if(dV < 0) dV = 0;

  pressUnits = dV * settings.pressureScale;

  if(pressFilt < 0) {
    pressFilt = pressUnits;
  } else {
    pressFilt = pressFilt * (1.0f - PRESS_ALPHA) + pressUnits * PRESS_ALPHA;
  }

  if(!pressOK && pressFilt >= (settings.pressureThreshold + PRESS_HYST)) {
    pressOK = true;
  } else if(pressOK && pressFilt <= (settings.pressureThreshold - PRESS_HYST)) {
    pressOK = false;
  }
}

// ==================== Modus UI ====================
void splashMode(const char* name){
  tft.fillScreen(ORANGE_BG);
  drawHeader(name);
  tft.fillRoundRect(18, CONTENT_Y+50, SCREEN_W-36, 80, 14, ORANGE_ACCENT);
  tft.drawRoundRect(18, CONTENT_Y+50, SCREEN_W-36, 80, 14, ORANGE_LIGHT);
  setTxt(2, BLACK_COLOR);
  tft.setCursor(28, CONTENT_Y+82);
  tft.print(name);
  delay(300);
}

void drawSensorModeUI(){
  tft.fillScreen(ORANGE_BG);
  drawHeader("Sensor-Modus");
  tft.fillRoundRect(14, CONTENT_Y, SCREEN_W-28, 170, 14, PANEL_BG);
  tft.drawRoundRect(14, CONTENT_Y, SCREEN_W-28, 170, 14, LINE_COLOR);

  setTxt(2, TEXT_COLOR);
  tft.setCursor(26, CONTENT_Y+40);
  tft.print("Warte auf Signal");

  drawFooter("3x Klick: Zurück");
}

// ==================== Sensor-Modus (GEÄNDERT: Active-LOW + Re-Trigger-Sperre) ====================
void runSensorMode(){
  static bool activated = false;

  // Active-LOW: Trigger ist aktiv, wenn Pin LOW ist
  bool trig = (digitalRead(SENSOR_TRIG_PIN) == LOW);

  // Sobald Sensor wieder HIGH ist, wieder scharf schalten
  if(!trig) sensorArmed = true;  // !trig => Pin HIGH

  // Nur einmal pro "LOW-Phase" auslösen
  if(sensorArmed && trig && !relayActive){
    activateRelay();
    relayTimer = millis() + settings.sensorDuration;
    activated = true;
    sensorArmed = false;

    tft.fillRoundRect(18, CONTENT_Y+120, SCREEN_W-36, 44, 12, ORANGE_ACCENT);
    tft.drawRoundRect(18, CONTENT_Y+120, SCREEN_W-36, 44, 12, ORANGE_LIGHT);
    setTxt(2, BLACK_COLOR);
    tft.setCursor(52, CONTENT_Y+134);
    tft.print("AKTIVIERT!");
  }

  if(relayActive && activated && (long)(millis() - relayTimer) >= 0){
    deactivateRelay();
    activated = false;
    drawSensorModeUI();
  }
}

void drawJoystickModeUI(){
  tft.fillScreen(ORANGE_BG);
  drawHeader("Joystick-Modus");
  tft.fillRoundRect(14, CONTENT_Y, SCREEN_W-28, 170, 14, PANEL_BG);
  tft.drawRoundRect(14, CONTENT_Y, SCREEN_W-28, 170, 14, LINE_COLOR);

  setTxt(2, TEXT_COLOR);
  tft.setCursor(22, CONTENT_Y+30);
  tft.print("Joystick halten");
  tft.setCursor(22, CONTENT_Y+55);
  tft.print("Relais bleibt EIN");

  drawFooter("3x Klick: Zurück");
}

void drawJoyStatus(bool on){
  uint16_t col = on ? GREEN_COLOR : ORANGE_DARK;
  tft.fillRoundRect(18, CONTENT_Y+120, SCREEN_W-36, 44, 12, col);
  tft.drawRoundRect(18, CONTENT_Y+120, SCREEN_W-36, 44, 12, ORANGE_LIGHT);
  setTxt(2, BLACK_COLOR);
  tft.setCursor(80, CONTENT_Y+134);
  tft.print(on ? "EIN" : "AUS");
}

void runJoystickMode(){
  int dx = joyDX();
  int dy = joyDY();

  long dist2 = (long)dx*dx + (long)dy*dy;
  long on2  = (long)settings.joyOnThreshold  * (long)settings.joyOnThreshold;
  long off2 = (long)settings.joyOffThreshold * (long)settings.joyOffThreshold;

  if(!joyHoldLatched && dist2 > on2){
    joyHoldLatched = true;
    activateRelay();
    drawJoyStatus(true);
  }
  else if(joyHoldLatched && dist2 < off2){
    joyHoldLatched = false;
    deactivateRelay();
    drawJoyStatus(false);
  }
}

void drawAutoModeUI(){
  tft.fillScreen(ORANGE_BG);
  drawHeader("Automatik");
  tft.fillRoundRect(14, CONTENT_Y, SCREEN_W-28, 170, 14, PANEL_BG);
  tft.drawRoundRect(14, CONTENT_Y, SCREEN_W-28, 170, 14, LINE_COLOR);

  setTxt(2, TEXT_COLOR);
  tft.setCursor(44, CONTENT_Y+30);
  tft.print("AUTO PULS");

  setTxt(1, TEXT_COLOR);
  tft.setCursor(52, CONTENT_Y+58);
  tft.printf("EIN %ds / AUS %ds", settings.autoOnTime/1000, settings.autoOffTime/1000);

  drawFooter("3x Klick: Nur wenn AUS");
}

void runAutoMode(){
  if(autoFirst){
    autoFirst = false;
    autoState = false;
    deactivateRelay();
    autoTimer = millis();
    drawAutoModeUI();
  }

  unsigned long now = millis();
  uint16_t interval = autoState ? settings.autoOnTime : settings.autoOffTime;

  if(now - autoTimer >= interval){
    autoState = !autoState;
    autoTimer = now;

    if(autoState) activateRelay();
    else deactivateRelay();

    tft.fillRoundRect(18, CONTENT_Y+95, SCREEN_W-36, 50, 14, autoState ? ORANGE_ACCENT : ORANGE_DARK);
    tft.drawRoundRect(18, CONTENT_Y+95, SCREEN_W-36, 50, 14, ORANGE_LIGHT);
    setTxt(2, autoState ? BLACK_COLOR : ORANGE_LIGHT);
    tft.setCursor(62, CONTENT_Y+112);
    tft.print(autoState ? "STROM EIN" : "STROM AUS");
  }
}

void drawPressureUI(){
  tft.fillScreen(ORANGE_BG);
  drawHeader("Druckanzeige");
  tft.fillRoundRect(14, CONTENT_Y, SCREEN_W-28, 210, 14, PANEL_BG);
  tft.drawRoundRect(14, CONTENT_Y, SCREEN_W-28, 210, 14, LINE_COLOR);
  drawFooter("Halten=Null | L/R=Schwelle | 3=Zurück");
}

void drawPressureGauge(){
  tft.fillRoundRect(18, CONTENT_Y+6, SCREEN_W-36, 198, 12, PANEL_BG);

  setTxt(2, TEXT_COLOR);
  tft.setCursor(24, CONTENT_Y+18);
  tft.print("Druck (relativ)");

  setTxt(2, ORANGE_LIGHT);
  tft.setCursor(24, CONTENT_Y+45);
  tft.print(pressFilt, 3);

  setTxt(1, TEXT_COLOR);
  tft.setCursor(24, CONTENT_Y+75);
  tft.print("ADC: "); tft.print(rawADC);
  tft.print("  V: "); tft.print(pressVolt, 3);

  tft.setCursor(24, CONTENT_Y+92);
  tft.print("NullV: "); tft.print(settings.zeroVoltage, 3);

  tft.setCursor(24, CONTENT_Y+110);
  tft.print("Schwelle: "); tft.print(settings.pressureThreshold, 3);

  uint16_t sc = pressOK ? GREEN_COLOR : RED_COLOR;
  const char* st = pressOK ? "OK" : "NICHT OK";

  tft.fillRoundRect(18, CONTENT_Y+130, SCREEN_W-36, 44, 12, sc);
  tft.drawRoundRect(18, CONTENT_Y+130, SCREEN_W-36, 44, 12, ORANGE_LIGHT);
  setTxt(2, BLACK_COLOR);
  tft.setCursor(84, CONTENT_Y+144);
  tft.print(st);

  int barX = 24, barY = CONTENT_Y+182, barW = 192, barH = 14;
  tft.drawRect(barX, barY, barW, barH, LINE_COLOR);
  tft.fillRect(barX+1, barY+1, barW-2, barH-2, BLACK_COLOR);

  float visMax = max(settings.pressureThreshold * 2.0f, 1.0f);
  float n = clampf(pressFilt, 0.0f, visMax);
  int fw = (int)((n / visMax) * (barW - 2));
  fw = clampi(fw, 0, barW-2);
  tft.fillRect(barX+1, barY+1, fw, barH-2, sc);
}

void runPressureMode(){
  static unsigned long lastDraw = 0;
  static unsigned long lastThreshChange = 0;

  updatePressure();

  int dx = joyDX();
  if(abs(dx) > MENU_THRESH && (millis() - lastThreshChange > 200)){
    if(dx > MENU_THRESH) settings.pressureThreshold += 0.02f;
    else settings.pressureThreshold -= 0.02f;

    settings.pressureThreshold = clampf(settings.pressureThreshold, 0.00f, 5.00f);
    preferences.putFloat("pressureThreshold", settings.pressureThreshold);
    lastThreshChange = millis();
  }

  if(isButtonHeld(1500)){
    calibratePressureZero();
  }

  if(millis() - lastDraw > 250){
    drawPressureGauge();
    lastDraw = millis();
  }
}

// ==================== Web Server HTML ====================
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Kabelglaetter Steuerung</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #2a1f0c 0%, #1a1408 100%);
            color: #fff;
            padding: 20px;
            min-height: 100vh;
        }
        .container { max-width: 1200px; margin: 0 auto; }
        header {
            background: linear-gradient(90deg, #e65c00 0%, #ff8800 100%);
            padding: 20px;
            border-radius: 10px;
            margin-bottom: 20px;
            box-shadow: 0 4px 6px rgba(0,0,0,0.3);
        }
        h1 { font-size: 2.2em; margin-bottom: 5px; }
        .subtitle { font-size: 0.9em; opacity: 0.9; }
        .grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            gap: 20px;
        }
        .card {
            background: rgba(255, 140, 0, 0.1);
            border: 1px solid rgba(255, 140, 0, 0.3);
            border-radius: 10px;
            padding: 20px;
            backdrop-filter: blur(10px);
        }
        .card h2 { color: #ff8800; margin-bottom: 15px; font-size: 1.4em; }
        .status-item {
            display: flex;
            justify-content: space-between;
            margin-bottom: 8px;
            padding-bottom: 8px;
            border-bottom: 1px solid rgba(255,255,255,0.1);
        }
        .status-label { font-weight: bold; color: #ffcc80; }
        .status-value { font-family: monospace; font-size: 1.1em; }
        .status-on { color: #4caf50; }
        .status-off { color: #f44336; }
        .status-ok { color: #4caf50; }
        .status-notok { color: #f44336; }
        .oscilloscope {
            background: rgba(0, 0, 0, 0.5);
            border-radius: 5px;
            padding: 10px;
            margin-top: 10px;
        }
        canvas {
            width: 100%;
            height: 100px;
            background: #000;
            border-radius: 5px;
            display: block;
        }
        input[type="range"] {
            width: 100%;
            margin: 10px 0;
            background: rgba(255,140,0,0.2);
            border-radius: 5px;
        }
        input[type="number"] {
            width: 100px;
            padding: 5px;
            border-radius: 5px;
            border: 1px solid #ff8800;
            background: rgba(0,0,0,0.5);
            color: white;
        }
        button {
            background: linear-gradient(90deg, #e65c00 0%, #ff8800 100%);
            color: white;
            border: none;
            padding: 10px 20px;
            border-radius: 5px;
            cursor: pointer;
            font-weight: bold;
            margin: 5px;
            transition: transform 0.2s;
        }
        button:hover { transform: translateY(-2px); box-shadow: 0 4px 8px rgba(0,0,0,0.3); }
        button:active { transform: translateY(0); }
        button.danger { background: linear-gradient(90deg, #c62828 0%, #f44336 100%); }
        button.success { background: linear-gradient(90deg, #2e7d32 0%, #4caf50 100%); }
        .setting-group { margin-bottom: 15px; }
        .setting-label { display: block; margin-bottom: 5px; color: #ffcc80; }
        .hidden { display: none; }
        .notification {
            position: fixed;
            top: 20px;
            right: 20px;
            background: #4caf50;
            color: white;
            padding: 15px 20px;
            border-radius: 5px;
            box-shadow: 0 4px 6px rgba(0,0,0,0.3);
            animation: slideIn 0.3s ease;
        }
        @keyframes slideIn {
            from { transform: translateX(100%); opacity: 0; }
            to { transform: translateX(0); opacity: 1; }
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>Kabelglaetter Steuerung</h1>
            <div class="subtitle">ESP32 Web Interface - IP: 192.168.4.1</div>
        </header>

        <div class="grid">
            <div class="card">
                <h2>Status</h2>
                <div class="status-item">
                    <span class="status-label">Modus:</span>
                    <span class="status-value" id="mode">-</span>
                </div>
                <div class="status-item">
                    <span class="status-label">Relais:</span>
                    <span class="status-value" id="relay">-</span>
                </div>
                <div class="status-item">
                    <span class="status-label">Druck:</span>
                    <span class="status-value" id="pressure">-</span>
                </div>
                <div class="status-item">
                    <span class="status-label">Status:</span>
                    <span class="status-value" id="pressureStatus">-</span>
                </div>
                <div class="status-item">
                    <span class="status-label">ADC / Spannung:</span>
                    <span class="status-value" id="adcVolt">-</span>
                </div>
                <div class="oscilloscope">
                    <h3>Motoraktivität</h3>
                    <canvas id="oscilloscope"></canvas>
                </div>
            </div>

            <div class="card">
                <h2>Einstellungen</h2>

                <div class="setting-group">
                    <label class="setting-label">Druckschwelle: <span id="thresholdValue">0.35</span></label>
                    <input type="range" id="threshold" min="0" max="5" step="0.01" value="0.35">
                </div>

                <div class="setting-group">
                    <label class="setting-label">Druckskala: <span id="scaleValue">2.0</span></label>
                    <input type="range" id="scale" min="0.1" max="10" step="0.1" value="2.0">
                </div>

                <div class="setting-group">
                    <label class="setting-label">Joystick ON Schwelle: <input type="number" id="joyOn" min="100" max="2000" value="650"> (höher)</label>
                    <label class="setting-label">Joystick OFF Schwelle: <input type="number" id="joyOff" min="100" max="2000" value="420"> (niedriger)</label>
                </div>

                <div class="setting-group">
                    <label class="setting-label">Auto EIN Zeit (ms): <input type="number" id="autoOn" min="100" max="30000" value="5000"></label>
                    <label class="setting-label">Auto AUS Zeit (ms): <input type="number" id="autoOff" min="100" max="30000" value="2000"></label>
                </div>

                <div class="setting-group">
                    <label class="setting-label">Sensor Dauer (ms): <input type="number" id="sensorDur" min="100" max="60000" value="10000"></label>
                </div>

                <div class="setting-group">
                    <label class="setting-label">Spannungsteiler Verhältnis (1.0=kein Teiler): <input type="number" id="divider" min="0.1" max="1.0" step="0.01" value="1.00"></label>
                </div>

                <button onclick="saveSettings()">Einstellungen speichern</button>
                <button class="success" onclick="calibrateZero()">Nullpunkt kalibrieren</button>
                <button class="danger" onclick="toggleRelay()">Relais Test EIN/AUS</button>
            </div>
        </div>
    </div>

    <div id="notification" class="notification hidden"></div>

    <script>
        let waveData = new Array(100).fill(0);
        let waveIndex = 0;
        const canvas = document.getElementById('oscilloscope');
        const ctx = canvas.getContext('2d');

        function updateStatus(data) {
            document.getElementById('mode').textContent = data.mode;
            document.getElementById('relay').textContent = data.relay;
            document.getElementById('relay').className = 'status-value ' + (data.relay ? 'status-on' : 'status-off');
            document.getElementById('pressure').textContent = data.pressureFilt.toFixed(3);
            document.getElementById('pressureStatus').textContent = data.pressureOK ? 'OK' : 'NICHT OK';
            document.getElementById('pressureStatus').className = 'status-value ' + (data.pressureOK ? 'status-ok' : 'status-notok');
            document.getElementById('adcVolt').textContent = data.adc + ' / ' + data.volt.toFixed(3) + 'V';

            waveData[waveIndex] = data.pressureFilt * 10;
            waveIndex = (waveIndex + 1) % waveData.length;

            drawOscilloscope(data.relay);

            if (!document.getElementById('threshold').classList.contains('dragging')) {
                document.getElementById('threshold').value = data.threshold;
                document.getElementById('thresholdValue').textContent = data.threshold.toFixed(2);
            }
            if (!document.getElementById('scale').classList.contains('dragging')) {
                document.getElementById('scale').value = data.scale;
                document.getElementById('scaleValue').textContent = data.scale.toFixed(1);
            }
            document.getElementById('joyOn').value = data.joyOn;
            document.getElementById('joyOff').value = data.joyOff;
            document.getElementById('autoOn').value = data.autoOn;
            document.getElementById('autoOff').value = data.autoOff;
            document.getElementById('sensorDur').value = data.sensorDuration;
            document.getElementById('divider').value = data.dividerRatio;
        }

        function drawOscilloscope(relayActive) {
            const width = canvas.width;
            const height = canvas.height;

            ctx.fillStyle = '#000';
            ctx.fillRect(0, 0, width, height);

            ctx.strokeStyle = '#333';
            ctx.lineWidth = 1;

            for (let x = 0; x < width; x += width / 10) {
                ctx.beginPath();
                ctx.moveTo(x, 0);
                ctx.lineTo(x, height);
                ctx.stroke();
            }

            for (let y = 0; y < height; y += height / 5) {
                ctx.beginPath();
                ctx.moveTo(0, y);
                ctx.lineTo(width, y);
                ctx.stroke();
            }

            if (relayActive) {
                ctx.strokeStyle = '#0ff';
                ctx.lineWidth = 2;
                ctx.beginPath();

                for (let i = 0; i < waveData.length; i++) {
                    const x = (i * width) / waveData.length;
                    const y = height / 2 - waveData[(i + waveIndex) % waveData.length];

                    if (i === 0) ctx.moveTo(x, y);
                    else ctx.lineTo(x, y);
                }
                ctx.stroke();
            } else {
                ctx.strokeStyle = '#666';
                ctx.lineWidth = 2;
                ctx.beginPath();
                ctx.moveTo(0, height / 2);
                ctx.lineTo(width, height / 2);
                ctx.stroke();
            }
        }

        function saveSettings() {
            const settings = {
                threshold: parseFloat(document.getElementById('threshold').value),
                scale: parseFloat(document.getElementById('scale').value),
                joyOn: parseInt(document.getElementById('joyOn').value),
                joyOff: parseInt(document.getElementById('joyOff').value),
                autoOn: parseInt(document.getElementById('autoOn').value),
                autoOff: parseInt(document.getElementById('autoOff').value),
                sensorDuration: parseInt(document.getElementById('sensorDur').value),
                dividerRatio: parseFloat(document.getElementById('divider').value)
            };

            fetch('/api/settings', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(settings)
            })
            .then(response => response.json())
            .then(data => { showNotification('Einstellungen gespeichert!'); })
            .catch(error => {
                showNotification('Fehler beim Speichern!', true);
                console.error('Error:', error);
            });
        }

        function calibrateZero() {
            fetch('/api/calibrateZero', { method: 'POST' })
            .then(response => response.json())
            .then(data => { showNotification('Nullpunkt kalibriert!'); })
            .catch(error => {
                showNotification('Fehler bei Kalibrierung!', true);
                console.error('Error:', error);
            });
        }

        function toggleRelay() {
            fetch('/api/toggleRelay', { method: 'POST' })
            .then(response => response.json())
            .then(data => { showNotification('Relais ' + (data.relay ? 'EIN' : 'AUS')); })
            .catch(error => {
                showNotification('Fehler!', true);
                console.error('Error:', error);
            });
        }

        function showNotification(message, isError = false) {
            const notification = document.getElementById('notification');
            notification.textContent = message;
            notification.style.background = isError ? '#f44336' : '#4caf50';
            notification.classList.remove('hidden');

            setTimeout(() => { notification.classList.add('hidden'); }, 3000);
        }

        function pollStatus() {
            fetch('/api/status')
                .then(response => response.json())
                .then(data => updateStatus(data))
                .catch(error => console.error('Error:', error));
        }

        document.getElementById('threshold').addEventListener('mousedown', () => {
            document.getElementById('threshold').classList.add('dragging');
        });
        document.getElementById('threshold').addEventListener('mouseup', () => {
            document.getElementById('threshold').classList.remove('dragging');
        });

        document.getElementById('scale').addEventListener('mousedown', () => {
            document.getElementById('scale').classList.add('dragging');
        });
        document.getElementById('scale').addEventListener('mouseup', () => {
            document.getElementById('scale').classList.remove('dragging');
        });

        document.getElementById('threshold').addEventListener('input', (e) => {
            document.getElementById('thresholdValue').textContent = parseFloat(e.target.value).toFixed(2);
        });

        document.getElementById('scale').addEventListener('input', (e) => {
            document.getElementById('scaleValue').textContent = parseFloat(e.target.value).toFixed(1);
        });

        setInterval(pollStatus, 250);
        pollStatus();

        function resizeCanvas() {
            canvas.width = canvas.clientWidth;
            canvas.height = canvas.clientHeight;
        }

        window.addEventListener('resize', resizeCanvas);
        resizeCanvas();
    </script>
</body>
</html>
)rawliteral";

// ==================== Web Server Handlers ====================
String getJsonValue(String json, String key) {
  int start = json.indexOf("\"" + key + "\":");
  if (start == -1) return "0";
  start += key.length() + 3;
  int end = json.indexOf(",", start);
  if (end == -1) end = json.indexOf("}", start);
  return json.substring(start, end);
}

void handleRoot() {
  server.send(200, "text/html", htmlPage);
}

void handleStatus() {
  String json = "{";
  json += "\"mode\":\"" + String(activeMode == -1 ? "Menü" :
           activeMode == 0 ? "Sensor" :
           activeMode == 1 ? "Joystick" :
           activeMode == 2 ? "Automatik" : "Druck") + "\",";
  json += "\"relay\":" + String(relayActive ? "true" : "false") + ",";
  json += "\"pressureFilt\":" + String(pressFilt, 3) + ",";
  json += "\"pressureRaw\":" + String(pressUnits, 3) + ",";
  json += "\"adc\":" + String(rawADC) + ",";
  json += "\"volt\":" + String(pressVolt, 3) + ",";
  json += "\"zeroV\":" + String(settings.zeroVoltage, 3) + ",";
  json += "\"pressureOK\":" + String(pressOK ? "true" : "false") + ",";
  json += "\"threshold\":" + String(settings.pressureThreshold, 2) + ",";
  json += "\"scale\":" + String(settings.pressureScale, 1) + ",";
  json += "\"joyOn\":" + String(settings.joyOnThreshold) + ",";
  json += "\"joyOff\":" + String(settings.joyOffThreshold) + ",";
  json += "\"autoOn\":" + String(settings.autoOnTime) + ",";
  json += "\"autoOff\":" + String(settings.autoOffTime) + ",";
  json += "\"sensorDuration\":" + String(settings.sensorDuration) + ",";
  json += "\"dividerRatio\":" + String(settings.dividerRatio, 2);
  json += "}";

  server.send(200, "application/json", json);
}

void handleSettings() {
  if (server.hasArg("plain")) {
    String json = server.arg("plain");

    settings.pressureThreshold = getJsonValue(json, "threshold").toFloat();
    settings.pressureScale = getJsonValue(json, "scale").toFloat();
    settings.joyOnThreshold = getJsonValue(json, "joyOn").toInt();
    settings.joyOffThreshold = getJsonValue(json, "joyOff").toInt();
    settings.autoOnTime = getJsonValue(json, "autoOn").toInt();
    settings.autoOffTime = getJsonValue(json, "autoOff").toInt();
    settings.sensorDuration = getJsonValue(json, "sensorDuration").toInt();
    settings.dividerRatio = getJsonValue(json, "dividerRatio").toFloat();

    preferences.putFloat("pressureThreshold", settings.pressureThreshold);
    preferences.putFloat("pressureScale", settings.pressureScale);
    preferences.putInt("joyOnThreshold", settings.joyOnThreshold);
    preferences.putInt("joyOffThreshold", settings.joyOffThreshold);
    preferences.putInt("autoOnTime", settings.autoOnTime);
    preferences.putInt("autoOffTime", settings.autoOffTime);
    preferences.putInt("sensorDuration", settings.sensorDuration);
    preferences.putFloat("dividerRatio", settings.dividerRatio);

    server.send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
  }
}

void handleCalibrateZero() {
  calibratePressureZero();
  server.send(200, "application/json", "{\"status\":\"calibrated\"}");
}

void handleToggleRelay() {
  if (relayActive) deactivateRelay();
  else activateRelay();

  server.send(200, "application/json", "{\"relay\":" + String(relayActive ? "true" : "false") + "}");
}

// ==================== NVS Initialisierung ====================
void loadSettings() {
  settings.pressureThreshold = preferences.getFloat("pressureThreshold", 0.35f);
  settings.pressureScale = preferences.getFloat("pressureScale", 2.0f);
  settings.joyOnThreshold = preferences.getInt("joyOnThreshold", 650);
  settings.joyOffThreshold = preferences.getInt("joyOffThreshold", 420);
  settings.autoOnTime = preferences.getInt("autoOnTime", 5000);
  settings.autoOffTime = preferences.getInt("autoOffTime", 2000);
  settings.sensorDuration = preferences.getInt("sensorDuration", 10000);
  settings.dividerRatio = preferences.getFloat("dividerRatio", 1.0f);
  settings.zeroVoltage = preferences.getFloat("zeroVoltage", 0.0f);
}

// ==================== Oszilloskop Animation Update ====== #Amir #Mobasheraghdam ==============
void updateWaveform() {
  if(millis() - lastWaveUpdate >= WAVE_UPDATE_MS){
    if(relayActive){
      waveValues[waveIndex] = (int)(sin(waveIndex * 0.3) * 10 + random(-2, 2));
    } else {
      waveValues[waveIndex] = 0;
    }
    waveIndex = (waveIndex + 1) % WAVE_POINTS;
    lastWaveUpdate = millis();
    drawWaveform();
  }
}

// ==================== Setup / Loop Amir Mobasheraghdam uni bonn ====================
void setup(){
  Serial.begin(115200);

  pinMode(JOY_SW, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);

  // ✅ Sensor jetzt Active-LOW -> PullUp nutzen
  pinMode(SENSOR_TRIG_PIN, INPUT_PULLUP);

  // ADC
  analogReadResolution(12);
  analogSetPinAttenuation(JOY_X, ADC_11db);
  analogSetPinAttenuation(JOY_Y, ADC_11db);
  analogSetPinAttenuation(PRESS_PIN, ADC_11db);

  // TFT
  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(2);

  // Einstellungen laden
  preferences.begin(NVS_NAMESPACE, false);
  loadSettings();

  // Startbildschirm
  tft.fillScreen(BLACK_COLOR);
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(2);
  tft.setCursor(40, 60);
  tft.print("W-glaetter");
  tft.setTextSize(1);
  tft.setCursor(60, 90);
  tft.print("Starte...");
  delay(500);

  // Joystick kalibrieren
  tft.fillScreen(BLACK_COLOR);
  tft.setTextSize(2);
  tft.setCursor(10, 70);
  tft.print("Kalibriere...");
  tft.setTextSize(1);
  tft.setCursor(10, 98);
  tft.print("Joystick zentrieren!");
  delay(400);
  calibrateJoystick();

  // Drucksensor Nullpunkt kalibrieren
  tft.fillScreen(BLACK_COLOR);
  tft.setTextSize(2);
  tft.setCursor(10, 70);
  tft.print("Druck Nullpunkt...");
  tft.setTextSize(1);
  tft.setCursor(10, 98);
  tft.print("Sensor in offener Luft");
  delay(500);
  calibratePressureZero();

  // WiFi AP starten
  WiFi.softAP(apSSID, apPassword);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  // Web Server
  server.on("/", handleRoot);
  server.on("/api/status", handleStatus);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/calibrateZero", HTTP_POST, handleCalibrateZero);
  server.on("/api/toggleRelay", HTTP_POST, handleToggleRelay);
  server.begin();

  // Hauptmenü
  drawMainMenu();

  Serial.println("WiFi AP gestartet:");
  Serial.print("SSID: "); Serial.println(apSSID);
  Serial.print("IP: "); Serial.println(WiFi.softAPIP());
}

void loop(){
  server.handleClient();

  updateJoyFilter();
  updateWaveform();

  if(buttonPressedEdge()){
    if(activeMode == -1){
      if(buttonClickCount == 1){
        activeMode = currentSelection;
        splashMode(menuItems[currentSelection]);

        switch(activeMode){
          case 0:
            // ✅ Sensor beim Betreten wieder "scharf" machen
            sensorArmed = true;
            drawSensorModeUI();
            break;
          case 1:
            joyHoldLatched = false;
            deactivateRelay();
            drawJoystickModeUI();
            drawJoyStatus(false);
            break;
          case 2:
            autoFirst = true;
            drawAutoModeUI();
            break;
          case 3:
            drawPressureUI();
            drawPressureGauge();
            break;
        }
      }
    } else {
      if(tripleClickDetected()){
        if(activeMode != 2 || !relayActive){
          deactivateRelay();
          activeMode = -1;
          buttonClickCount = 0;
          drawMainMenu();
          return;
        }
      }
    }
  }

  if(activeMode == -1){
    handleMainMenuNav();
  } else {
    switch(activeMode){
      case 0: runSensorMode(); break;
      case 1: runJoystickMode(); break;
      case 2: runAutoMode(); break;
      case 3: runPressureMode(); break;
    }
  }

  delay(10);
}
