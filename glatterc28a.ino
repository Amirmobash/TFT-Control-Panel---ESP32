#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <math.h>

// ==================== DEBUG KONFIGURATION ====================
#define DEBUG_SERIAL 1

#if DEBUG_SERIAL
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTF(...)
#endif

// ==================== HARDWARE DEFINITIONEN ====================
#define TFT_SCK   18
#define TFT_MOSI  23
#define TFT_MISO  19
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST    4

#define JOY_Y     34
#define JOY_X     35
#define JOY_SW    27
#define PRESS_PIN 33
#define RELAY_PIN 25
#define SENSOR_TRIG_PIN 32

#define RELAY_ON   LOW
#define RELAY_OFF  HIGH

// ==================== BILDSCHIRM KONSTANTEN ====================
#define SCREEN_W  240
#define SCREEN_H  320
#define HEADER_H  44
#define FOOTER_H  28
#define CONTENT_Y (HEADER_H + 6)
#define CONTENT_H (SCREEN_H - HEADER_H - FOOTER_H - 12)

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

// ==================== FARBPALETTE (Premium Orange) ====================
static inline uint16_t swapRGB(uint16_t c) {
  return (c & 0x07E0) | ((c & 0xF800) >> 11) | ((c & 0x001F) << 11);
}

#define ORANGE_DARK_B    0x0280
#define ORANGE_MAIN_B    0x02A0
#define ORANGE_ACCENT_B  0x0300
#define ORANGE_LIGHT_B   0x03E0
#define PANEL_BG_B       0x2104
#define BG_DARK_B        0x0008

#define ORANGE_DARK   swapRGB(ORANGE_DARK_B)
#define ORANGE_MAIN   swapRGB(ORANGE_MAIN_B)
#define ORANGE_ACCENT swapRGB(ORANGE_ACCENT_B)
#define ORANGE_LIGHT  swapRGB(ORANGE_LIGHT_B)
#define PANEL_BG      swapRGB(PANEL_BG_B)
#define BG_DARK       swapRGB(BG_DARK_B)

#define TEXT_WHITE    0xFFFF
#define TEXT_LIGHT    0xFFDF
#define TEXT_DARK     0x7BEF
#define BLACK         0x0000
#define GREEN         0x07E0
#define GREEN_DARK    0x03C0
#define RED           0xF800
#define RED_DARK      0x7800
#define YELLOW        0xFFE0
#define BLUE          0x001F
#define CYAN          0x07FF
#define LINE_COLOR    0x39E7
#define WAVE_COLOR    0x07FF

// ==================== DRUCK-STATUS ENUM ====================
enum PressureStatus {
  PRESSURE_OK = 0,
  PRESSURE_TOO_LOW = 1,
  PRESSURE_TOO_HIGH = 2
};

// ==================== EINSTELLUNGEN (NVS) ====================
Preferences preferences;
#define NVS_NAMESPACE "W_Glaetter"

struct Settings {
  // Druck-Kalibrierung
  float pressureScale;        // bar/V (automatisch durch Skalierungskalibrierung)
  float dividerRatio;         // Spannungsteiler-Verhältnis (1.0 = kein Teiler)
  float zeroVoltage;          // Spannung bei 0 bar (am ADC gemessen)
  float okMin;                // Untergrenze für OK (bar)
  float okMax;                // Obergrenze für OK (bar)
  float pressureThreshold;    // Nur für Anzeige (historisch)
  
  // Joystick
  int joyOnThreshold;
  int joyOffThreshold;
  
  // Automatik-Modus
  int autoOnTime;             // ms
  int autoOffTime;            // ms
  
  // Sensor-Modus
  int sensorDuration;         // ms
  
  // Zwei-Punkt Kalibrierung Status
  bool scaleCalibrated;       // Skalierung wurde kalibriert
  float knownPressure;        // Bekannter Druck für Skalierungskalibrierung
};

Settings settings = {
  .pressureScale = 1.0f,
  .dividerRatio = 1.0f,
  .zeroVoltage = 0.0f,
  .okMin = 0.40f,
  .okMax = 0.50f,
  .pressureThreshold = 0.35f,
  .joyOnThreshold = 650,
  .joyOffThreshold = 420,
  .autoOnTime = 5000,
  .autoOffTime = 2000,
  .sensorDuration = 10000,
  .scaleCalibrated = false,
  .knownPressure = 0.0f
};

// ==================== GLOBALE VARIABLEN ====================
const char* menuItems[] = {" Sensor-Modus", " Joystick-Modus", " Automatik", " Druckanzeige"};
const uint8_t menuCount = sizeof(menuItems) / sizeof(menuItems[0]);
uint8_t currentMenuSelection = 0;
int8_t activeMode = -1;  // -1 = Menü, 0-3 = Modi

bool relayActive = false;
unsigned long relayTimer = 0;

// Joystick
int centerX = 2048, centerY = 2048;
float fx = 2048, fy = 2048;
const int DEADZONE = 220;
const int MENU_THRESH = 520;
const float JOY_ALPHA = 0.25f;
bool menuNavArmed = true;
unsigned long lastMenuMove = 0;
bool joyHoldLatched = false;

// Taste
unsigned long lastButtonPress = 0;
unsigned long lastClickTime = 0;
uint8_t buttonClickCount = 0;

// Drucksensor
float pressVolt = 0.0f;          // Gemessene Spannung am ADC (0-3.3V)
float pressUnits = 0.0f;         // Rohdruck in bar
float pressFilt = -1.0f;         // Gefilterter Druck in bar
PressureStatus pressureStatus = PRESSURE_TOO_LOW;
int rawADC = 0;
bool pressCalibrating = false;
const float PRESS_ALPHA = 0.15f;
const float PRESS_HYST = 0.01f;  // Hysterese für Status-Übergänge

// Automatik-Modus
bool autoState = false;
bool autoFirst = true;
unsigned long autoTimer = 0;

// Web Server
WebServer server(80);
const char* apSSID = "Kabelglaetter";
const char* apPassword = "12345678";
IPAddress apIP(192, 168, 4, 1);

// Oszilloskop Animation
const int WAVE_POINTS = 32;
int waveValues[WAVE_POINTS] = {0};
int waveIndex = 0;
unsigned long lastWaveUpdate = 0;
const int WAVE_UPDATE_MS = 50;

// Sensor (Active-LOW)
bool sensorArmed = true;

// ==================== HILFSFUNKTIONEN ====================
static inline int clampi(int v, int lo, int hi) { 
  return v < lo ? lo : (v > hi ? hi : v); 
}

static inline float clampf(float v, float lo, float hi) { 
  return v < lo ? lo : (v > hi ? hi : v); 
}

void setTextStyle(uint8_t size, uint16_t color) { 
  tft.setTextSize(size); 
  tft.setTextColor(color); 
}

const char* getPressureStatusText() {
  switch (pressureStatus) {
    case PRESSURE_OK: return "OK";
    case PRESSURE_TOO_LOW: return "ZU NIEDRIG";
    case PRESSURE_TOO_HIGH: return "ZU HOCH";
    default: return "UNBEKANNT";
  }
}

uint16_t getPressureStatusColor() {
  switch (pressureStatus) {
    case PRESSURE_OK: return GREEN;
    case PRESSURE_TOO_LOW: return RED;
    case PRESSURE_TOO_HIGH: return YELLOW;
    default: return LINE_COLOR;
  }
}

