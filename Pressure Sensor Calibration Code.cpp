#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <math.h>

// ==================== KONFIGURATION ====================
#define DEBUG_SERIAL 1  // Setze auf 0 für Produktion

#if DEBUG_SERIAL
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTF(...)
#endif

// ==================== SPI KONFIGURATION ====================
#define SPI_FREQ 40000000  // 40 MHz (stabil für ILI9341)

// ==================== PIN-DEFINITIONEN ====================
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

// ==================== BILDSCHIRM ====================
#define SCREEN_W  240
#define SCREEN_H  320
#define HEADER_H  44
#define FOOTER_H  26
#define CONTENT_Y (HEADER_H + 6)
#define CONTENT_H (SCREEN_H - HEADER_H - FOOTER_H - 12)

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

// ==================== FARBPALETTE (Premium Orange) ====================
static inline uint16_t swapRGB(uint16_t c) {
  return (c & 0x07E0) | ((c & 0xF800) >> 11) | ((c & 0x001F) << 11);
}

// Basis Farben (5-6-5 RGB)
#define ORANGE_DARK_B    0x02BF    // Dunkles Orange
#define ORANGE_MAIN_B    0x02DF    // Hauptorange
#define ORANGE_ACCENT_B  0x04FF    // Akzentorange
#define ORANGE_LIGHT_B   0x02FF    // Helles Orange
#define PANEL_BG_B       0x2104    // Dunkelgrau
#define BG_DARK_B        0x0004    // Sehr dunkles Grau

// Konvertiert zu ILI9341 Format (16-bit)
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
#define BLUE          0x001F
#define CYAN          0x07FF
#define LINE_COLOR    0x39E7
#define WAVE_COLOR    0x07FF

// ==================== EINSTELLUNGEN (NVS) ====================
Preferences preferences;
#define NVS_NAMESPACE "W_Glaetter"

struct Settings {
  // Druck-Kalibrierung
  float pressureThreshold;    // in bar
  float zeroVoltage;          // Spannung bei 0 bar (nach Teiler)
  float spanVoltage;          // Spannung bei bekanntem Druck
  float knownPressure;        // Bekannter Druck für Kalibrierung (bar)
  float pressureScale;        // bar/V (automatisch berechnet)
  bool spanCalibrated;        // Zwei-Punkt-Kalibrierung durchgeführt
  bool pressureInvert;        // Spannung sinkt bei Druck (true = invertiert)
  float dividerRatio;         // Spannungsteiler-Verhältnis (1.0 = kein Teiler)
  
  // Joystick
  int joyOnThreshold;         // Ein-Schwelle
  int joyOffThreshold;        // Aus-Schwelle (Hysterese)
  
  // Automatik-Modus
  int autoOnTime;             // ms
  int autoOffTime;            // ms
  
  // Sensor-Modus
  int sensorDuration;         // ms
};

Settings settings = {
  .pressureThreshold = 0.35f,
  .zeroVoltage = 0.0f,
  .spanVoltage = 0.0f,
  .knownPressure = 0.0f,
  .pressureScale = 1.0f,
  .spanCalibrated = false,
  .pressureInvert = false,    // Standard: Spannung steigt mit Druck
  .dividerRatio = 1.0f,
  .joyOnThreshold = 650,
  .joyOffThreshold = 420,
  .autoOnTime = 5000,
  .autoOffTime = 2000,
  .sensorDuration = 10000
};

// ==================== GLOBALE VARIABLEN ====================
uint8_t currentMenuSelection = 0;
int8_t activeMode = -1;  // -1 = Menü, 0-3 = Modi
const char* menuItems[] = {" Sensor-Modus", " Joystick-Modus", " Automatik", " Druckanzeige"};
const uint8_t menuCount = sizeof(menuItems) / sizeof(menuItems[0]);

// Relais
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
float pressVolt = 0.0f;
float pressUnits = 0.0f;
float pressFilt = -1.0f;
bool pressOK = false;
int rawADC = 0;
bool pressCalibrating = false;
const float PRESS_ALPHA = 0.15f;
const float PRESS_HYST = 0.05f;

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

// Display Recovery
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_RECOVERY_INTERVAL = 30000;  // 30 Sekunden

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

// ==================== DISPLAY-HANDLING ====================
void beginSPITransaction() {
  SPI.beginTransaction(SPISettings(SPI_FREQ, MSBFIRST, SPI_MODE0));
}

void endSPITransaction() {
  SPI.endTransaction();
}

void safeDrawPixel(int16_t x, int16_t y, uint16_t color) {
  beginSPITransaction();
  tft.drawPixel(x, y, color);
  endSPITransaction();
}

void safeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  beginSPITransaction();
  tft.fillRect(x, y, w, h, color);
  endSPITransaction();
}

void safeDrawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  beginSPITransaction();
  tft.drawRect(x, y, w, h, color);
  endSPITransaction();
}

void safeFillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
  beginSPITransaction();
  tft.fillRoundRect(x, y, w, h, r, color);
  endSPITransaction();
}

void safeDrawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
  beginSPITransaction();
  tft.drawRoundRect(x, y, w, h, r, color);
  endSPITransaction();
}

void safeSetCursor(int16_t x, int16_t y) {
  beginSPITransaction();
  tft.setCursor(x, y);
  endSPITransaction();
}

void safePrint(const char* text) {
  beginSPITransaction();
  tft.print(text);
  endSPITransaction();
}

void safePrintf(const char* format, ...) {
  char buffer[64];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  
  beginSPITransaction();
  tft.print(buffer);
  endSPITransaction();
}

void initDisplay() {
  DEBUG_PRINTLN("Initialisiere Display...");
  
  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  beginSPITransaction();
  if (!tft.begin()) {
    DEBUG_PRINTLN("Display-Initialisierung fehlgeschlagen!");
    // Versuche Reset
    digitalWrite(TFT_RST, LOW);
    delay(50);
    digitalWrite(TFT_RST, HIGH);
    delay(200);
    if (!tft.begin()) {
      DEBUG_PRINTLN("Display-Reset fehlgeschlagen!");
    }
  }
  tft.setRotation(2);
  endSPITransaction();
  
  // Display-Clear mit schwarzem Hintergrund
  safeFillRect(0, 0, SCREEN_W, SCREEN_H, BLACK);
  
  DEBUG_PRINTLN("Display initialisiert.");
}

