// Сначала подключаем стандартную сеть и сервер
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h> 
#include <SPI.h>

// Экран подключаем ПОСЛЕ сетевых библиотек
#include <TFT_eSPI.h>

// Подключаем надежную библиотеку Adafruit для светодиода
#include <Adafruit_NeoPixel.h>

TFT_eSPI tft = TFT_eSPI();
Preferences prefs;
WebServer server(80); 

// Инициализируем 1 светодиод на пине 48
Adafruit_NeoPixel led(1, 48, NEO_GRB + NEO_KHZ800);

// --- НАСТРОЙКИ ЖЕЛЕЗА ---
#define BUTTON_PIN 0      
#define RGB_LED_PIN 48    
#define TEST_WAVE_PIN 42  
#define TFT_BL_PIN 38     // Пин подсветки дисплея

// --- ДОСТУПНЫЕ АНАЛОГОВЫЕ ПИНЫ ---
const int adcPins[] = {4, 5, 9}; 
const int NUM_ADC_PINS = 3;

// --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ГРАФИКА ---
volatile float voltage = 0.0;
int xPos = 0;
int prevX = 0;
int prevY = 0;
int prevAdcValue = 0;

// Буфер для передачи волны по Wi-Fi
const int WAVE_BUFFER_SIZE = 60;
int waveBuffer[WAVE_BUFFER_SIZE];
int waveBufferIdx = 0;

// Аналитика сигнала
volatile int currentMinAdc = 4095;
volatile int currentMaxAdc = 0;
volatile int lastMinAdc = 0;
volatile int lastMaxAdc = 4095;
volatile int crossings = 0;
unsigned long sweepStartTime = 0;
volatile float freqHz = 0.0;
volatile float vMax = 0.0;
volatile float vMin = 0.0;
bool isFrozen = false; 

// Пользовательские настройки
int currentPinIndex; 
int graphSpeed;
int smoothLevel;     
int zoomIndex;       
int graphColorIndex;
bool useGrid;
int ledMode;         
int ledColorIndex;   
int testFreqIndex;   
bool wifiEnabled = true; 
bool graphStyleLine = true; 
int brightnessIndex = 0;    

// МЕЖЪЯДЕРНЫЙ ФЛАГ ДЛЯ БАГФИКСА ГЕНЕРАТОРА
volatile bool generatorChanged = false;

// Списки параметров
uint16_t graphColors[] = {TFT_GREEN, TFT_YELLOW, TFT_CYAN, TFT_WHITE};
const int zoomLevels[] = {1, 2, 4, 8};
const char* colorNames[] = {"GREEN", "YELLOW", "CYAN", "WHITE"};
const char* smoothNames[] = {"DISABLED", "LIGHT", "MEDIUM", "STRICT"};
const char* ledModes[] = {"DISABLED", "STATIC", "RAINBOW", "SWEEP"};
const char* ledColorNames[] = {"WHITE", "RED", "GREEN", "BLUE", "YELLOW", "CYAN", "MAGENTA"};
const int brightnessLevels[] = {255, 128, 50};
const char* brightnessNames[] = {"100%", "50%", "20%"};

// Массив RGB значений для Adafruit NeoPixel
const uint8_t ledRGBValues[][3] = {
  {255, 255, 255}, // WHITE
  {255, 0, 0},     // RED
  {0, 255, 0},     // GREEN
  {0, 0, 255},     // BLUE
  {255, 255, 0},   // YELLOW
  {0, 255, 255},   // CYAN
  {255, 0, 255}    // MAGENTA
};

const int testFreqs[] = {10, 50, 100, 500, 1000, 5000, 10000, 100000};
const int NUM_FREQS = 8;

uint8_t rainbowHue = 0;
unsigned long lastLedUpdate = 0;
int sweepDirection = 1;
int sweepVal = 0;

// --- СОСТОЯНИЯ МЕНЮ ---
int currentMode = 0; 
int menuCursor = 0;
const int MENU_ITEMS_COUNT = 12; 

// --- ТРИГГЕРЫ КНОПКИ ---
bool isPressing = false;
unsigned long buttonPressTime = 0;
bool longPressedTriggered = false;