// ==================== DISPLAY-FUNKTIONEN ====================
void drawHeader(const char* title) {
  // Header-Bereich
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, ORANGE_DARK);
  tft.drawFastHLine(0, HEADER_H - 1, SCREEN_W, LINE_COLOR);
  
  // Projektname
  setTextStyle(2, TEXT_WHITE);
  tft.setCursor(10, 8);
  tft.print("W-GLÄTTER");
  
  // Modus-Titel
  setTextStyle(1, TEXT_LIGHT);
  tft.setCursor(10, 28);
  tft.print(title);
  
  // Relais-Status LED
  tft.fillRoundRect(SCREEN_W - 40, 10, 30, 24, 10, PANEL_BG);
  tft.drawRoundRect(SCREEN_W - 40, 10, 30, 24, 10, LINE_COLOR);
  tft.fillCircle(SCREEN_W - 25, 22, 6, relayActive ? GREEN : RED_DARK);
  tft.drawCircle(SCREEN_W - 25, 22, 6, TEXT_WHITE);
}

void drawWaveform() {
  const int waveX = 125;
  const int waveY = 10;
  const int waveW = 70;
  const int waveH = 24;
  
  tft.fillRect(waveX, waveY, waveW, waveH, ORANGE_DARK);
  
  if (relayActive) {
    for (int i = 0; i < WAVE_POINTS - 1; i++) {
      int x1 = waveX + (i * waveW) / WAVE_POINTS;
      int x2 = waveX + ((i + 1) * waveW) / WAVE_POINTS;
      int y1 = waveY + waveH/2 + waveValues[i]/4;
      int y2 = waveY + waveH/2 + waveValues[i+1]/4;
      tft.drawLine(x1, y1, x2, y2, WAVE_COLOR);
    }
  } else {
    tft.drawFastHLine(waveX, waveY + waveH/2, waveW, LINE_COLOR);
  }
}

void drawFooter(const char* text) {
  tft.fillRect(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, ORANGE_DARK);
  tft.drawFastHLine(0, SCREEN_H - FOOTER_H, SCREEN_W, LINE_COLOR);
  
  setTextStyle(1, TEXT_WHITE);
  tft.setCursor(8, SCREEN_H - FOOTER_H + 10);
  tft.print(text);
}

void drawMenuItem(uint8_t idx, bool selected) {
  const int x = 18, w = SCREEN_W - 36, h = 40;
  const int y = CONTENT_Y + 12 + idx * (h + 10);
  
  uint16_t fillColor = selected ? ORANGE_ACCENT : ORANGE_MAIN;
  uint16_t borderColor = selected ? ORANGE_LIGHT : LINE_COLOR;
  
  tft.fillRoundRect(x, y, w, h, 10, fillColor);
  tft.drawRoundRect(x, y, w, h, 10, borderColor);
  
  setTextStyle(2, selected ? BLACK : TEXT_WHITE);
  tft.setCursor(x + 12, y + 12);
  tft.print(menuItems[idx]);
}

// ==================== HAUPTMENÜ ====================
void drawMainMenu() {
  tft.fillScreen(BG_DARK);
  drawHeader("Hauptmenü");
  
  tft.fillRoundRect(10, CONTENT_Y, SCREEN_W - 20, CONTENT_H, 14, PANEL_BG);
  tft.drawRoundRect(10, CONTENT_Y, SCREEN_W - 20, CONTENT_H, 14, LINE_COLOR);
  
  for (uint8_t i = 0; i < menuCount; i++) {
    drawMenuItem(i, i == currentMenuSelection);
  }
  
  drawFooter("HOCH/RUNTER | EIN: Auswahl | 3x: Zurück");
}

// ==================== JOYSTICK ====================
void calibrateJoystick() {
  long sx = 0, sy = 0;
  const int N = 200;
  
  for (int i = 0; i < N; i++) {
    sx += analogRead(JOY_X);
    sy += analogRead(JOY_Y);
    delay(5);
  }
  
  centerX = sx / N;
  centerY = sy / N;
  fx = centerX;
  fy = centerY;
}

void updateJoyFilter() {
  int rx = analogRead(JOY_X);
  int ry = analogRead(JOY_Y);
  fx = fx * (1.0f - JOY_ALPHA) + rx * JOY_ALPHA;
  fy = fy * (1.0f - JOY_ALPHA) + ry * JOY_ALPHA;
}

int joyDX() { return (int)fx - centerX; }
int joyDY() { return (int)fy - centerY; }

void handleMainMenuNav() {
  int dx = joyDX();
  int dy = joyDY();

  if (abs(dx) < DEADZONE && abs(dy) < DEADZONE) {
    menuNavArmed = true;
  }

  if (!menuNavArmed) return;
  if (millis() - lastMenuMove < 180) return;

  uint8_t oldSel = currentMenuSelection;
  bool moved = false;

  if (dy < -MENU_THRESH) {
    currentMenuSelection = (currentMenuSelection == 0) ? (menuCount - 1) : (currentMenuSelection - 1);
    moved = true;
  } else if (dy > MENU_THRESH) {
    currentMenuSelection = (currentMenuSelection + 1) % menuCount;
    moved = true;
  }

  if (moved) {
    drawMenuItem(oldSel, false);
    drawMenuItem(currentMenuSelection, true);
    menuNavArmed = false;
    lastMenuMove = millis();
  }
}