void recoverDisplay() {
  DEBUG_PRINTLN("Versuche Display-Wiederherstellung...");
  initDisplay();
  
  // Aktuellen Modus neu zeichnen
  if (activeMode == -1) {
    drawMainMenu();
  } else {
    switch (activeMode) {
      case 0: drawSensorModeUI(); break;
      case 1: drawJoystickModeUI(); break;
      case 2: drawAutoModeUI(); break;
      case 3: drawPressureUI(); break;
    }
  }
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

// ==================== JOYSTICK ====================
void calibrateJoystick() {
  DEBUG_PRINTLN("Kalibriere Joystick...");
  
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
  
  DEBUG_PRINTF("Joystick Center: X=%d, Y=%d\n", centerX, centerY);
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

  // Deadzone für neutrale Position
  if (abs(dx) < DEADZONE && abs(dy) < DEADZONE) {
    menuNavArmed = true;
    return;
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
    // Warte bis Taste losgelassen
    while (digitalRead(JOY_SW) == LOW) { delay(10); }
    wasPressed = false;
    return true;
  }

  return false;
}

// ==================== DRUCKSENSOR ====================
float readPressureVoltage() {
  const int N = 64;
  long sum = 0;
  
  for (int i = 0; i < N; i++) {
    sum += analogRead(PRESS_PIN);
    delayMicroseconds(80);  // Stabilität für ADC
  }
  
  rawADC = (int)(sum / N);
  return (rawADC / 4095.0f) * 3.3f;  // 12-bit ADC, 3.3V Referenz
}

void calibratePressureZero() {
  pressCalibrating = true;
  DEBUG_PRINTLN("Starte Nullpunkt-Kalibrierung...");

  // UI Feedback
  safeFillRoundRect(14, CONTENT_Y + 105, SCREEN_W - 28, 80, 12, ORANGE_ACCENT);
  safeDrawRoundRect(14, CONTENT_Y + 105, SCREEN_W - 28, 80, 12, ORANGE_LIGHT);
  
  setTextStyle(2, BLACK);
  safeSetCursor(22, CONTENT_Y + 120);
  safePrint("Kalibrierung...");
  
  setTextStyle(1, BLACK);
  safeSetCursor(22, CONTENT_Y + 145);
  safePrint("Sensor in offener Luft");

  // Mittelwert über 100 Messungen
  const int N = 100;
  float sumV = 0;
  
  for (int i = 0; i < N; i++) {
    sumV += readPressureVoltage();
    delay(15);
  }

  settings.zeroVoltage = sumV / N;
  settings.spanCalibrated = false;  // Null-Kalibrierung invalidiert Span
  
  // Filter zurücksetzen
  pressFilt = -1.0f;
  pressOK = false;

  // In NVS speichern
  preferences.putFloat("zeroVoltage", settings.zeroVoltage);
  preferences.putBool("spanCalibrated", false);

  pressCalibrating = false;
  DEBUG_PRINTF("Nullpunkt kalibriert: %.3fV\n", settings.zeroVoltage);
  
  delay(300);
}

void calibratePressureSpan(float knownPressure) {
  pressCalibrating = true;
  DEBUG_PRINTLN("Starte Span-Kalibrierung...");

  // UI Feedback
  safeFillRoundRect(14, CONTENT_Y + 105, SCREEN_W - 28, 80, 12, ORANGE_ACCENT);
  safeDrawRoundRect(14, CONTENT_Y + 105, SCREEN_W - 28, 80, 12, ORANGE_LIGHT);
  
  setTextStyle(2, BLACK);
  safeSetCursor(22, CONTENT_Y + 120);
  safePrint("Span Kalibrierung...");
  
  setTextStyle(1, BLACK);
  safeSetCursor(22, CONTENT_Y + 145);
  safePrintf("Bekannter Druck: %.2f bar", knownPressure);

  // Mittelwert über 100 Messungen
  const int N = 100;
  float sumV = 0;
  
  for (int i = 0; i < N; i++) {
    sumV += readPressureVoltage();
    delay(15);
  }

  float currentVoltage = sumV / N;
  float sensorVoltage = currentVoltage / settings.dividerRatio;
  
  // Spannungsdifferenz berechnen (mit Invertierung)
  float voltageDiff;
  if (settings.pressureInvert) {
    voltageDiff = settings.zeroVoltage - sensorVoltage;
  } else {
    voltageDiff = sensorVoltage - settings.zeroVoltage;
  }
  
  // Skalierungsfaktor berechnen
  if (fabs(voltageDiff) > 0.01f) {  // Vermeide Division durch 0
    settings.pressureScale = knownPressure / voltageDiff;
    settings.spanVoltage = sensorVoltage;
    settings.knownPressure = knownPressure;
    settings.spanCalibrated = true;
    
    DEBUG_PRINTF("Span kalibriert: %.3fV -> %.2f bar (Scale: %.3f bar/V)\n", 
                 sensorVoltage, knownPressure, settings.pressureScale);
    
    // In NVS speichern
    preferences.putFloat("spanVoltage", settings.spanVoltage);
    preferences.putFloat("knownPressure", knownPressure);
    preferences.putFloat("pressureScale", settings.pressureScale);
    preferences.putBool("spanCalibrated", true);
  } else {
    DEBUG_PRINTLN("FEHLER: Spannungsdifferenz zu klein!");
    
    setTextStyle(1, RED);
    safeSetCursor(22, CONTENT_Y + 160);
    safePrint("FEHLER: Spannung zu klein!");
  }

  pressCalibrating = false;
  delay(300);
}

void updatePressure() {
  if (pressCalibrating) return;

  pressVolt = readPressureVoltage();
  
  // Spannungsteiler berücksichtigen
  float sensorVoltage = pressVolt / settings.dividerRatio;
  
  // Spannungsdifferenz berechnen (mit Invertierung)
  float voltageDiff;
  if (settings.pressureInvert) {
    voltageDiff = settings.zeroVoltage - sensorVoltage;
  } else {
    voltageDiff = sensorVoltage - settings.zeroVoltage;
  }
  
  // Druck berechnen
  if (settings.spanCalibrated) {
    // Verwendung des kalibrierten Skalierungsfaktors
    pressUnits = voltageDiff * settings.pressureScale;
  } else {
    // Fallback: Verwendung des manuellen Skalierungsfaktors
    pressUnits = voltageDiff * settings.pressureScale;
  }
  
  // Keine negativen Drücke anzeigen
  if (pressUnits < 0.0f) pressUnits = 0.0f;

  // Tiefpass-Filter
  if (pressFilt < 0) {
    pressFilt = pressUnits;
  } else {
    pressFilt = pressFilt * (1.0f - PRESS_ALPHA) + pressUnits * PRESS_ALPHA;
  }

  // Hysterese-Schaltung für Schwellwert
  if (!pressOK && pressFilt >= (settings.pressureThreshold + PRESS_HYST)) {
    pressOK = true;
  } else if (pressOK && pressFilt <= (settings.pressureThreshold - PRESS_HYST)) {
    pressOK = false;
  }
}

// ==================== UI-KOMPONENTEN ====================
void drawHeader(const char* title) {
  // Header-Bereich
  safeFillRect(0, 0, SCREEN_W, HEADER_H, ORANGE_DARK);
  safeDrawRect(0, HEADER_H - 1, SCREEN_W, 1, LINE_COLOR);
  
  // Titel
  setTextStyle(2, TEXT_WHITE);
  safeSetCursor(10, 8);
  safePrint("W-GLÄTTER");
  
  setTextStyle(1, TEXT_LIGHT);
  safeSetCursor(10, 28);
  safePrint(title);
  
  // Relais-Status
  safeFillRoundRect(SCREEN_W - 40, 10, 30, 24, 10, PANEL_BG);
  safeDrawRoundRect(SCREEN_W - 40, 10, 30, 24, 10, LINE_COLOR);
  
  // Status-LED
  beginSPITransaction();
  tft.fillCircle(SCREEN_W - 25, 22, 6, relayActive ? GREEN : RED_DARK);
  tft.drawCircle(SCREEN_W - 25, 22, 6, TEXT_WHITE);
  endSPITransaction();
}

void drawWaveform() {
  const int waveX = 125;
  const int waveY = 10;
  const int waveW = 70;
  const int waveH = 24;
  
  safeFillRect(waveX, waveY, waveW, waveH, ORANGE_DARK);
  
  if (relayActive) {
    beginSPITransaction();
    for (int i = 0; i < WAVE_POINTS - 1; i++) {
      int x1 = waveX + (i * waveW) / WAVE_POINTS;
      int x2 = waveX + ((i + 1) * waveW) / WAVE_POINTS;
      int y1 = waveY + waveH / 2 + waveValues[i] / 4;
      int y2 = waveY + waveH / 2 + waveValues[i + 1] / 4;
      tft.drawLine(x1, y1, x2, y2, WAVE_COLOR);
    }
    endSPITransaction();
  } else {
    safeDrawRect(waveX, waveY + waveH / 2, waveW, 1, LINE_COLOR);
  }
}

void drawFooter(const char* text) {
  safeFillRect(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, ORANGE_DARK);
  safeDrawRect(0, SCREEN_H - FOOTER_H, SCREEN_W, 1, LINE_COLOR);
  
  setTextStyle(1, TEXT_WHITE);
  safeSetCursor(8, SCREEN_H - FOOTER_H + 9);
  safePrint(text);
}

void drawMenuItem(uint8_t idx, bool selected) {
  const int x = 18, w = SCREEN_W - 36, h = 40;
  const int y = CONTENT_Y + 12 + idx * (h + 10);
  
  uint16_t fillColor = selected ? ORANGE_ACCENT : ORANGE_MAIN;
  uint16_t borderColor = selected ? ORANGE_LIGHT : LINE_COLOR;
  
  safeFillRoundRect(x, y, w, h, 10, fillColor);
  safeDrawRoundRect(x, y, w, h, 10, borderColor);
  
  setTextStyle(2, selected ? BLACK : TEXT_WHITE);
  safeSetCursor(x + 12, y + 12);
  safePrint(menuItems[idx]);
}

void drawMainMenu() {
  // Hintergrund
  safeFillRect(0, 0, SCREEN_W, SCREEN_H, BG_DARK);
  
  // Header
  drawHeader("Hauptmenü");
  
  // Hauptbereich
  safeFillRoundRect(10, CONTENT_Y, SCREEN_W - 20, CONTENT_H, 14, PANEL_BG);
  safeDrawRoundRect(10, CONTENT_Y, SCREEN_W - 20, CONTENT_H, 14, LINE_COLOR);
  
  // Menüpunkte
  for (uint8_t i = 0; i < menuCount; i++) {
    drawMenuItem(i, i == currentMenuSelection);
  }
  
  // Footer
  drawFooter("HOCH/RUNTER | EIN: Auswählen | 3x: Zurück");
}

// ==================== MODUS-UI ====================
void splashMode(const char* name) {
  safeFillRect(0, 0, SCREEN_W, SCREEN_H, BG_DARK);
  drawHeader(name);
  
  safeFillRoundRect(18, CONTENT_Y + 50, SCREEN_W - 36, 80, 14, ORANGE_ACCENT);
  safeDrawRoundRect(18, CONTENT_Y + 50, SCREEN_W - 36, 80, 14, ORANGE_LIGHT);
  
  setTextStyle(2, BLACK);
  safeSetCursor(28, CONTENT_Y + 82);
  safePrint(name);
  
  delay(300);
}

void drawSensorModeUI() {
  safeFillRect(0, 0, SCREEN_W, SCREEN_H, BG_DARK);
  drawHeader("Sensor-Modus");
  
  safeFillRoundRect(14, CONTENT_Y, SCREEN_W - 28, 170, 14, PANEL_BG);
  safeDrawRoundRect(14, CONTENT_Y, SCREEN_W - 28, 170, 14, LINE_COLOR);
  
  setTextStyle(2, TEXT_WHITE);
  safeSetCursor(26, CONTENT_Y + 40);
  safePrint("Warte auf Signal");
  
  safeSetCursor(26, CONTENT_Y + 70);
  safePrint("Aktiv bei LOW-Trigger");
  
  drawFooter("3x Klick: Zurück zum Menü");
}

void runSensorMode() {
  static bool activated = false;
  
  // Active-LOW: Trigger bei LOW
  bool trig = (digitalRead(SENSOR_TRIG_PIN) == LOW);
  
  // Re-Trigger Sperre: Nur scharf wenn HIGH
  if (!trig) sensorArmed = true;
  
  // Auslösen wenn scharf, Trigger LOW und Relais noch aus
  if (sensorArmed && trig && !relayActive) {
    activateRelay();
    relayTimer = millis() + settings.sensorDuration;
    activated = true;
    sensorArmed = false;
    
    // UI Feedback
    safeFillRoundRect(18, CONTENT_Y + 120, SCREEN_W - 36, 44, 12, ORANGE_ACCENT);
    safeDrawRoundRect(18, CONTENT_Y + 120, SCREEN_W - 36, 44, 12, ORANGE_LIGHT);
    
    setTextStyle(2, BLACK);
    safeSetCursor(52, CONTENT_Y + 134);
    safePrint("AKTIVIERT!");
  }
  
  // Timer für automatisches Abschalten
  if (relayActive && activated && millis() >= relayTimer) {
    deactivateRelay();
    activated = false;
    drawSensorModeUI();
  }
}

void drawJoystickModeUI() {
  safeFillRect(0, 0, SCREEN_W, SCREEN_H, BG_DARK);
  drawHeader("Joystick-Modus");
  
  safeFillRoundRect(14, CONTENT_Y, SCREEN_W - 28, 170, 14, PANEL_BG);
  safeDrawRoundRect(14, CONTENT_Y, SCREEN_W - 28, 170, 14, LINE_COLOR);
  
  setTextStyle(2, TEXT_WHITE);
  safeSetCursor(22, CONTENT_Y + 30);
  safePrint("Joystick halten");
  
  setTextStyle(1, TEXT_LIGHT);
  safeSetCursor(22, CONTENT_Y + 60);
  safePrint("Relais bleibt EIN");
  
  safeSetCursor(22, CONTENT_Y + 80);
  safePrintf("Ein: >%d  Aus: <%d", 
             settings.joyOnThreshold, settings.joyOffThreshold);
  
  drawFooter("3x Klick: Zurück zum Menü");
}

void drawJoyStatus(bool on) {
  uint16_t color = on ? GREEN : ORANGE_DARK;
  
  safeFillRoundRect(18, CONTENT_Y + 120, SCREEN_W - 36, 44, 12, color);
  safeDrawRoundRect(18, CONTENT_Y + 120, SCREEN_W - 36, 44, 12, ORANGE_LIGHT);
  
  setTextStyle(2, BLACK);
  safeSetCursor(80, CONTENT_Y + 134);
  safePrint(on ? "EIN" : "AUS");
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

void drawAutoModeUI() {
  safeFillRect(0, 0, SCREEN_W, SCREEN_H, BG_DARK);
  drawHeader("Automatik-Modus");
  
  safeFillRoundRect(14, CONTENT_Y, SCREEN_W - 28, 170, 14, PANEL_BG);
  safeDrawRoundRect(14, CONTENT_Y, SCREEN_W - 28, 170, 14, LINE_COLOR);
  
  setTextStyle(2, TEXT_WHITE);
  safeSetCursor(44, CONTENT_Y + 30);
  safePrint("AUTO PULS");
  
  setTextStyle(1, TEXT_LIGHT);
  safeSetCursor(52, CONTENT_Y + 58);
  safePrintf("EIN %ds / AUS %ds", 
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
    
    // Status anzeigen
    safeFillRoundRect(18, CONTENT_Y + 95, SCREEN_W - 36, 50, 14, 
                     autoState ? ORANGE_ACCENT : ORANGE_DARK);
    safeDrawRoundRect(18, CONTENT_Y + 95, SCREEN_W - 36, 50, 14, ORANGE_LIGHT);
    
    setTextStyle(2, autoState ? BLACK : ORANGE_LIGHT);
    safeSetCursor(62, CONTENT_Y + 112);
    safePrint(autoState ? "STROM EIN" : "STROM AUS");
  }
}

void drawPressureUI() {
  safeFillRect(0, 0, SCREEN_W, SCREEN_H, BG_DARK);
  drawHeader("Druckanzeige");
  
  safeFillRoundRect(14, CONTENT_Y, SCREEN_W - 28, 230, 14, PANEL_BG);
  safeDrawRoundRect(14, CONTENT_Y, SCREEN_W - 28, 230, 14, LINE_COLOR);
  
  drawFooter("Halten=Null | L/R=Schwelle | 3x=Zurück");
}

void drawPressureGauge() {
  // Hintergrund
  safeFillRoundRect(18, CONTENT_Y + 6, SCREEN_W - 36, 218, 12, PANEL_BG);
  
  // Titel
  setTextStyle(2, TEXT_WHITE);
  safeSetCursor(24, CONTENT_Y + 18);
  safePrint("Druck (bar)");
  
  // Aktueller Druck (groß)
  setTextStyle(2, ORANGE_LIGHT);
  safeSetCursor(24, CONTENT_Y + 45);
  safePrintf("%.3f", pressFilt);
  
  // Messwerte
  setTextStyle(1, TEXT_LIGHT);
  safeSetCursor(24, CONTENT_Y + 75);
  safePrintf("ADC: %d  V: %.3f", rawADC, pressVolt);
  
  safeSetCursor(24, CONTENT_Y + 92);
  safePrintf("NullV: %.3f", settings.zeroVoltage);
  
  if (settings.spanCalibrated) {
    safeSetCursor(24, CONTENT_Y + 109);
    safePrintf("SpanV: %.3f", settings.spanVoltage);
    
    safeSetCursor(24, CONTENT_Y + 126);
    safePrintf("Scale: %.3f bar/V", settings.pressureScale);
  }
  
  // Schwellwert
  safeSetCursor(24, CONTENT_Y + (settings.spanCalibrated ? 143 : 126));
  safePrintf("Schwelle: %.3f bar", settings.pressureThreshold);
  
  // Invert-Status
  safeSetCursor(24, CONTENT_Y + (settings.spanCalibrated ? 160 : 143));
  safePrintf("Invert: %s", settings.pressureInvert ? "JA" : "NEIN");
  
  // Status-Anzeige (OK/NICHT OK)
  uint16_t statusColor = pressOK ? GREEN : RED;
  const char* statusText = pressOK ? "OK" : "NICHT OK";
  
  safeFillRoundRect(18, CONTENT_Y + 174, SCREEN_W - 36, 44, 12, statusColor);
  safeDrawRoundRect(18, CONTENT_Y + 174, SCREEN_W - 36, 44, 12, ORANGE_LIGHT);
  
  setTextStyle(2, BLACK);
  safeSetCursor(84, CONTENT_Y + 188);
  safePrint(statusText);
  
  // Balkengrafik
  int barX = 24, barY = CONTENT_Y + 222, barW = 192, barH = 14;
  safeDrawRect(barX, barY, barW, barH, LINE_COLOR);
  safeFillRect(barX + 1, barY + 1, barW - 2, barH - 2, BLACK);
  
  float visMax = max(settings.pressureThreshold * 2.0f, 1.0f);
  float n = clampf(pressFilt, 0.0f, visMax);
  int fillWidth = (int)((n / visMax) * (barW - 2));
  fillWidth = clampi(fillWidth, 0, barW - 2);
  
  safeFillRect(barX + 1, barY + 1, fillWidth, barH - 2, statusColor);
}

void runPressureMode() {
  static unsigned long lastDraw = 0;
  static unsigned long lastThreshChange = 0;
  
  updatePressure();
  
  // Schwellwert mit Joystick einstellen
  int dx = joyDX();
  if (abs(dx) > MENU_THRESH && (millis() - lastThreshChange > 200)) {
    if (dx > MENU_THRESH) {
      settings.pressureThreshold += 0.02f;
    } else {
      settings.pressureThreshold -= 0.02f;
    }
    
    settings.pressureThreshold = clampf(settings.pressureThreshold, 0.00f, 5.00f);
    preferences.putFloat("pressureThreshold", settings.pressureThreshold);
    lastThreshChange = millis();
  }
  
  // Lange Taste für Null-Kalibrierung
  if (isButtonHeld(1500)) {
    calibratePressureZero();
    drawPressureGauge();  // Sofort aktualisieren
  }
  
  // Regelmäßige UI-Aktualisierung
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
    <title>W-Glätter Steuerung</title>
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
            --error: #f44336;
            --panel-bg: rgba(255, 140, 0, 0.1);
            --border: rgba(255, 140, 0, 0.3);
        }
        
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;
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
            letter-spacing: 0.5px;
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
            transition: transform 0.2s, box-shadow 0.2s;
        }
        
        .card:hover {
            transform: translateY(-2px);
            box-shadow: 0 8px 20px rgba(0, 0, 0, 0.4);
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
            font-family: 'JetBrains Mono', 'Cascadia Code', monospace;
            font-weight: 500;
            font-size: 1.1em;
        }
        
        .status-on { color: var(--success); }
        .status-off { color: var(--error); }
        .status-ok { color: var(--success); }
        .status-notok { color: var(--error); }
        
        .oscilloscope-container {
            margin-top: 25px;
            background: rgba(0, 0, 0, 0.5);
            border-radius: 10px;
            padding: 20px;
            border: 1px solid rgba(255, 255, 255, 0.1);
        }
        
        .oscilloscope-title {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 15px;
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
        
        input[type="range"] {
            width: 100%;
            height: 8px;
            background: rgba(255, 140, 0, 0.2);
            border-radius: 4px;
            outline: none;
            -webkit-appearance: none;
        }
        
        input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            width: 22px;
            height: 22px;
            background: var(--orange-medium);
            border-radius: 50%;
            cursor: pointer;
            border: 2px solid var(--text-light);
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
        
        .checkbox-group {
            display: flex;
            align-items: center;
            gap: 10px;
            margin: 15px 0;
        }
        
        input[type="checkbox"] {
            width: 20px;
            height: 20px;
            accent-color: var(--orange-medium);
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
            margin: 8px;
            transition: all 0.2s;
            display: inline-flex;
            align-items: center;
            gap: 8px;
        }
        
        button:hover {
            transform: translateY(-2px);
            box-shadow: 0 6px 12px rgba(0, 0, 0, 0.3);
            background: linear-gradient(90deg, var(--orange-medium) 0%, var(--orange-light) 100%);
        }
        
        button:active {
            transform: translateY(0);
        }
        
        button.success {
            background: linear-gradient(90deg, #2e7d32 0%, var(--success) 100%);
        }
        
        button.danger {
            background: linear-gradient(90deg, #c62828 0%, var(--error) 100%);
        }
        
        .button-group {
            display: flex;
            flex-wrap: wrap;
            gap: 10px;
            margin-top: 20px;
        }
        
        .calibration-section {
            margin-top: 25px;
            padding-top: 20px;
            border-top: 2px solid var(--border);
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
            display: flex;
            align-items: center;
            gap: 12px;
            max-width: 350px;
        }
        
        .notification.error {
            background: var(--error);
        }
        
        @keyframes slideIn {
            from {
                transform: translateX(100%);
                opacity: 0;
            }
            to {
                transform: translateX(0);
                opacity: 1;
            }
        }
        
        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.7; }
        }
        
        .pulsing {
            animation: pulse 2s infinite;
        }
        
        .hidden { display: none; }
        
        @media (max-width: 768px) {
            .grid {
                grid-template-columns: 1fr;
            }
            
            header {
                padding: 20px;
            }
            
            h1 {
                font-size: 2em;
            }
        }
    </style>
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
</head>
<body>
    <div class="container">
        <header>
            <h1><i class="fas fa-bolt"></i> W-GLÄTTER STEUERUNG</h1>
            <div class="subtitle">
                ESP32 Web Interface | IP: 192.168.4.1 | Verbunden: <span id="clientCount">0</span> Clients
            </div>
        </header>
        
        <div class="grid">
            <!-- Status Panel -->
            <div class="card">
                <h2><i class="fas fa-chart-line"></i> Systemstatus</h2>
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
                        <span class="status-value" id="pressureScale">-</span>
                    </div>
                    <div class="status-item">
                        <span class="status-label">Span kalibriert:</span>
                        <span class="status-value" id="spanCal">-</span>
                    </div>
                    <div class="status-item">
                        <span class="status-label">Invertiert:</span>
                        <span class="status-value" id="invertStatus">-</span>
                    </div>
                    <div class="status-item">
                        <span class="status-label">Uptime:</span>
                        <span class="status-value" id="uptime">-</span>
                    </div>
                </div>
                
                <div class="oscilloscope-container">
                    <div class="oscilloscope-title">
                        <h3><i class="fas fa-wave-square"></i> Motoraktivität</h3>
                        <span id="waveStatus" class="pulsing">Live</span>
                    </div>
                    <canvas id="oscilloscope"></canvas>
                </div>
            </div>
            
            <!-- Einstellungen Panel -->
            <div class="card">
                <h2><i class="fas fa-sliders-h"></i> Einstellungen</h2>
                
                <div class="setting-group">
                    <label class="setting-label">Druckschwelle: <span id="thresholdValue">0.35</span> bar</label>
                    <input type="range" id="threshold" min="0" max="5" step="0.01" value="0.35">
                </div>
                
                <div class="setting-group">
                    <label class="setting-label">Manuelle Druckskala: <input type="number" id="scale" min="0.1" max="10" step="0.1" value="1.0"> bar/V</label>
                </div>
                
                <div class="setting-group">
                    <label class="setting-label">Spannungsteiler Verhältnis: <input type="number" id="divider" min="0.1" max="1.0" step="0.01" value="1.00"></label>
                </div>
                
                <div class="checkbox-group">
                    <input type="checkbox" id="pressureInvert">
                    <label class="setting-label" for="pressureInvert">Spannung invertieren (sinkt bei Druck)</label>
                </div>
                
                <div class="setting-group">
                    <label class="setting-label">Joystick EIN-Schwelle: <input type="number" id="joyOn" min="100" max="2000" value="650"></label>
                    <label class="setting-label">Joystick AUS-Schwelle: <input type="number" id="joyOff" min="100" max="2000" value="420"></label>
                </div>
                
                <div class="setting-group">
                    <label class="setting-label">Auto EIN Zeit: <input type="number" id="autoOn" min="100" max="30000" value="5000"> ms</label>
                    <label class="setting-label">Auto AUS Zeit: <input type="number" id="autoOff" min="100" max="30000" value="2000"> ms</label>
                </div>
                
                <div class="setting-group">
                    <label class="setting-label">Sensor Dauer: <input type="number" id="sensorDur" min="100" max="60000" value="10000"> ms</label>
                </div>
                
                <div class="button-group">
                    <button onclick="saveSettings()"><i class="fas fa-save"></i> Einstellungen speichern</button>
                    <button class="danger" onclick="toggleRelay()"><i class="fas fa-power-off"></i> Relais umschalten</button>
                </div>
                
                <!-- Kalibrierungs-Sektion -->
                <div class="calibration-section">
                    <h3><i class="fas fa-tachometer-alt"></i> Kalibrierung</h3>
                    
                    <div class="button-group">
                        <button class="success" onclick="calibrateZero()">
                            <i class="fas fa-balance-scale"></i> Nullpunkt kalibrieren
                        </button>
                    </div>
                    
                    <div class="setting-group" style="margin-top: 15px;">
                        <label class="setting-label">Span-Kalibrierung (bekannter Druck):</label>
                        <input type="number" id="knownPressure" min="0.01" max="5.0" step="0.01" value="0.45" style="width: 100%; max-width: 200px;">
                        <button class="success" onclick="calibrateSpan()" style="margin-top: 10px;">
                            <i class="fas fa-ruler"></i> Span kalibrieren
                        </button>
                    </div>
                    
                    <p style="color: var(--text-medium); font-size: 0.9em; margin-top: 15px; line-height: 1.5;">
                        <i class="fas fa-info-circle"></i> Anleitung:<br>
                        1. <strong>Null kalibrieren:</strong> Sensor in offener Luft<br>
                        2. <strong>Span kalibrieren:</strong> Bekannten Druck anwenden (z.B. 0.45 bar)<br>
                        3. Bei invertierten Sensoren: "Spannung invertieren" aktivieren
                    </p>
                </div>
            </div>
        </div>
    </div>
    
    <!-- Notification -->
    <div id="notification" class="notification hidden">
        <i class="fas fa-check-circle"></i>
        <span id="notificationText"></span>
    </div>
    
    <script>
        let waveData = new Array(100).fill(0);
        let waveIndex = 0;
        let startTime = Date.now();
        const canvas = document.getElementById('oscilloscope');
        const ctx = canvas.getContext('2d');
        
        // Canvas initialisieren
        function initCanvas() {
            canvas.width = canvas.clientWidth;
            canvas.height = canvas.clientHeight;
            drawOscilloscope(false);
        }
        
        // Status abrufen
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
                pressureStatusEl.textContent = data.pressureOK ? 'OK' : 'NICHT OK';
                pressureStatusEl.className = 'status-value ' + (data.pressureOK ? 'status-ok' : 'status-notok');
                
                document.getElementById('adcVolt').textContent = data.adc + ' / ' + data.volt.toFixed(3) + 'V';
                document.getElementById('zeroV').textContent = data.zeroV.toFixed(3) + 'V';
                document.getElementById('pressureScale').textContent = data.pressureScale.toFixed(3);
                
                const spanCalEl = document.getElementById('spanCal');
                spanCalEl.textContent = data.spanCalibrated ? 'Ja' : 'Nein';
                spanCalEl.className = 'status-value ' + (data.spanCalibrated ? 'status-ok' : 'status-notok');
                
                const invertEl = document.getElementById('invertStatus');
                invertEl.textContent = data.pressureInvert ? 'Ja' : 'Nein';
                invertEl.className = 'status-value ' + (data.pressureInvert ? 'status-ok' : 'status-notok');
                
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
                
                // Einstellungen-Slider nur aktualisieren wenn nicht gerade bewegt
                if (!document.getElementById('threshold').classList.contains('dragging')) {
                    document.getElementById('threshold').value = data.threshold;
                    document.getElementById('thresholdValue').textContent = data.threshold.toFixed(2);
                }
                
                // Checkbox für Invertierung
                document.getElementById('pressureInvert').checked = data.pressureInvert;
                
                // Andere Einstellungen
                document.getElementById('scale').value = data.scale;
                document.getElementById('divider').value = data.dividerRatio;
                document.getElementById('joyOn').value = data.joyOn;
                document.getElementById('joyOff').value = data.joyOff;
                document.getElementById('autoOn').value = data.autoOn;
                document.getElementById('autoOff').value = data.autoOff;
                document.getElementById('sensorDur').value = data.sensorDuration;
                
            } catch (error) {
                console.error('Fehler beim Status-Abruf:', error);
            }
        }
        
        // Oszilloskop zeichnen
        function drawOscilloscope(relayActive) {
            const width = canvas.width;
            const height = canvas.height;
            
            // Hintergrund
            ctx.fillStyle = '#000';
            ctx.fillRect(0, 0, width, height);
            
            // Gitter
            ctx.strokeStyle = '#222';
            ctx.lineWidth = 1;
            
            // Vertikale Linien
            for (let x = 0; x < width; x += width / 20) {
                ctx.beginPath();
                ctx.moveTo(x, 0);
                ctx.lineTo(x, height);
                ctx.stroke();
            }
            
            // Horizontale Linien
            for (let y = 0; y < height; y += height / 8) {
                ctx.beginPath();
                ctx.moveTo(0, y);
                ctx.lineTo(width, y);
                ctx.stroke();
            }
            
            // Mittellinie
            ctx.strokeStyle = '#333';
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.moveTo(0, height / 2);
            ctx.lineTo(width, height / 2);
            ctx.stroke();
            
            if (relayActive) {
                // Wellenform mit Glow-Effekt
                ctx.strokeStyle = '#0ff';
                ctx.lineWidth = 3;
                ctx.shadowBlur = 10;
                ctx.shadowColor = '#0ff';
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
                
                // Reset Shadow
                ctx.shadowBlur = 0;
            } else {
                // Horizontale Linie wenn inaktiv
                ctx.strokeStyle = '#444';
                ctx.lineWidth = 2;
                ctx.beginPath();
                ctx.moveTo(0, height / 2);
                ctx.lineTo(width, height / 2);
                ctx.stroke();
            }
        }
        
        // Einstellungen speichern
        async function saveSettings() {
            const settings = {
                threshold: parseFloat(document.getElementById('threshold').value),
                scale: parseFloat(document.getElementById('scale').value),
                dividerRatio: parseFloat(document.getElementById('divider').value),
                pressureInvert: document.getElementById('pressureInvert').checked,
                joyOn: parseInt(document.getElementById('joyOn').value),
                joyOff: parseInt(document.getElementById('joyOff').value),
                autoOn: parseInt(document.getElementById('autoOn').value),
                autoOff: parseInt(document.getElementById('autoOff').value),
                sensorDuration: parseInt(document.getElementById('sensorDur').value)
            };
            
            try {
                const response = await fetch('/api/settings', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(settings)
                });
                
                const data = await response.json();
                showNotification('Einstellungen erfolgreich gespeichert!');
            } catch (error) {
                showNotification('Fehler beim Speichern der Einstellungen!', true);
                console.error('Error:', error);
            }
        }
        
        // Nullpunkt kalibrieren
        async function calibrateZero() {
            if (!confirm('Nullpunkt kalibrieren? Sensor muss in offener Luft sein.')) {
                return;
            }
            
            try {
                const response = await fetch('/api/calibrateZero', { method: 'POST' });
                const data = await response.json();
                showNotification('Nullpunkt erfolgreich kalibriert!');
            } catch (error) {
                showNotification('Fehler bei der Kalibrierung!', true);
                console.error('Error:', error);
            }
        }
        
        // Span kalibrieren
        async function calibrateSpan() {
            const knownPressure = parseFloat(document.getElementById('knownPressure').value);
            
            if (!knownPressure || knownPressure <= 0) {
                showNotification('Bitte einen gültigen Druck eingeben!', true);
                return;
            }
            
            if (!confirm(`Span-Kalibrierung mit ${knownPressure} bar durchführen?`)) {
                return;
            }
            
            try {
                const response = await fetch('/api/calibrateSpan', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ knownPressure: knownPressure })
                });
                
                const data = await response.json();
                if (data.status === 'calibrated') {
                    showNotification(`Span erfolgreich mit ${knownPressure} bar kalibriert!`);
                } else {
                    showNotification('Fehler: ' + data.error, true);
                }
            } catch (error) {
                showNotification('Fehler bei der Span-Kalibrierung!', true);
                console.error('Error:', error);
            }
        }
        
        // Relais umschalten
        async function toggleRelay() {
            try {
                const response = await fetch('/api/toggleRelay', { method: 'POST' });
                const data = await response.json();
                showNotification('Relais ' + (data.relay ? 'EINGESCHALTET' : 'AUSGESCHALTET'));
            } catch (error) {
                showNotification('Fehler beim Umschalten des Relais!', true);
                console.error('Error:', error);
            }
        }
        
        // Benachrichtigung anzeigen
        function showNotification(message, isError = false) {
            const notification = document.getElementById('notification');
            const text = document.getElementById('notificationText');
            
            text.textContent = message;
            notification.className = 'notification' + (isError ? ' error' : '');
            notification.classList.remove('hidden');
            
            setTimeout(() => {
                notification.classList.add('hidden');
            }, 3000);
        }
        
        // Event-Listener
        document.getElementById('threshold').addEventListener('mousedown', () => {
            document.getElementById('threshold').classList.add('dragging');
        });
        
        document.getElementById('threshold').addEventListener('mouseup', () => {
            document.getElementById('threshold').classList.remove('dragging');
        });
        
        document.getElementById('threshold').addEventListener('input', (e) => {
            document.getElementById('thresholdValue').textContent = parseFloat(e.target.value).toFixed(2);
        });
        
        // Initialisierung
        window.addEventListener('resize', initCanvas);
        initCanvas();
        
        // Polling starten
        setInterval(updateStatus, 250);
        updateStatus();
    </script>