// Прототипы
void loadSettings();
void saveSettings();
void handleButton();
void drawOscilloscope();
void drawGrid();
void updateLED();
void handleLEDAnimations();
uint32_t ledWheel(uint8_t wheelPos);
void onLongPressAction();
void onShortPressAction();
void updateMenuDisplay();
void updateGenerator();
void toggleWiFi();
void updateBrightness();

// ==========================================
// WEB-ИНТЕРФЕЙС С ГРАФИКОМ
// ==========================================
const char PAGE_MAIN[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <title>ESP32 Quantum Scope</title>
    <style>
        :root { --bg: #0a0a12; --card: #141424; --accent: #00ffcc; --text: #ffffff; --neon-red: #ff0055; }
        body { background: var(--bg); color: var(--text); font-family: 'Segoe UI', system-ui, sans-serif; margin: 0; padding: 20px; display: flex; flex-direction: column; align-items: center; }
        h1 { margin-bottom: 5px; text-transform: uppercase; letter-spacing: 2px; color: var(--accent); text-shadow: 0 0 10px rgba(0,255,204,0.3); font-size: 24px; }
        .subtitle { color: #888; font-size: 12px; margin-bottom: 25px; }
        #waveCanvas { background: #05050d; border: 2px solid #222; border-radius: 16px; box-shadow: 0 0 20px rgba(0,255,204,0.1); width: 100%; max-width: 570px; margin-bottom: 20px; }
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(130px, 1fr)); gap: 15px; width: 100%; max-width: 600px; margin-bottom: 25px; }
        .card { background: var(--card); padding: 15px; border-radius: 12px; border: 1px solid #222; text-align: center; box-shadow: 0 4px 15px rgba(0,0,0,0.5); }
        .card .label { font-size: 11px; text-transform: uppercase; color: #666; letter-spacing: 1px; margin-bottom: 5px; }
        .card .val { font-size: 22px; font-weight: bold; color: #fff; font-family: monospace; }
        .control-panel { background: var(--card); width: 100%; max-width: 570px; padding: 20px; border-radius: 16px; border: 1px solid #222; margin-bottom: 25px; }
        .control-panel button { background: #1f1f38; color: white; border: 1px solid #333; padding: 8px 16px; border-radius: 8px; font-size: 14px; cursor: pointer; width: 100%; font-weight: bold; text-transform: uppercase; border-color: var(--neon-red); color: var(--neon-red); background: rgba(255,0,85,0.05); }
    </style>
</head>
<body>
    <h1>⚡ Quantum Scope UI ⚡</h1>
    <div class='subtitle'>ESP32-S3 Hardware Dashboard</div>
    <canvas id="waveCanvas" width="500" height="220"></canvas>
    <div class='grid'>
        <div class='card'><div class='label'>Voltage</div><div class='val' id='v' style='color: var(--accent)'>-- V</div></div>
        <div class='card'><div class='label'>V-Max</div><div class='val' id='vmax'>-- V</div></div>
        <div class='card'><div class='label'>V-Min</div><div class='val' id='vmin'>-- V</div></div>
        <div class='card'><div class='label'>Frequency</div><div class='val' id='freq' style='color: #ffaa00'>-- Hz</div></div>
    </div>
    <div class='control-panel'>
        <button onclick='sendSet("freeze", "1")'>Поставить на паузу (HOLD)</button>
    </div>
    <script>
        async function sendSet(param, val) { await fetch(`/set?${param}=${val}`); }
        let canvas = document.getElementById('waveCanvas'); let ctx = canvas.getContext('2d');
        setInterval(async () => {
            try {
                let r = await fetch('/data'); let j = await r.json();
                document.getElementById('v').innerText = j.v.toFixed(2) + ' V';
                document.getElementById('vmax').innerText = j.vmax.toFixed(2) + ' V';
                document.getElementById('vmin').innerText = j.vmin.toFixed(2) + ' V';
                document.getElementById('freq').innerText = j.freq >= 1000 ? (j.freq/1000).toFixed(2) + ' kHz' : j.freq.toFixed(0) + ' Hz';
                ctx.clearRect(0, 0, canvas.width, canvas.height);
                ctx.strokeStyle = '#111122'; ctx.lineWidth = 1;
                for(let m=1; m<4; m++) { ctx.beginPath(); ctx.moveTo(0, canvas.height*m/4); ctx.lineTo(canvas.width, canvas.height*m/4); ctx.stroke(); }
                ctx.strokeStyle = '#00ffcc'; ctx.lineWidth = 3; ctx.beginPath();
                for(let i=0; i<j.wave.length; i++) {
                    let x = (i / (j.wave.length-1)) * canvas.width; let y = canvas.height - ((j.wave[i] / 4095) * canvas.height);
                    if(i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
                }
                ctx.stroke();
            } catch(e) {}
        }, 200);
    </script>
</body>
</html>
)rawliteral";

void handleRoot() { server.send_P(200, "text/html", PAGE_MAIN); }

void handleData() {
  String json = "{\"v\":" + String(voltage) + ",";
  json += "\"vmax\":" + String(vMax) + ",";
  json += "\"vmin\":" + String(vMin) + ",";
  json += "\"freq\":" + String(freqHz) + ",";
  json += "\"wave\":[";
  for (int i = 0; i < WAVE_BUFFER_SIZE; i++) {
    json += String(waveBuffer[i]);
    if (i < WAVE_BUFFER_SIZE - 1) json += ",";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleSet() {
  if (server.hasArg("freeze")) isFrozen = !isFrozen;
  server.send(200, "text/plain", "OK");
}

// ==========================================
// ЗАДАЧА ДЛЯ ЯДРА 0 (Wi-Fi + Сервер)
// ==========================================
void core0Task(void * pvParameters) {
  if (wifiEnabled) {
    WiFi.softAP("ESP32-Scope", "12345678"); 
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.on("/set", handleSet);
    server.begin();
  }
  pinMode(TEST_WAVE_PIN, OUTPUT);
  for (;;) {
    if (wifiEnabled) server.handleClient(); 
    if (testFreqs[testFreqIndex] == 10) digitalWrite(TEST_WAVE_PIN, (millis() / 50) % 2); 
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}

// ==========================================
// ОСНОВНОЙ ЦИКЛ (ЯДРО 1)
// ==========================================
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(TFT_BL_PIN, OUTPUT); 
  
  // Запуск Adafruit NeoPixel
  led.begin();
  led.setBrightness(50); // Комфортная базовая яркость (макс 255)

  loadSettings();
  updateBrightness();
  
  xTaskCreatePinnedToCore(core0Task, "Core0Task", 8192, NULL, 1, NULL, 0);
  updateGenerator(); 
  
  tft.init();
  tft.setRotation(1); 
  tft.fillScreen(TFT_BLACK);
  
  prevY = tft.height() / 2;
  sweepStartTime = millis();
  updateLED(); 
}

void loop() {
  handleButton(); 
  handleLEDAnimations(); 

  if (generatorChanged) {
    generatorChanged = false;
    updateGenerator();
    if (currentMode == 1) updateMenuDisplay(); 
  }

  if (currentMode == 0) {
    if (!isFrozen) drawOscilloscope();
  }
  delay(1);
}

void toggleWiFi() {
  wifiEnabled = !wifiEnabled;
  if (wifiEnabled) { WiFi.softAP("ESP32-Scope", "12345678"); server.begin(); } 
  else { WiFi.softAPdisconnect(true); }
  saveSettings();
}

void updateBrightness() {
  analogWrite(TFT_BL_PIN, brightnessLevels[brightnessIndex]);
}

void updateGenerator() {
  int f = testFreqs[testFreqIndex];
  if (f == 10) noTone(TEST_WAVE_PIN); else tone(TEST_WAVE_PIN, f); 
}

void loadSettings() {
  prefs.begin("oscin", true); 
  currentPinIndex = prefs.getInt("pin_idx", 0); 
  graphSpeed = prefs.getInt("speed", 10);
  smoothLevel = prefs.getInt("smooth_l", 2); 
  zoomIndex = prefs.getInt("zoom_idx", 0); 
  graphColorIndex = prefs.getInt("gcolor", 0);
  useGrid = prefs.getBool("grid", true);
  ledMode = prefs.getInt("lmode", 2);          
  ledColorIndex = prefs.getInt("lcolor", 0); 
  testFreqIndex = prefs.getInt("gen_idx", 4);
  wifiEnabled = prefs.getBool("wifi_en", true);
  graphStyleLine = prefs.getBool("g_style", true);
  brightnessIndex = prefs.getInt("brg_idx", 0);
  prefs.end();
}

void saveSettings() {
  prefs.begin("oscin", false); 
  prefs.putInt("pin_idx", currentPinIndex);
  prefs.putInt("speed", graphSpeed);
  prefs.putInt("smooth_l", smoothLevel);
  prefs.putInt("zoom_idx", zoomIndex);
  prefs.putInt("gcolor", graphColorIndex);
  prefs.putBool("grid", useGrid);            
  prefs.putInt("lmode", ledMode);
  prefs.putInt("lcolor", ledColorIndex);
  prefs.putInt("gen_idx", testFreqIndex);
  prefs.putBool("wifi_en", wifiEnabled);     
  prefs.putBool("g_style", graphStyleLine);  
  prefs.putInt("brg_idx", brightnessIndex);
  prefs.end();
}

// ==========================================
// УПРАВЛЕНИЕ СВЕТОДИОДОМ С ADAFRUIT
// ==========================================
void updateLED() {
  if (ledMode == 0) {
    led.setPixelColor(0, led.Color(0, 0, 0)); // Выключен
    led.show();
  } 
  else if (ledMode == 1) {
    uint8_t r = ledRGBValues[ledColorIndex][0];
    uint8_t g = ledRGBValues[ledColorIndex][1];
    uint8_t b = ledRGBValues[ledColorIndex][2];
    led.setPixelColor(0, led.Color(r, g, b)); // Статика
    led.show();
  }
}

void handleLEDAnimations() {
  if (ledMode < 2) return; 

  if (millis() - lastLedUpdate >= 20) {
    lastLedUpdate = millis();

    if (ledMode == 2) {
      // РАДУГА
      led.setPixelColor(0, ledWheel(rainbowHue));
      led.show();
      rainbowHue += 2;
    } 
    else if (ledMode == 3) {
      // SWEEP (Дыхание)
      sweepVal += sweepDirection * 4;
      if (sweepVal >= 255) { sweepVal = 255; sweepDirection = -1; }
      if (sweepVal <= 10) { sweepVal = 10; sweepDirection = 1; }

      uint8_t r = (ledRGBValues[ledColorIndex][0] * sweepVal) / 255;
      uint8_t g = (ledRGBValues[ledColorIndex][1] * sweepVal) / 255;
      uint8_t b = (ledRGBValues[ledColorIndex][2] * sweepVal) / 255;
      led.setPixelColor(0, led.Color(r, g, b));
      led.show();
    }
  }
}

// Генератор цвета Adafruit Wheel
uint32_t ledWheel(uint8_t wheelPos) {
  wheelPos = 255 - wheelPos;
  if (wheelPos < 85) {
    return led.Color(255 - wheelPos * 3, 0, wheelPos * 3);
  } else if (wheelPos < 170) {
    wheelPos -= 85;
    return led.Color(0, wheelPos * 3, 255 - wheelPos * 3);
  } else {
    wheelPos -= 170;
    return led.Color(wheelPos * 3, 255 - wheelPos * 3, 0);
  }
}

// ==========================================
// НАСТРОЙКА ИДЕАЛЬНОГО МЕНЮ 320х170
// ==========================================
void updateMenuDisplay() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("=== OSCILLOSCOPE CONFIG ===", 48, 5, 2); 

  String text = "";
  for (int i = 0; i < MENU_ITEMS_COUNT - 1; i++) {
    if (i == menuCursor) tft.setTextColor(TFT_GREEN, TFT_BLACK);
    else tft.setTextColor(TFT_WHITE, TFT_BLACK);

    switch(i) {
      case 0:  text = "CH: GPIO " + String(adcPins[currentPinIndex]); break;
      case 1:  text = "ZOOM: " + String(zoomLevels[zoomIndex]) + "x"; break;
      case 2:  text = "FILT: " + String(smoothNames[smoothLevel]); break;
      case 3:  text = "SPEED: " + String(graphSpeed) + "ms"; break;
      case 4:  text = "GRID: " + String(useGrid ? "ON" : "OFF"); break;
      case 5:  text = "STYLE: " + String(graphStyleLine ? "LINES" : "DOTS"); break;
      case 6:  text = "COLOR: " + String(colorNames[graphColorIndex]); break;
      case 7:  text = "LED M: " + String(ledModes[ledMode]); break;
      case 8:  text = "LED C: " + String(ledColorNames[ledColorIndex]); break;
      case 9:  text = "BRIGHT: " + String(brightnessNames[brightnessIndex]); break;
      case 10: text = "WI-FI SETTINGS >"; break;
    }

    int x = (i < 6) ? 10 : 165;
    int y = 30 + (i % 6) * 22; 
    tft.drawString(text, x, y, 2);
  }

  if (menuCursor == 11) tft.setTextColor(TFT_GREEN, TFT_BLACK);
  else tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.drawString("[ EXIT & SAVE ]", 10, 140, 2);
}

void updateWiFiMenuDisplay(int subCursor) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("=== WI-FI CONTROL ===", 75, 8, 2);

  if (subCursor == 0) tft.setTextColor(TFT_GREEN, TFT_BLACK);
  else tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(wifiEnabled ? "STATUS: [ NETWORK ENABLED ]" : "STATUS: [ NETWORK DISABLED ]", 15, 38, 2);

  tft.setTextColor(TFT_NAVY, TFT_BLACK); 
  tft.drawString("AP SSID:  ESP32-Scope", 15, 66, 2);
  tft.drawString("AP PASS:  12345678", 15, 88, 2);
  tft.drawString("WEB LINK: 192.168.4.1", 15, 110, 2);

  if (subCursor == 1) tft.setTextColor(TFT_GREEN, TFT_BLACK);
  else tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("[ BACK TO MENU ]", 15, 140, 2); 
}

int wifiSubCursor = 0; 

void onShortPressAction() {
  if (currentMode == 0) { isFrozen = !isFrozen; } 
  else if (currentMode == 1) { menuCursor = (menuCursor + 1) % MENU_ITEMS_COUNT; updateMenuDisplay(); } 
  else if (currentMode == 3) { wifiSubCursor = (wifiSubCursor + 1) % 2; updateWiFiMenuDisplay(wifiSubCursor); }
}

void onLongPressAction() {
  if (currentMode == 0) { currentMode = 1; menuCursor = 0; updateMenuDisplay(); } 
  else if (currentMode == 1) {
    if (menuCursor == 11) { currentMode = 0; xPos = 0; tft.fillScreen(TFT_BLACK); } 
    else if (menuCursor == 0) { currentPinIndex = (currentPinIndex + 1) % NUM_ADC_PINS; updateMenuDisplay(); }
    else if (menuCursor == 1) { zoomIndex = (zoomIndex + 1) % 4; updateMenuDisplay(); }
    else if (menuCursor == 2) { smoothLevel = (smoothLevel + 1) % 4; updateMenuDisplay(); }
    else if (menuCursor == 3) { graphSpeed = (graphSpeed + 5) % 35; updateMenuDisplay(); }
    else if (menuCursor == 4) { useGrid = !useGrid; updateMenuDisplay(); }
    else if (menuCursor == 5) { graphStyleLine = !graphStyleLine; updateMenuDisplay(); }
    else if (menuCursor == 6) { graphColorIndex = (graphColorIndex + 1) % 4; updateMenuDisplay(); }
    else if (menuCursor == 7) { ledMode = (ledMode + 1) % 4; updateLED(); updateMenuDisplay(); }
    else if (menuCursor == 8) { ledColorIndex = (ledColorIndex + 1) % 7; updateLED(); updateMenuDisplay(); }
    else if (menuCursor == 9) { brightnessIndex = (brightnessIndex + 1) % 3; updateBrightness(); updateMenuDisplay(); }
    else if (menuCursor == 10) { currentMode = 3; wifiSubCursor = 0; updateWiFiMenuDisplay(wifiSubCursor); }
    saveSettings();
  } 
  else if (currentMode == 3) {
    if (wifiSubCursor == 0) { toggleWiFi(); updateWiFiMenuDisplay(wifiSubCursor); } 
    else if (wifiSubCursor == 1) { currentMode = 1; updateMenuDisplay(); }
  }
}

// ==========================================
// ОТРИСОВКА ГРАФИКА ОССЦИЛЛОГРАФА
// ==========================================
void drawGrid() {
  int yTop = 32; int yBottom = tft.height() - 1; int h = yBottom - yTop;
  uint16_t gridColor = tft.color565(45, 45, 45); 
  for (int i = 1; i < 4; i++) tft.drawFastHLine(0, yTop + (h * i) / 4, tft.width(), gridColor);
  for (int i = 1; i < 6; i++) tft.drawFastVLine((tft.width() * i) / 6, yTop, h, gridColor);
}

void drawOscilloscope() {
  int adcValue = 0; int activePin = adcPins[currentPinIndex]; 
  int samples = (smoothLevel == 1) ? 5 : (smoothLevel == 2) ? 20 : (smoothLevel == 3) ? 50 : 1;

  if (samples > 1) { long adcTotal = 0; for (int i = 0; i < samples; i++) adcTotal += analogRead(activePin); adcValue = adcTotal / samples; } 
  else { adcValue = analogRead(activePin); }

  if (xPos % 5 == 0) { waveBuffer[waveBufferIdx] = adcValue; waveBufferIdx = (waveBufferIdx + 1) % WAVE_BUFFER_SIZE; }

  if (adcValue > currentMaxAdc) currentMaxAdc = adcValue; if (adcValue < currentMinAdc) currentMinAdc = adcValue;
  int midpoint = (lastMinAdc + lastMaxAdc) / 2;
  if (xPos > 0 && prevAdcValue < midpoint && adcValue >= midpoint) crossings++;
  prevAdcValue = adcValue; voltage = (adcValue * 3.3) / 4095.0; 

  tft.setTextColor(TFT_WHITE, TFT_BLACK); char topText[64];
  if (freqHz < 1000.0) snprintf(topText, sizeof(topText), "G%d V:%.1f| ^%.1f _%.1f| %dHz  ", activePin, voltage, vMax, vMin, (int)freqHz);
  else snprintf(topText, sizeof(topText), "G%d V:%.1f| ^%.1f _%.1f| %.1fkHz  ", activePin, voltage, vMax, vMin, freqHz / 1000.0);
  tft.drawString(topText, 4, 8, 2); 

  int yTop = 32; int yBottom = tft.height() - 1; int range = (lastMaxAdc - lastMinAdc); 
  float scale = (range > 50) ? 4095.0 / range : 1.0; 
  int zoomedAdc = (int)((adcValue - lastMinAdc) * scale) * zoomLevels[zoomIndex];
  if (zoomedAdc > 4095) zoomedAdc = 4095; if (zoomedAdc < 0) zoomedAdc = 0;
  int yPos = map(zoomedAdc, 0, 4095, yBottom, yTop); 

  if (xPos == 0) {
    unsigned long sweepDuration = millis() - sweepStartTime;
    if (sweepDuration > 0 && range > 100) freqHz = (crossings * 1000.0) / sweepDuration; else freqHz = 0.0; 
    vMax = (lastMaxAdc * 3.3) / 4095.0; vMin = (lastMinAdc * 3.3) / 4095.0;
    lastMinAdc = currentMinAdc; lastMaxAdc = currentMaxAdc; currentMinAdc = 4095; currentMaxAdc = 0; crossings = 0; sweepStartTime = millis();
    prevX = 0; prevY = yPos; tft.fillRect(0, yTop, tft.width(), tft.height() - yTop, TFT_BLACK); 
    if (useGrid) drawGrid();
  }

  if (graphStyleLine) tft.drawLine(prevX, prevY, xPos, yPos, graphColors[graphColorIndex]);
  else tft.drawPixel(xPos, yPos, graphColors[graphColorIndex]);

  prevX = xPos; prevY = yPos; xPos++; if (xPos >= tft.width()) xPos = 0; 
  if (!isFrozen && graphSpeed > 0) delay(graphSpeed);
}

void handleButton() {
  bool btnState = digitalRead(BUTTON_PIN);
  if (btnState == LOW) {
    if (!isPressing) { buttonPressTime = millis(); isPressing = true; longPressedTriggered = false; delay(20); } 
    else if (!longPressedTriggered && (millis() - buttonPressTime >= 600)) { longPressedTriggered = true; onLongPressAction(); }
  } else {
    if (isPressing) { if (!longPressedTriggered && (millis() - buttonPressTime >= 30)) onShortPressAction(); isPressing = false; delay(20); }
  }
}