// ==================== TASTEN-HANDLING ====================
bool buttonPressedEdge() {
  bool pressed = (digitalRead(JOY_SW) == LOW);
  if (!pressed) return false;

  unsigned long now = millis();
  if (now - lastButtonPress > 250) {
    if (now - lastClickTime < 600) {
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

bool tripleClickDetected() {
  return (buttonClickCount >= 3 && (millis() - lastClickTime) < 800);
}

bool isButtonHeld(uint16_t msHold) {
  static bool wasPressed = false;
  static unsigned long pressStart = 0;

  bool pressed = (digitalRead(JOY_SW) == LOW);

  if (pressed && !wasPressed) {
    wasPressed = true;
    pressStart = millis();
  }

  if (!pressed && wasPressed) {
    wasPressed = false;
    if (millis() - pressStart < msHold) {
      return false;
    }
  }

  if (pressed && wasPressed && (millis() - pressStart) > msHold) {
    while (digitalRead(JOY_SW) == LOW) { delay(10); }
    wasPressed = false;
    return true;
  }

  return false;
}

// ==================== RELAIS-STEUERUNG ====================
void activateRelay() {
  digitalWrite(RELAY_PIN, RELAY_ON);
  relayActive = true;
  DEBUG_PRINTLN("Relais AKTIV");
}

void deactivateRelay() {
  digitalWrite(RELAY_PIN, RELAY_OFF);
  relayActive = false;
  DEBUG_PRINTLN("Relais INAKTIV");
}

// ==================== DRUCKSENSOR ====================
float readPressureVoltage() {
  const int N = 64;
  long sum = 0;
  
  for (int i = 0; i < N; i++) {
    sum += analogRead(PRESS_PIN);
    delayMicroseconds(80);
  }
  
  rawADC = (int)(sum / N);
  return (rawADC / 4095.0f) * 3.3f;
}

void calibratePressureZero() {
  pressCalibrating = true;
  DEBUG_PRINTLN("Starte Nullpunkt-Kalibrierung...");

  // UI Feedback
  tft.fillRoundRect(14, CONTENT_Y + 105, SCREEN_W - 28, 80, 12, ORANGE_ACCENT);
  tft.drawRoundRect(14, CONTENT_Y + 105, SCREEN_W - 28, 80, 12, ORANGE_LIGHT);
  
  setTextStyle(2, BLACK);
  tft.setCursor(22, CONTENT_Y + 120);
  tft.print("Nullpunkt Kalibrierung...");
  
  setTextStyle(1, BLACK);
  tft.setCursor(22, CONTENT_Y + 145);
  tft.print("Sensor in offener Luft (0 bar)");

  // Mittelwert über 100 Messungen
  const int N = 100;
  float sumV = 0;
  
  for (int i = 0; i < N; i++) {
    sumV += readPressureVoltage();
    delay(15);
  }

  settings.zeroVoltage = sumV / N;
  pressFilt = -1.0f;
  pressureStatus = PRESSURE_TOO_LOW;

  preferences.putFloat("zeroVoltage", settings.zeroVoltage);

  pressCalibrating = false;
  DEBUG_PRINTF("Nullpunkt kalibriert: %.3fV\n", settings.zeroVoltage);
  
  delay(300);
}

void calibratePressureScale(float knownPressure) {
  pressCalibrating = true;
  DEBUG_PRINTLN("Starte Skalierungskalibrierung...");

  // UI Feedback
  tft.fillRoundRect(14, CONTENT_Y + 105, SCREEN_W - 28, 80, 12, ORANGE_ACCENT);
  tft.drawRoundRect(14, CONTENT_Y + 105, SCREEN_W - 28, 80, 12, ORANGE_LIGHT);
  
  setTextStyle(2, BLACK);
  tft.setCursor(22, CONTENT_Y + 120);
  tft.print("Skalierung Kalibrierung...");
  
  setTextStyle(1, BLACK);
  tft.setCursor(22, CONTENT_Y + 145);
  tft.printf("Bekannter Druck: %.2f bar", knownPressure);

  // Mittelwert über 100 Messungen
  const int N = 100;
  float sumV = 0;
  
  for (int i = 0; i < N; i++) {
    sumV += readPressureVoltage();
    delay(15);
  }

  float currentVoltage = sumV / N;
  
  // Berechne Skalierungsfaktor mit korrekter Formel
  float dV = (currentVoltage - settings.zeroVoltage) / settings.dividerRatio;
  
  if (fabs(dV) > 0.001f) {  // Vermeide Division durch Null
    settings.pressureScale = knownPressure / dV;
    settings.scaleCalibrated = true;
    settings.knownPressure = knownPressure;
    
    DEBUG_PRINTF("Skalierung kalibriert: %.3f bar/V bei %.3fV\n", 
                 settings.pressureScale, currentVoltage);
    
    // In NVS speichern
    preferences.putFloat("pressureScale", settings.pressureScale);
    preferences.putBool("scaleCalibrated", true);
    preferences.putFloat("knownPressure", knownPressure);
  } else {
    DEBUG_PRINTLN("FEHLER: Spannungsdifferenz zu klein!");
    
    setTextStyle(1, RED);
    tft.setCursor(22, CONTENT_Y + 160);
    tft.print("FEHLER: Spannung zu klein!");
  }

  pressCalibrating = false;
  delay(300);
}

void updatePressure() {
  if (pressCalibrating) return;

  pressVolt = readPressureVoltage();
  
  // Korrekte Formel mit Teiler:
  float dV = (pressVolt - settings.zeroVoltage) / settings.dividerRatio;
  
  // Druck berechnen
  pressUnits = dV * settings.pressureScale;
  if (pressUnits < 0.0f) pressUnits = 0.0f;

  // Tiefpass-Filter
  if (pressFilt < 0) {
    pressFilt = pressUnits;
  } else {
    pressFilt = pressFilt * (1.0f - PRESS_ALPHA) + pressUnits * PRESS_ALPHA;
  }

  // Status mit Hysterese bestimmen
  static PressureStatus lastStatus = PRESSURE_TOO_LOW;
  
  switch (lastStatus) {
    case PRESSURE_TOO_LOW:
      if (pressFilt >= (settings.okMin + PRESS_HYST)) {
        lastStatus = PRESSURE_OK;
      }
      break;
      
    case PRESSURE_OK:
      if (pressFilt <= (settings.okMin - PRESS_HYST)) {
        lastStatus = PRESSURE_TOO_LOW;
      } else if (pressFilt >= (settings.okMax + PRESS_HYST)) {
        lastStatus = PRESSURE_TOO_HIGH;
      }
      break;
      
    case PRESSURE_TOO_HIGH:
      if (pressFilt <= (settings.okMax - PRESS_HYST)) {
        lastStatus = PRESSURE_OK;
      }
      break;
  }
  
  pressureStatus = lastStatus;
}

// ==================== MODI ====================
void splashMode(const char* name) {
  tft.fillScreen(BG_DARK);
  drawHeader(name);
  
  tft.fillRoundRect(18, CONTENT_Y + 50, SCREEN_W - 36, 80, 14, ORANGE_ACCENT);
  tft.drawRoundRect(18, CONTENT_Y + 50, SCREEN_W - 36, 80, 14, ORANGE_LIGHT);
  
  setTextStyle(2, BLACK);
  tft.setCursor(28, CONTENT_Y + 82);
  tft.print(name);
  
  delay(300);
}

// Sensor-Modus
void drawSensorModeUI() {
  tft.fillScreen(BG_DARK);
  drawHeader("Sensor-Modus");
  
  tft.fillRoundRect(14, CONTENT_Y, SCREEN_W - 28, 170, 14, PANEL_BG);
  tft.drawRoundRect(14, CONTENT_Y, SCREEN_W - 28, 170, 14, LINE_COLOR);
  
  setTextStyle(2, TEXT_WHITE);
  tft.setCursor(26, CONTENT_Y + 40);
  tft.print("Warte auf Signal");
  
  tft.setCursor(26, CONTENT_Y + 70);
  tft.print("Trigger: Active-LOW");
  
  drawFooter("3x Klick: Zurück zum Menü");
}

void runSensorMode() {
  static bool activated = false;
  
  bool trig = (digitalRead(SENSOR_TRIG_PIN) == LOW);
  
  if (!trig) sensorArmed = true;
  
  if (sensorArmed && trig && !relayActive) {
    activateRelay();
    relayTimer = millis() + settings.sensorDuration;
    activated = true;
    sensorArmed = false;
    
    tft.fillRoundRect(18, CONTENT_Y + 120, SCREEN_W - 36, 44, 12, ORANGE_ACCENT);
    tft.drawRoundRect(18, CONTENT_Y + 120, SCREEN_W - 36, 44, 12, ORANGE_LIGHT);
    
    setTextStyle(2, BLACK);
    tft.setCursor(52, CONTENT_Y + 134);
    tft.print("AKTIVIERT!");
  }
  
  if (relayActive && activated && millis() >= relayTimer) {
    deactivateRelay();
    activated = false;
    drawSensorModeUI();
  }
}

// Joystick-Modus
void drawJoystickModeUI() {
  tft.fillScreen(BG_DARK);
  drawHeader("Joystick-Modus");
  
  tft.fillRoundRect(14, CONTENT_Y, SCREEN_W - 28, 170, 14, PANEL_BG);
  tft.drawRoundRect(14, CONTENT_Y, SCREEN_W - 28, 170, 14, LINE_COLOR);
  
  setTextStyle(2, TEXT_WHITE);
  tft.setCursor(22, CONTENT_Y + 30);
  tft.print("Joystick halten");
  
  setTextStyle(1, TEXT_LIGHT);
  tft.setCursor(22, CONTENT_Y + 60);
  tft.print("Relais bleibt EIN");
  
  tft.setCursor(22, CONTENT_Y + 80);
  tft.printf("Ein: >%d  Aus: <%d", 
             settings.joyOnThreshold, settings.joyOffThreshold);
  
  drawFooter("3x Klick: Zurück zum Menü");
}

void drawJoyStatus(bool on) {
  uint16_t color = on ? GREEN : ORANGE_DARK;
  
  tft.fillRoundRect(18, CONTENT_Y + 120, SCREEN_W - 36, 44, 12, color);
  tft.drawRoundRect(18, CONTENT_Y + 120, SCREEN_W - 36, 44, 12, ORANGE_LIGHT);
  
  setTextStyle(2, BLACK);
  tft.setCursor(80, CONTENT_Y + 134);
  tft.print(on ? "EIN" : "AUS");
}

void runJoystickMode() {
  int dx = joyDX();
  int dy = joyDY();
  
  long dist2 = (long)dx * dx + (long)dy * dy;
  long on2 = (long)settings.joyOnThreshold * (long)settings.joyOnThreshold;
  long off2 = (long)settings.joyOffThreshold * (long)settings.joyOffThreshold;
  
  if (!joyHoldLatched && dist2 > on2) {
    joyHoldLatched = true;
    activateRelay();
    drawJoyStatus(true);
  } else if (joyHoldLatched && dist2 < off2) {
    joyHoldLatched = false;
    deactivateRelay();
    drawJoyStatus(false);
  }
}

// Automatik-Modus
void drawAutoModeUI() {
  tft.fillScreen(BG_DARK);
  drawHeader("Automatik-Modus");
  
  tft.fillRoundRect(14, CONTENT_Y, SCREEN_W - 28, 170, 14, PANEL_BG);
  tft.drawRoundRect(14, CONTENT_Y, SCREEN_W - 28, 170, 14, LINE_COLOR);
  
  setTextStyle(2, TEXT_WHITE);
  tft.setCursor(44, CONTENT_Y + 30);
  tft.print("AUTO PULS");
  
  setTextStyle(1, TEXT_LIGHT);
  tft.setCursor(52, CONTENT_Y + 58);
  tft.printf("EIN %ds / AUS %ds", 
             settings.autoOnTime / 1000, settings.autoOffTime / 1000);
  
  drawFooter("3x Klick: Nur wenn AUS (Sicherheit)");
}

void runAutoMode() {
  if (autoFirst) {
    autoFirst = false;
    autoState = false;
    deactivateRelay();
    autoTimer = millis();
    drawAutoModeUI();
  }
  
  unsigned long now = millis();
  uint16_t interval = autoState ? settings.autoOnTime : settings.autoOffTime;
  
  if (now - autoTimer >= interval) {
    autoState = !autoState;
    autoTimer = now;
    
    if (autoState) {
      activateRelay();
    } else {
      deactivateRelay();
    }
    
    tft.fillRoundRect(18, CONTENT_Y + 95, SCREEN_W - 36, 50, 14, 
                     autoState ? ORANGE_ACCENT : ORANGE_DARK);
    tft.drawRoundRect(18, CONTENT_Y + 95, SCREEN_W - 36, 50, 14, ORANGE_LIGHT);
    
    setTextStyle(2, autoState ? BLACK : ORANGE_LIGHT);
    tft.setCursor(62, CONTENT_Y + 112);
    tft.print(autoState ? "STROM EIN" : "STROM AUS");
  }
}

// Druckanzeige-Modus
void drawPressureUI() {
  tft.fillScreen(BG_DARK);
  drawHeader("Druckanzeige");
  
  tft.fillRoundRect(14, CONTENT_Y, SCREEN_W - 28, 230, 14, PANEL_BG);
  tft.drawRoundRect(14, CONTENT_Y, SCREEN_W - 28, 230, 14, LINE_COLOR);
  
  drawFooter("Halten=Nullkalibrierung | 3x=Menü");
}

void drawPressureGauge() {
  tft.fillRoundRect(18, CONTENT_Y + 6, SCREEN_W - 36, 218, 12, PANEL_BG);
  
  // Titel
  setTextStyle(2, TEXT_WHITE);
  tft.setCursor(24, CONTENT_Y + 18);
  tft.print("Druck (bar)");
  
  // Aktueller Druck
  setTextStyle(2, ORANGE_LIGHT);
  tft.setCursor(24, CONTENT_Y + 45);
  tft.printf("%.3f", pressFilt);
  
  // Messwerte
  setTextStyle(1, TEXT_LIGHT);
  tft.setCursor(24, CONTENT_Y + 75);
  tft.printf("ADC: %d  V: %.3f", rawADC, pressVolt);
  
  tft.setCursor(24, CONTENT_Y + 92);
  tft.printf("NullV: %.3f", settings.zeroVoltage);
  
  tft.setCursor(24, CONTENT_Y + 109);
  tft.printf("Teiler: %.2f", settings.dividerRatio);
  
  tft.setCursor(24, CONTENT_Y + 126);
  tft.printf("Skala: %.3f bar/V", settings.pressureScale);
  
  if (settings.scaleCalibrated) {
    tft.setCursor(24, CONTENT_Y + 143);
    tft.printf("Kalibriert bei: %.2f bar", settings.knownPressure);
  }
  
  // OK-Bereich
  tft.setCursor(24, CONTENT_Y + (settings.scaleCalibrated ? 160 : 143));
  tft.printf("OK-Bereich: %.2f - %.2f bar", settings.okMin, settings.okMax);
  
  // Status-Anzeige
  uint16_t statusColor = getPressureStatusColor();
  const char* statusText = getPressureStatusText();
  
  tft.fillRoundRect(18, CONTENT_Y + 177, SCREEN_W - 36, 44, 12, statusColor);
  tft.drawRoundRect(18, CONTENT_Y + 177, SCREEN_W - 36, 44, 12, ORANGE_LIGHT);
  
  setTextStyle(2, BLACK);
  int textWidth = strlen(statusText) * 12;
  tft.setCursor((SCREEN_W - textWidth) / 2, CONTENT_Y + 191);
  tft.print(statusText);
  
  // Balkengrafik
  int barX = 24, barY = CONTENT_Y + 225, barW = 192, barH = 14;
  tft.drawRect(barX, barY, barW, barH, LINE_COLOR);
  tft.fillRect(barX + 1, barY + 1, barW - 2, barH - 2, BLACK);
  
  float visMax = max(settings.okMax * 1.5f, 1.0f);
  float n = clampf(pressFilt, 0.0f, visMax);
  int fillWidth = (int)((n / visMax) * (barW - 2));
  fillWidth = clampi(fillWidth, 0, barW - 2);
  
  // OK-Bereich markieren
  int okStart = (int)((settings.okMin / visMax) * (barW - 2));
  int okEnd = (int)((settings.okMax / visMax) * (barW - 2));
  okStart = clampi(okStart, 0, barW - 2);
  okEnd = clampi(okEnd, 0, barW - 2);
  
  tft.fillRect(barX + 1 + okStart, barY + 1, okEnd - okStart, barH - 2, GREEN_DARK);
  tft.fillRect(barX + 1, barY + 1, fillWidth, barH - 2, statusColor);
}

void runPressureMode() {
  static unsigned long lastDraw = 0;
  
  updatePressure();
  
  if (isButtonHeld(1500)) {
    calibratePressureZero();
    drawPressureGauge();
    lastDraw = millis();
  }
  
  if (millis() - lastDraw > 250) {
    drawPressureGauge();
    lastDraw = millis();
  }
}

// ==================== WEB UI ====================
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Kabelglätter Steuerung</title>
    <style>
        :root {
            --primary-dark: #1a1408;
            --primary-medium: #2a1f0c;
            --orange-dark: #e65c00;
            --orange-medium: #ff7700;
            --orange-light: #ff8800;
            --orange-accent: #ffaa44;
            --text-light: #ffffff;
            --text-medium: #cccccc;
            --success: #4caf50;
            --warning: #ff9800;
            --error: #f44336;
            --panel-bg: rgba(255, 140, 0, 0.1);
            --border: rgba(255, 140, 0, 0.3);
        }
        
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;
        }
        
        body {
            background: linear-gradient(135deg, var(--primary-dark) 0%, var(--primary-medium) 100%);
            color: var(--text-light);
            min-height: 100vh;
            padding: 20px;
            line-height: 1.6;
        }
        
        .container {
            max-width: 1200px;
            margin: 0 auto;
        }
        
        header {
            background: linear-gradient(90deg, var(--orange-dark) 0%, var(--orange-medium) 100%);
            padding: 24px 30px;
            border-radius: 16px;
            margin-bottom: 30px;
            box-shadow: 0 8px 16px rgba(0, 0, 0, 0.3);
            border: 1px solid var(--border);
        }
        
        h1 {
            font-size: 2.4em;
            font-weight: 700;
            margin-bottom: 8px;
        }
        
        .subtitle {
            font-size: 1em;
            opacity: 0.9;
            font-weight: 300;
        }
        
        .grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(350px, 1fr));
            gap: 25px;
            margin-bottom: 30px;
        }
        
        .card {
            background: var(--panel-bg);
            border: 1px solid var(--border);
            border-radius: 16px;
            padding: 25px;
            backdrop-filter: blur(12px);
        }
        
        .card h2 {
            color: var(--orange-accent);
            margin-bottom: 20px;
            font-size: 1.6em;
            font-weight: 600;
            padding-bottom: 10px;
            border-bottom: 2px solid var(--border);
        }
        
        .status-grid {
            display: grid;
            grid-template-columns: repeat(2, 1fr);
            gap: 12px;
        }
        
        .status-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 12px 15px;
            background: rgba(0, 0, 0, 0.3);
            border-radius: 10px;
            border-left: 4px solid var(--orange-medium);
        }
        
        .status-label {
            font-weight: 600;
            color: var(--orange-accent);
            font-size: 0.95em;
        }
        
        .status-value {
            font-family: 'Consolas', 'Monaco', monospace;
            font-weight: 500;
            font-size: 1.1em;
        }
        
        .status-on { color: var(--success); }
        .status-off { color: var(--error); }
        .status-ok { color: var(--success); }
        .status-low { color: var(--error); }
        .status-high { color: var(--warning); }
        
        .oscilloscope-container {
            margin-top: 25px;
            background: rgba(0, 0, 0, 0.5);
            border-radius: 10px;
            padding: 20px;
            border: 1px solid rgba(255, 255, 255, 0.1);
        }
        
        canvas {
            width: 100%;
            height: 120px;
            background: #000;
            border-radius: 8px;
            display: block;
            border: 1px solid #333;
        }
        
        .setting-group {
            margin-bottom: 22px;
        }
        
        .setting-label {
            display: block;
            margin-bottom: 8px;
            color: var(--orange-accent);
            font-weight: 500;
            font-size: 0.95em;
        }
        
        input[type="number"] {
            width: 120px;
            padding: 10px 12px;
            border-radius: 8px;
            border: 1px solid var(--orange-medium);
            background: rgba(0, 0, 0, 0.5);
            color: var(--text-light);
            font-size: 1em;
            margin-right: 10px;
        }
        
        button {
            background: linear-gradient(90deg, var(--orange-dark) 0%, var(--orange-medium) 100%);
            color: var(--text-light);
            border: none;
            padding: 14px 24px;
            border-radius: 10px;
            cursor: pointer;
            font-weight: 600;
            font-size: 1em;
            margin: 8px 8px 8px 0;
            transition: all 0.2s;
        }
        
        button:hover {
            transform: translateY(-2px);
            box-shadow: 0 6px 12px rgba(0, 0, 0, 0.3);
        }
        
        button.success {
            background: linear-gradient(90deg, #2e7d32 0%, var(--success) 100%);
        }
        
        button.danger {
            background: linear-gradient(90deg, #c62828 0%, var(--error) 100%);
        }
        
        .notification {
            position: fixed;
            top: 25px;
            right: 25px;
            background: var(--success);
            color: var(--text-light);
            padding: 18px 24px;
            border-radius: 12px;
            box-shadow: 0 8px 20px rgba(0, 0, 0, 0.4);
            z-index: 1000;
            animation: slideIn 0.3s ease-out;
            max-width: 350px;
        }
        
        .notification.error {
            background: var(--error);
        }
        
        .notification.warning {
            background: var(--warning);
        }
        
        @keyframes slideIn {
            from { transform: translateX(100%); opacity: 0; }
            to { transform: translateX(0); opacity: 1; }
        }
        
        .hidden { display: none; }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>W-GLÄTTER STEUERUNG</h1>
            <div class="subtitle">
                ESP32 Web Interface | IP: 192.168.4.1 | Verbundene Clients: <span id="clientCount">0</span>
            </div>
        </header>
        
        <div class="grid">
            <div class="card">
                <h2>Systemstatus</h2>
                <div class="status-grid">
                    <div class="status-item">
                        <span class="status-label">Aktiver Modus:</span>
                        <span class="status-value" id="mode">-</span>
                    </div>
                    <div class="status-item">
                        <span class="status-label">Relais Status:</span>
                        <span class="status-value" id="relay">-</span>
                    </div>
                    <div class="status-item">
                        <span class="status-label">Druck (bar):</span>
                        <span class="status-value" id="pressure">-</span>
                    </div>
                    <div class="status-item">
                        <span class="status-label">Druck Status:</span>
                        <span class="status-value" id="pressureStatus">-</span>
                    </div>
                    <div class="status-item">
                        <span class="status-label">ADC / Spannung:</span>
                        <span class="status-value" id="adcVolt">-</span>
                    </div>
                    <div class="status-item">
                        <span class="status-label">Null-Spannung:</span>
                        <span class="status-value" id="zeroV">-</span>
                    </div>
                    <div class="status-item">
                        <span class="status-label">Skalierung:</span>
                        <span class="status-value" id="scale">-</span>
                    </div>
                    <div class="status-item">
                        <span class="status-label">Teiler-Verhältnis:</span>
                        <span class="status-value" id="dividerRatio">-</span>
                    </div>
                    <div class="status-item">
                        <span class="status-label">OK-Bereich:</span>
                        <span class="status-value" id="okRange">-</span>
                    </div>
                    <div class="status-item">
                        <span class="status-label">Uptime:</span>
                        <span class="status-value" id="uptime">-</span>
                    </div>
                </div>
                
                <div class="oscilloscope-container">
                    <h3>Motoraktivität</h3>
                    <canvas id="oscilloscope"></canvas>
                </div>
            </div>
            
            <div class="card">
                <h2>Einstellungen</h2>
                
                <div class="setting-group">
                    <label class="setting-label">Druckskala (bar/V):</label>
                    <input type="number" id="scaleInput" min="0.1" max="10" step="0.001">
                </div>
                
                <div class="setting-group">
                    <label class="setting-label">Spannungsteiler Verhältnis (1.0 = kein Teiler):</label>
                    <input type="number" id="dividerInput" min="0.1" max="1.0" step="0.01">
                </div>
                
                <div class="setting-group">
                    <label class="setting-label">OK-Bereich Minimum (bar):</label>
                    <input type="number" id="okMinInput" min="0.0" max="5.0" step="0.01">
                </div>
                
                <div class="setting-group">
                    <label class="setting-label">OK-Bereich Maximum (bar):</label>
                    <input type="number" id="okMaxInput" min="0.0" max="5.0" step="0.01">
                </div>
                
                <div class="setting-group">
                    <label class="setting-label">Joystick EIN-Schwelle:</label>
                    <input type="number" id="joyOnInput" min="100" max="2000">
                </div>
                
                <div class="setting-group">
                    <label class="setting-label">Joystick AUS-Schwelle:</label>
                    <input type="number" id="joyOffInput" min="100" max="2000">
                </div>
                
                <div class="setting-group">
                    <label class="setting-label">Auto EIN Zeit (ms):</label>
                    <input type="number" id="autoOnInput" min="100" max="30000">
                </div>
                
                <div class="setting-group">
                    <label class="setting-label">Auto AUS Zeit (ms):</label>
                    <input type="number" id="autoOffInput" min="100" max="30000">
                </div>
                
                <div class="setting-group">
                    <label class="setting-label">Sensor Dauer (ms):</label>
                    <input type="number" id="sensorDurInput" min="100" max="60000">
                </div>
                
                <button onclick="saveSettings()">Einstellungen speichern</button>
                <button class="danger" onclick="toggleRelay()">Relais umschalten</button>
                
                <div style="margin-top: 30px; padding-top: 20px; border-top: 2px solid var(--border);">
                    <h3>Kalibrierung</h3>
                    
                    <div class="setting-group">
                        <label class="setting-label">Bekannter Druck für Skalierung (bar):</label>
                        <input type="number" id="knownPressureInput" min="0.1" max="5.0" step="0.01" value="0.45">
                    </div>
                    
                    <button class="success" onclick="calibrateZero()">Nullpunkt kalibrieren</button>
                    <button class="success" onclick="calibrateScale()">Skalierung kalibrieren</button>
                    
                    <p style="color: var(--text-medium); font-size: 0.9em; margin-top: 15px;">
                        Anleitung: 1. Nullpunkt bei 0 bar (offene Luft) kalibrieren.<br>
                        2. Bekannten Druck anwenden und Skalierung kalibrieren.
                    </p>
                </div>
            </div>
        </div>
    </div>
    
    <div id="notification" class="notification hidden">
        <span id="notificationText"></span>
    </div>
    
    <script>
        let waveData = new Array(100).fill(0);
        let waveIndex = 0;
        let inputFocus = false;
        const canvas = document.getElementById('oscilloscope');
        const ctx = canvas.getContext('2d');
        
        function initCanvas() {
            canvas.width = canvas.clientWidth;
            canvas.height = canvas.clientHeight;
            drawOscilloscope(false);
        }
        
        async function updateStatus() {
            try {
                const response = await fetch('/api/status');
                const data = await response.json();
                
                // Statuswerte aktualisieren
                document.getElementById('mode').textContent = data.mode;
                
                const relayEl = document.getElementById('relay');
                relayEl.textContent = data.relay ? 'EIN' : 'AUS';
                relayEl.className = 'status-value ' + (data.relay ? 'status-on' : 'status-off');
                
                document.getElementById('pressure').textContent = data.pressureFilt.toFixed(3);
                
                const pressureStatusEl = document.getElementById('pressureStatus');
                let statusText = '';
                let statusClass = '';
                switch(data.pressureStatus) {
                    case 0: statusText = 'OK'; statusClass = 'status-ok'; break;
                    case 1: statusText = 'ZU NIEDRIG'; statusClass = 'status-low'; break;
                    case 2: statusText = 'ZU HOCH'; statusClass = 'status-high'; break;
                }
                pressureStatusEl.textContent = statusText;
                pressureStatusEl.className = 'status-value ' + statusClass;
                
                document.getElementById('adcVolt').textContent = data.adc + ' / ' + data.volt.toFixed(3) + 'V';
                document.getElementById('zeroV').textContent = data.zeroV.toFixed(3) + 'V';
                document.getElementById('scale').textContent = data.scale.toFixed(3);
                document.getElementById('dividerRatio').textContent = data.dividerRatio.toFixed(2);
                document.getElementById('okRange').textContent = data.okMin.toFixed(2) + ' - ' + data.okMax.toFixed(2) + ' bar';
                
                // Uptime berechnen
                const uptimeMs = data.uptime || 0;
                const hours = Math.floor(uptimeMs / 3600000);
                const minutes = Math.floor((uptimeMs % 3600000) / 60000);
                const seconds = Math.floor((uptimeMs % 60000) / 1000);
                document.getElementById('uptime').textContent = 
                    `${hours.toString().padStart(2, '0')}:${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`;
                
                // Client-Anzahl
                document.getElementById('clientCount').textContent = data.clientCount || '0';
                
                // Waveform-Daten
                waveData[waveIndex] = data.pressureFilt * 10;
                waveIndex = (waveIndex + 1) % waveData.length;
                
                // Oszilloskop zeichnen
                drawOscilloscope(data.relay);
                
                // Einstellungen nur aktualisieren wenn kein Input-Focus
                if (!inputFocus) {
                    document.getElementById('scaleInput').value = data.scale;
                    document.getElementById('dividerInput').value = data.dividerRatio;
                    document.getElementById('okMinInput').value = data.okMin;
                    document.getElementById('okMaxInput').value = data.okMax;
                    document.getElementById('joyOnInput').value = data.joyOn;
                    document.getElementById('joyOffInput').value = data.joyOff;
                    document.getElementById('autoOnInput').value = data.autoOn;
                    document.getElementById('autoOffInput').value = data.autoOff;
                    document.getElementById('sensorDurInput').value = data.sensorDuration;
                }
                
            } catch (error) {
                console.error('Fehler beim Status-Abruf:', error);
            }
        }
        
        function drawOscilloscope(relayActive) {
            const width = canvas.width;
            const height = canvas.height;
            
            // Hintergrund
            ctx.fillStyle = '#000';
            ctx.fillRect(0, 0, width, height);
            
            // Gitter
            ctx.strokeStyle = '#222';
            ctx.lineWidth = 1;
            
            for (let x = 0; x < width; x += width / 20) {
                ctx.beginPath();
                ctx.moveTo(x, 0);
                ctx.lineTo(x, height);
                ctx.stroke();
            }
            
            for (let y = 0; y < height; y += height / 8) {
                ctx.beginPath();
                ctx.moveTo(0, y);
                ctx.lineTo(width, y);
                ctx.stroke();
            }
            
            if (relayActive) {
                // Wellenform
                ctx.strokeStyle = '#0ff';
                ctx.lineWidth = 2;
                ctx.beginPath();
                
                for (let i = 0; i < waveData.length; i++) {
                    const x = (i * width) / waveData.length;
                    const y = height / 2 - waveData[(i + waveIndex) % waveData.length];
                    
                    if (i === 0) {
                        ctx.moveTo(x, y);
                    } else {
                        ctx.lineTo(x, y);
                    }
                }
                ctx.stroke();
            } else {
                // Horizontale Linie
                ctx.strokeStyle = '#444';
                ctx.lineWidth = 2;
                ctx.beginPath();
                ctx.moveTo(0, height / 2);
                ctx.lineTo(width, height / 2);
                ctx.stroke();
            }
        }
        
        async function saveSettings() {
            const settings = {
                scale: parseFloat(document.getElementById('scaleInput').value),
                dividerRatio: parseFloat(document.getElementById('dividerInput').value),
                okMin: parseFloat(document.getElementById('okMinInput').value),
                okMax: parseFloat(document.getElementById('okMaxInput').value),
                joyOn: parseInt(document.getElementById('joyOnInput').value),
                joyOff: parseInt(document.getElementById('joyOffInput').value),
                autoOn: parseInt(document.getElementById('autoOnInput').value),
                autoOff: parseInt(document.getElementById('autoOffInput').value),
                sensorDuration: parseInt(document.getElementById('sensorDurInput').value)
            };
            
            try {
                const response = await fetch('/api/settings', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(settings)
                });
                
                const data = await response.json();
                showNotification('Einstellungen gespeichert!');
            } catch (error) {
                showNotification('Fehler beim Speichern!', 'error');
            }
        }
        
        async function calibrateZero() {
            if (!confirm('Nullpunkt kalibrieren? Sensor muss in offener Luft sein (0 bar).')) {
                return;
            }
            
            try {
                const response = await fetch('/api/calibrateZero', { method: 'POST' });
                const data = await response.json();
                showNotification('Nullpunkt kalibriert!');
            } catch (error) {
                showNotification('Fehler bei der Kalibrierung!', 'error');
            }
        }
        
        async function calibrateScale() {
            const knownPressure = parseFloat(document.getElementById('knownPressureInput').value);
            
            if (!knownPressure || knownPressure <= 0) {
                showNotification('Bitte einen gültigen Druck eingeben!', 'error');
                return;
            }
            
            if (!confirm(`Skalierung mit ${knownPressure} bar kalibrieren?`)) {
                return;
            }
            
            try {
                const response = await fetch('/api/calibrateScale', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ knownPressure: knownPressure })
                });
                
                const data = await response.json();
                if (data.status === 'calibrated') {
                    showNotification(`Skalierung mit ${knownPressure} bar kalibriert!`);
                } else {
                    showNotification('Fehler: ' + data.error, 'error');
                }
            } catch (error) {
                showNotification('Fehler bei der Skalierungskalibrierung!', 'error');
            }
        }
        
        async function toggleRelay() {
            try {
                const response = await fetch('/api/toggleRelay', { method: 'POST' });
                const data = await response.json();
                showNotification('Relais ' + (data.relay ? 'EIN' : 'AUS'));
            } catch (error) {
                showNotification('Fehler beim Umschalten!', 'error');
            }
        }
        
        function showNotification(message, type = 'success') {
            const notification = document.getElementById('notification');
            const text = document.getElementById('notificationText');
            
            text.textContent = message;
            notification.className = 'notification ' + (type === 'error' ? 'error' : '');
            notification.classList.remove('hidden');
            
            setTimeout(() => {
                notification.classList.add('hidden');
            }, 3000);
        }
        
        // Input-Focus Tracking
        const inputs = document.querySelectorAll('input[type="number"]');
        inputs.forEach(input => {
            input.addEventListener('focus', () => { inputFocus = true; });
            input.addEventListener('blur', () => { inputFocus = false; });
        });
        
        window.addEventListener('resize', initCanvas);
        initCanvas();
        
        setInterval(updateStatus, 250);
        updateStatus();
    </script>