</body>
</html>
)rawliteral";

// ==================== WEB SERVER HANDLER ====================
String getJsonValue(String json, String key) {
  int start = json.indexOf("\"" + key + "\":");
  if (start == -1) return "";
  start += key.length() + 3;
  int end = json.indexOf(",", start);
  if (end == -1) end = json.indexOf("}", start);
  if (end == -1) return "";
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
  json += "\"pressureScale\":" + String(settings.pressureScale, 3) + ",";
  json += "\"spanCalibrated\":" + String(settings.spanCalibrated ? "true" : "false") + ",";
  json += "\"pressureInvert\":" + String(settings.pressureInvert ? "true" : "false") + ",";
  json += "\"pressureOK\":" + String(pressOK ? "true" : "false") + ",";
  json += "\"threshold\":" + String(settings.pressureThreshold, 2) + ",";
  json += "\"scale\":" + String(settings.pressureScale, 1) + ",";
  json += "\"dividerRatio\":" + String(settings.dividerRatio, 2) + ",";
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

    settings.pressureThreshold = getJsonValue(json, "threshold").toFloat();
    settings.pressureScale = getJsonValue(json, "scale").toFloat();
    settings.dividerRatio = getJsonValue(json, "dividerRatio").toFloat();
    
    String invertStr = getJsonValue(json, "pressureInvert");
    settings.pressureInvert = (invertStr == "true");
    
    settings.joyOnThreshold = getJsonValue(json, "joyOn").toInt();
    settings.joyOffThreshold = getJsonValue(json, "joyOff").toInt();
    settings.autoOnTime = getJsonValue(json, "autoOn").toInt();
    settings.autoOffTime = getJsonValue(json, "autoOff").toInt();
    settings.sensorDuration = getJsonValue(json, "sensorDuration").toInt();

    // In NVS speichern
    preferences.putFloat("pressureThreshold", settings.pressureThreshold);
    preferences.putFloat("pressureScale", settings.pressureScale);
    preferences.putFloat("dividerRatio", settings.dividerRatio);
    preferences.putBool("pressureInvert", settings.pressureInvert);
    preferences.putInt("joyOnThreshold", settings.joyOnThreshold);
    preferences.putInt("joyOffThreshold", settings.joyOffThreshold);
    preferences.putInt("autoOnTime", settings.autoOnTime);
    preferences.putInt("autoOffTime", settings.autoOffTime);
    preferences.putInt("sensorDuration", settings.sensorDuration);

    server.send(200, "application/json", "{\"status\":\"ok\"}");
  } else {
    server.send(400, "application/json", "{\"error\":\"Invalid request\"}");
  }
}