</body>
</html>
)rawliteral";

// ==================== JSON PARSING ====================
String getJsonValue(String json, String key) {
  int start = json.indexOf("\"" + key + "\":");
  if (start == -1) return "";
  start += key.length() + 3;
  int end = json.indexOf(",", start);
  if (end == -1) end = json.indexOf("}", start);
  if (end == -1) return "";
  return json.substring(start, end);
}

// ==================== WEB SERVER HANDLER ====================
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
  json += "\"pressureStatus\":" + String(pressureStatus) + ",";
  json += "\"adc\":" + String(rawADC) + ",";
  json += "\"volt\":" + String(pressVolt, 3) + ",";
  json += "\"zeroV\":" + String(settings.zeroVoltage, 3) + ",";
  json += "\"scale\":" + String(settings.pressureScale, 3) + ",";
  json += "\"dividerRatio\":" + String(settings.dividerRatio, 2) + ",";
  json += "\"okMin\":" + String(settings.okMin, 2) + ",";
  json += "\"okMax\":" + String(settings.okMax, 2) + ",";
  json += "\"joyOn\":" + String(settings.joyOnThreshold) + ",";
  json += "\"joyOff\":" + String(settings.joyOffThreshold) + ",";
  json += "\"autoOn\":" + String(settings.autoOnTime) + ",";
  json += "\"autoOff\":" + String(settings.autoOffTime) + ",";
  json += "\"sensorDuration\":" + String(settings.sensorDuration) + ",";
  json += "\"uptime\":" + String(millis()) + ",";
  json += "\"clientCount\":" + String(WiFi.softAPgetStationNum());
  json += "}";

  server.send(200, "application/json", json);
}

void handleSettings() {
  if (server.hasArg("plain")) {
    String json = server.arg("plain");

    String scale = getJsonValue(json, "scale");
    if (scale.length() > 0) {
      settings.pressureScale = scale.toFloat();
      preferences.putFloat("pressureScale", settings.pressureScale);
    }
    
    String divider = getJsonValue(json, "dividerRatio");
    if (divider.length() > 0) {
      settings.dividerRatio = divider.toFloat();
      preferences.putFloat("dividerRatio", settings.dividerRatio);
    }
    
    String okMin = getJsonValue(json, "okMin");
    if (okMin.length() > 0) {
      settings.okMin = okMin.toFloat();
      preferences.putFloat("okMin", settings.okMin);
    }
    
    String okMax = getJsonValue(json, "okMax");
    if (okMax.length() > 0) {
      settings.okMax = okMax.toFloat();
      preferences.putFloat("okMax", settings.okMax);
    }
    
    String joyOn = getJsonValue(json, "joyOn");
    if (joyOn.length() > 0) {
      settings.joyOnThreshold = joyOn.toInt();
      preferences.putInt("joyOnThreshold", settings.joyOnThreshold);
    }
    
    String joyOff = getJsonValue(json, "joyOff");
    if (joyOff.length() > 0) {
      settings.joyOffThreshold = joyOff.toInt();
      preferences.putInt("joyOffThreshold", settings.joyOffThreshold);
    }
    
    String autoOn = getJsonValue(json, "autoOn");
    if (autoOn.length() > 0) {
      settings.autoOnTime = autoOn.toInt();
      preferences.putInt("autoOnTime", settings.autoOnTime);
    }
    
    String autoOff = getJsonValue(json, "autoOff");
    if (autoOff.length() > 0) {
      settings.autoOffTime = autoOff.toInt();
      preferences.putInt("autoOffTime", settings.autoOffTime);
    }
    
    String sensorDur = getJsonValue(json, "sensorDuration");
    if (sensorDur.length() > 0) {
      settings.sensorDuration = sensorDur.toInt();
      preferences.putInt("sensorDuration", settings.sensorDuration);
    }

    server.send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
  }
}