void handleCalibrateZero() {
  calibratePressureZero();
  server.send(200, "application/json", "{\"status\":\"calibrated\"}");
}

void handleCalibrateSpan() {
  if (server.hasArg("plain")) {
    String json = server.arg("plain");
    float knownPressure = getJsonValue(json, "knownPressure").toFloat();
    
    if (knownPressure > 0.0f && knownPressure <= 5.0f) {
      calibratePressureSpan(knownPressure);
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
  
  settings.pressureThreshold = preferences.getFloat("pressureThreshold", 0.35f);
  settings.pressureScale = preferences.getFloat("pressureScale", 1.0f);
  settings.dividerRatio = preferences.getFloat("dividerRatio", 1.0f);
  settings.pressureInvert = preferences.getBool("pressureInvert", false);
  
  settings.joyOnThreshold = preferences.getInt("joyOnThreshold", 650);
  settings.joyOffThreshold = preferences.getInt("joyOffThreshold", 420);
  settings.autoOnTime = preferences.getInt("autoOnTime", 5000);
  settings.autoOffTime = preferences.getInt("autoOffTime", 2000);
  settings.sensorDuration = preferences.getInt("sensorDuration", 10000);
  
  settings.zeroVoltage = preferences.getFloat("zeroVoltage", 0.0f);
  settings.spanVoltage = preferences.getFloat("spanVoltage", 0.0f);
  settings.knownPressure = preferences.getFloat("knownPressure", 0.0f);
  settings.spanCalibrated = preferences.getBool("spanCalibrated", false);
  
  DEBUG_PRINTF("Geladen: Threshold=%.2f, Invert=%d, Scale=%.3f\n", 
               settings.pressureThreshold, settings.pressureInvert, settings.pressureScale);
}

// ==================== OSZILLOSKOP ANIMATION ====================
void updateWaveform() {
  if (millis() - lastWaveUpdate >= WAVE_UPDATE_MS) {
    if (relayActive) {
      // Sinus-Welle mit etwas Rauschen
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
  DEBUG_PRINTLN("\n\n=== W-GLÄTTER STARTET ===");
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
  
  // NVS initialisieren
  preferences.begin(NVS_NAMESPACE, false);
  loadSettings();
  
  // Display initialisieren
  initDisplay();
  
  // Startbildschirm
  setTextStyle(2, TEXT_WHITE);
  safeSetCursor(40, 60);
  safePrint("W-GLÄTTER");
  
  setTextStyle(1, TEXT_LIGHT);
  safeSetCursor(60, 90);
  safePrint("Starte...");
  
  delay(500);
  
  // Joystick kalibrieren
  calibrateJoystick();
  DEBUG_PRINTLN("Joystick kalibriert");
  
  // Druck-Nullpunkt kalibrieren (falls noch nicht geschehen)
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
  server.on("/api/calibrateSpan", HTTP_POST, handleCalibrateSpan);
  server.on("/api/toggleRelay", HTTP_POST, handleToggleRelay);
  server.begin();
  
  DEBUG_PRINTLN("Web Server gestartet");
  DEBUG_PRINTF("SSID: %s\n", apSSID);
  DEBUG_PRINT("IP: "); DEBUG_PRINTLN(WiFi.softAPIP());
  
  // Hauptmenü anzeigen
  drawMainMenu();
  
  lastDisplayUpdate = millis();
}

// ==================== HAUPTSCHLEIFE ====================
void loop() {
  // Web Server Anfragen verarbeiten
  server.handleClient();
  
  // Joystick Filter aktualisieren
  updateJoyFilter();
  
  // Oszilloskop Animation
  updateWaveform();
  
  // Display Recovery (alle 30 Sekunden prüfen)
  if (millis() - lastDisplayUpdate > DISPLAY_RECOVERY_INTERVAL) {
    lastDisplayUpdate = millis();
    // Einfache Prüfung - könnte erweitert werden
  }
  
  // Tastenereignis verarbeiten
  if (buttonPressedEdge()) {
    if (activeMode == -1) {
      // Im Menü: Einfacher Klick für Auswahl
      if (buttonClickCount == 1) {
        activeMode = currentMenuSelection;
        splashMode(menuItems[currentMenuSelection]);
        
        switch (activeMode) {
          case 0:  // Sensor-Modus
            sensorArmed = true;
            drawSensorModeUI();
            break;
            
          case 1:  // Joystick-Modus
            joyHoldLatched = false;
            deactivateRelay();
            drawJoystickModeUI();
            drawJoyStatus(false);
            break;
            
          case 2:  // Automatik-Modus
            autoFirst = true;
            drawAutoModeUI();
            break;
            
          case 3:  // Druckanzeige
            drawPressureUI();
            drawPressureGauge();
            break;
        }
      }
    } else {
      // In einem Modus: Dreifach-Klick für Zurück
      if (tripleClickDetected()) {
        // Sicherheit: Nur zurück wenn nicht im Automatik-Modus ODER Relais aus
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
  
  // Modus-spezifische Logik
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
  
  // Kurze Pause für Stabilität
  delay(10);
}