void handleCalibrateZero() {
  calibratePressureZero();
  server.send(200, "application/json", "{\"status\":\"calibrated\"}");
}

void handleCalibrateScale() {
  if (server.hasArg("plain")) {
    String json = server.arg("plain");
    float knownPressure = getJsonValue(json, "knownPressure").toFloat();
    
    if (knownPressure > 0.0f && knownPressure <= 5.0f) {
      calibratePressureScale(knownPressure);
      server.send(200, "application/json", "{\"status\":\"calibrated\"}");
    } else {
      server.send(400, "application/json", "{\"error\":\"Ungültiger Druckwert\"}");
    }
  } else {
    server.send(400, "application/json", "{\"error\":\"Ungültige Anfrage\"}");
  }
}

void handleToggleRelay() {
  if (relayActive) {
    deactivateRelay();
  } else {
    activateRelay();
  }
  
  server.send(200, "application/json", "{\"relay\":" + String(relayActive ? "true" : "false") + "}");
}

// ==================== NVS INITIALISIERUNG ====================
void loadSettings() {
  DEBUG_PRINTLN("Lade Einstellungen aus NVS...");
  
  settings.pressureScale = preferences.getFloat("pressureScale", 1.0f);
  settings.dividerRatio = preferences.getFloat("dividerRatio", 1.0f);
  settings.zeroVoltage = preferences.getFloat("zeroVoltage", 0.0f);
  settings.okMin = preferences.getFloat("okMin", 0.40f);
  settings.okMax = preferences.getFloat("okMax", 0.50f);
  settings.pressureThreshold = preferences.getFloat("pressureThreshold", 0.35f);
  
  settings.joyOnThreshold = preferences.getInt("joyOnThreshold", 650);
  settings.joyOffThreshold = preferences.getInt("joyOffThreshold", 420);
  settings.autoOnTime = preferences.getInt("autoOnTime", 5000);
  settings.autoOffTime = preferences.getInt("autoOffTime", 2000);
  settings.sensorDuration = preferences.getInt("sensorDuration", 10000);
  
  settings.scaleCalibrated = preferences.getBool("scaleCalibrated", false);
  settings.knownPressure = preferences.getFloat("knownPressure", 0.0f);
  
  DEBUG_PRINTF("Geladen: Scale=%.3f, Divider=%.2f, ZeroV=%.3f\n", 
               settings.pressureScale, settings.dividerRatio, settings.zeroVoltage);
  DEBUG_PRINTF("OK-Bereich: %.2f - %.2f bar\n", settings.okMin, settings.okMax);
}

// ==================== OSZILLOSKOP ANIMATION ====================
void updateWaveform() {
  if (millis() - lastWaveUpdate >= WAVE_UPDATE_MS) {
    if (relayActive) {
      waveValues[waveIndex] = (int)(sin(waveIndex * 0.3) * 12 + random(-3, 3));
    } else {
      waveValues[waveIndex] = 0;
    }
    waveIndex = (waveIndex + 1) % WAVE_POINTS;
    lastWaveUpdate = millis();
    
    drawWaveform();
  }
}

// ==================== SETUP ====================
void setup() {
  #if DEBUG_SERIAL
  Serial.begin(115200);
  delay(100);
  DEBUG_PRINTLN("\n=== W-GLÄTTER STARTET ===");
  #endif
  
  // GPIO initialisieren
  pinMode(JOY_SW, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(SENSOR_TRIG_PIN, INPUT_PULLUP);
  digitalWrite(RELAY_PIN, RELAY_OFF);
  
  // ADC konfigurieren
  analogReadResolution(12);
  analogSetPinAttenuation(JOY_X, ADC_11db);
  analogSetPinAttenuation(JOY_Y, ADC_11db);
  analogSetPinAttenuation(PRESS_PIN, ADC_11db);
  
  // Display initialisieren
  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(2);
  
  // NVS initialisieren
  preferences.begin(NVS_NAMESPACE, false);
  loadSettings();
  
  // Startbildschirm
  tft.fillScreen(BLACK);
  setTextStyle(2, TEXT_WHITE);
  tft.setCursor(40, 60);
  tft.print("W-GLÄTTER");
  
  setTextStyle(1, TEXT_LIGHT);
  tft.setCursor(60, 90);
  tft.print("Starte...");
  
  delay(500);
  
  // Joystick kalibrieren
  calibrateJoystick();
  DEBUG_PRINTLN("Joystick kalibriert");
  
  // Druck-Nullpunkt kalibrieren (falls nötig)
  if (settings.zeroVoltage < 0.1f) {
    DEBUG_PRINTLN("Kalibriere Druck-Nullpunkt...");
    calibratePressureZero();
  }
  
  // WiFi AP starten
  WiFi.softAP(apSSID, apPassword);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  
  // Web Server konfigurieren
  server.on("/", handleRoot);
  server.on("/api/status", handleStatus);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/calibrateZero", HTTP_POST, handleCalibrateZero);
  server.on("/api/calibrateScale", HTTP_POST, handleCalibrateScale);
  server.on("/api/toggleRelay", HTTP_POST, handleToggleRelay);
  server.begin();
  
  DEBUG_PRINTLN("Web Server gestartet");
  DEBUG_PRINTF("SSID: %s\n", apSSID);
  DEBUG_PRINT("IP: "); DEBUG_PRINTLN(WiFi.softAPIP());
  
  // Hauptmenü anzeigen
  drawMainMenu();
}

// ==================== HAUPTSCHLEIFE ====================
void loop() {
  server.handleClient();
  
  updateJoyFilter();
  updateWaveform();
  
  if (buttonPressedEdge()) {
    if (activeMode == -1) {
      if (buttonClickCount == 1) {
        activeMode = currentMenuSelection;
        splashMode(menuItems[currentMenuSelection]);
        
        switch (activeMode) {
          case 0:
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
      if (tripleClickDetected()) {
        if (activeMode != 2 || !relayActive) {
          deactivateRelay();
          activeMode = -1;
          buttonClickCount = 0;
          drawMainMenu();
          return;
        }
      }
    }
  }
  
  if (activeMode == -1) {
    handleMainMenuNav();
  } else {
    switch (activeMode) {
      case 0: runSensorMode(); break;
      case 1: runJoystickMode(); break;
      case 2: runAutoMode(); break;
      case 3: runPressureMode(); break;
    }
  }
  
  delay(10);
}
