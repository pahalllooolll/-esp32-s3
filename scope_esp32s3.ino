#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h> 
#include <SPI.h>
#include <TFT_eSPI.h>
#include <Adafruit_NeoPixel.h>

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite menuSpr = TFT_eSprite(&tft); 
Preferences prefs;
WebServer server(80); 
Adafruit_NeoPixel led(1, 48, NEO_GRB + NEO_KHZ800);

// --- НАСТРОЙКИ КНОПОК ---
#define BTN_UP    41
#define BTN_DOWN  40
#define BTN_LEFT  39
#define BTN_RIGHT 37

#define TEST_WAVE_PIN 42  
#define TFT_BL_PIN    38     

bool lastStateUP    = HIGH;
bool lastStateDOWN  = HIGH;
bool lastStateLEFT  = HIGH;
bool lastStateRIGHT = HIGH;

// --- НАСТРОЙКА ПИНОВ ---
const int adcPins[] = {4, 5, 9}; 
const int NUM_ADC_PINS = 3;
bool pinEnabled[NUM_ADC_PINS] = {true, true, true}; 
uint16_t pinColors[NUM_ADC_PINS] = {TFT_CYAN, TFT_GREEN, TFT_MAGENTA}; 

// --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ГРАФИКА ---
volatile float voltage = 0.0;
int xPos = 0;
int prevX = 0;
int prevYPins[NUM_ADC_PINS] = {100, 100, 100};

const int WAVE_BUFFER_SIZE = 60;
int waveBuffer[WAVE_BUFFER_SIZE];
int waveBufferIdx = 0;

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
bool wasAboveMidpoint = false;

// Параметры
int graphSpeed;
int smoothLevel;     
int zoomIndex;       
bool useGrid;
int ledMode;         
int ledColorIndex;   
int testFreqIndex;   
bool wifiEnabled = true; 
int brightnessIndex = 0;    
volatile bool generatorChanged = false;

const int zoomLevels[] = {1, 2, 4, 8};
const char* smoothNames[] = {"DISABLED", "LIGHT", "MEDIUM", "STRICT"};
const char* ledModes[] = {"DISABLED", "STATIC", "RAINBOW", "SWEEP"};
const char* ledColorNames[] = {"WHITE", "RED", "GREEN", "BLUE", "YELLOW", "CYAN", "MAGENTA"};
const int brightnessLevels[] = {255, 128, 50};
const char* brightnessNames[] = {"100%", "50%", "20%"};
const int testFreqs[] = {10, 50, 100, 500, 1000, 5000, 10000, 100000};
const char* freqNames[] = {"10 Hz", "50 Hz", "100 Hz", "500 Hz", "1 kHz", "5 kHz", "10 kHz", "100 kHz"};
const int NUM_FREQS = 8;
const uint8_t ledRGBValues[][3] = {{255,255,255},{255,0,0},{0,255,0},{0,0,255},{255,255,0},{0,255,255},{255,0,255}};

uint8_t rainbowHue = 0;
unsigned long lastLedUpdate = 0;
int sweepDirection = 1;
int sweepVal = 0;

// --- ПЕРЕМЕННЫЕ МЕНЮ И АНИМАЦИИ ---
int currentMode = 0; 
int menuCursor = 0;
int menuScrollOffset = 0; 
const int MAX_VISIBLE_ITEMS = 4; 
const int MENU_ITEMS_COUNT = 12; 

// Переменные для КИНЕТИЧЕСКОЙ анимации скролла
float currentVisualY = 2.0; 
float currentScrollY = 0.0; 
int pulseState = 0;
int pulseDir = 1;

int pinsSubCursor = 0;
int wifiSubCursor = 0;

// ТАЙМЕР ХИНКАЛИ
int khinkaliTimerSec = 420; 
bool khinkaliRunning = false;
unsigned long lastKhinkaliSecTick = 0;
int steamAnimFrame = 0;

void loadSettings();
void saveSettings();
void handleButtons();
void drawOscilloscope();
void drawGrid();
void updateLED();
void handleLEDAnimations();
uint32_t ledWheel(uint8_t wheelPos);
void initMenuScreen();
void updateMenuDisplay();
void updatePinsMenuDisplay();
void updateWiFiMenuDisplay();
void updateGenerator();
void toggleWiFi();
void updateBrightness();
void drawKhinkaliScreen();

// --- ВЕБ ИНТЕРФЕЙС ---
const char PAGE_MAIN[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>Quantum Scope UI</title>
<style>
:root { --bg: #05050a; --card: #0d0d1f; --accent: #00ffcc; --magenta: #ff007f; --text: #e0e0ff; }
body { background: var(--bg); color: var(--text); font-family: 'Segoe UI', sans-serif; margin: 0; padding: 20px; display: flex; flex-direction: column; align-items: center; }
h2 { color: var(--accent); text-shadow: 0 0 10px rgba(0,255,204,0.5); font-weight: 400; letter-spacing: 2px; }
#waveCanvas { background: #020205; border: 2px solid #1a1a3a; border-radius: 16px; box-shadow: 0 0 20px rgba(0,255,204,0.15); width: 100%; max-width: 550px; }
.grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 15px; width: 100%; max-width: 550px; margin: 20px 0; }
.card { background: var(--card); padding: 15px; border-radius: 12px; text-align: center; border: 1px solid #1a1a3a; box-shadow: inset 0 0 10px rgba(255,255,255,0.02); }
.card .label { font-size: 11px; color: #62628a; text-transform: uppercase; letter-spacing: 1px; }
.card .val { font-size: 22px; font-weight: bold; margin-top: 6px; font-family: monospace; }
.wifi-info { margin-top: 15px; font-size: 13px; color: #555577; text-align: center; border-top: 1px solid #111; padding-top: 10px; }
</style></head><body>
<h2>⚡ QUANTUM SCOPE OS ⚡</h2>
<canvas id="waveCanvas" width="550" height="240"></canvas>
<div class='grid'>
<div class='card'><div class='label'>⚡ Amplitude</div><div class='val' id='v' style='color:var(--accent)'>-- V</div></div>
<div class='card'><div class='label'>🔄 Frequency</div><div class='val' id='freq' style='color:var(--magenta)'>-- Hz</div></div>
</div>
<div class='wifi-info'>Connected to AP: ESP32-Scope | Control Center v2.5</div>
<script>
let canvas = document.getElementById('waveCanvas'); let ctx = canvas.getContext('2d');
setInterval(async () => {
try {
let r = await fetch('/data'); let j = await r.json();
document.getElementById('v').innerText = j.v.toFixed(2) + ' V';
document.getElementById('freq').innerText = j.freq >= 1000 ? (j.freq/1000).toFixed(2) + ' kHz' : j.freq.toFixed(0) + ' Hz';
ctx.clearRect(0, 0, canvas.width, canvas.height);
ctx.strokeStyle = '#0a0a1f'; ctx.lineWidth = 1; ctx.beginPath();
for(let m=1; m<4; m++) { ctx.moveTo(0, canvas.height*m/4); ctx.lineTo(canvas.width, canvas.height*m/4); } ctx.stroke();
ctx.strokeStyle = '#00ffcc'; ctx.lineWidth = 2.5; ctx.shadowBlur = 8; ctx.shadowColor = '#00ffcc'; ctx.beginPath();
for(let i=0; i<j.wave.length; i++) {
let x = (i / (j.wave.length-1)) * canvas.width; let y = canvas.height - ((j.wave[i] / 4095) * canvas.height);
if(i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
} ctx.stroke(); ctx.shadowBlur = 0;
}catch(e){} }, 150);
</script></body></html>)rawliteral";

void handleRoot() { server.send_P(200, "text/html", PAGE_MAIN); }
void handleData() {
  String json = "{\"v\":" + String(voltage) + ",\"freq\":" + String(freqHz) + ",\"wave\":[";
  for (int i = 0; i < WAVE_BUFFER_SIZE; i++) { json += String(waveBuffer[i]); if (i < WAVE_BUFFER_SIZE - 1) json += ","; }
  json += "]}"; server.send(200, "application/json", json);
}

void core0Task(void * pvParameters) {
  if (wifiEnabled) { 
    WiFi.softAP("ESP32-Scope", "12345678"); 
    server.on("/", handleRoot); 
    server.on("/data", handleData); 
    server.begin(); 
    Serial.println("[WIFI] Access Point & Web Server Started successfully.");
  }
  for (;;) { if (wifiEnabled) server.handleClient(); vTaskDelay(5 / portTICK_PERIOD_MS); }
}

void setup() {
  // Инициализация последовательного порта для вывода отладки (Serial port)
  Serial.begin(115200);
  delay(500); // Небольшая задержка для стабилизации Serial-соединения
  Serial.println("\n--- QUANTUM SCOPE OS INITIALIZATION ---");

  pinMode(BTN_UP, INPUT_PULLUP); pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP); pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(TFT_BL_PIN, OUTPUT); pinMode(TEST_WAVE_PIN, OUTPUT); 
  
  loadSettings();
  led.begin(); led.setBrightness(50); updateBrightness();
  
  xTaskCreatePinnedToCore(core0Task, "Core0Task", 8192, NULL, 1, NULL, 0);
  updateGenerator(); 
  
  tft.init(); tft.setRotation(1); tft.fillScreen(TFT_BLACK);
  menuSpr.createSprite(tft.width(), 106);
  
  sweepStartTime = millis(); updateLED(); 
  Serial.println("[SYSTEM] Setup Complete. Entering Main Loop.");
}

void loop() {
  handleButtons(); 
  handleLEDAnimations(); 

  if (generatorChanged) { 
    generatorChanged = false; 
    updateGenerator(); 
  }
  
  if (currentMode == 0) {
    if (!isFrozen) drawOscilloscope();
    if (graphSpeed == 0) delayMicroseconds(10); 
  } else {
    if (currentMode == 1) {
      updateMenuDisplay(); 
    } else if (currentMode == 5) {
      drawKhinkaliScreen();
    }
    delay(16); 
  }
}

void initMenuScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("⚡ QUANTUM CONFIG ⚡", 50, 5, 2); 
  tft.drawFastHLine(0, 24, tft.width(), tft.color565(40, 40, 80));
  tft.drawFastHLine(0, 132, tft.width(), tft.color565(40, 40, 80));
}

void updateMenuDisplay() {
  if (menuCursor < menuScrollOffset) {
    menuScrollOffset = menuCursor;
  } else if (menuCursor >= menuScrollOffset + MAX_VISIBLE_ITEMS) {
    menuScrollOffset = menuCursor - MAX_VISIBLE_ITEMS + 1;
  }

  float targetScrollY = menuScrollOffset * 26.0;
  currentScrollY += (targetScrollY - currentScrollY) * 0.20; 

  float targetVisualY = (menuCursor * 26.0) - currentScrollY + 2.0;
  currentVisualY += (targetVisualY - currentVisualY) * 0.30; 

  pulseState += pulseDir * 4;
  if (pulseState >= 50 || pulseState <= 0) pulseDir = -pulseDir;

  menuSpr.fillSprite(TFT_BLACK);

  uint16_t sliderColor = menuSpr.color565(40 + pulseState, 40 + pulseState, 90 + pulseState / 2);
  menuSpr.fillRect(4, (int)currentVisualY, menuSpr.width() - 8, 22, sliderColor);

  for (int i = 0; i < MENU_ITEMS_COUNT; i++) {
    float yTextPos = 5.0 + (i * 26.0) - currentScrollY;

    if (yTextPos < -20 || yTextPos > 110) continue; 

    if (i == menuCursor) {
      menuSpr.setTextColor(TFT_GREEN, sliderColor);
    } else {
      menuSpr.setTextColor(TFT_WHITE, TFT_BLACK);
    }

    String itemText = "";
    switch(i) {
      case 0:  itemText = "-> CHANNELS CONFIG"; break;
      case 1:  itemText = "ZOOM: " + String(zoomLevels[zoomIndex]) + "x"; break;
      case 2:  itemText = "FILT: " + String(smoothNames[smoothLevel]); break;
      case 3:  itemText = "SPEED: " + String(graphSpeed) + "ms"; break;
      case 4:  itemText = "GRID: " + String(useGrid ? "ON" : "OFF"); break;
      case 5:  itemText = "GEN: " + String(freqNames[testFreqIndex]); break; 
      case 6:  itemText = "LED M: " + String(ledModes[ledMode]); break;
      case 7:  itemText = "LED C: " + String(ledColorNames[ledColorIndex]); break;
      case 8:  itemText = "BRIGHT: " + String(brightnessNames[brightnessIndex]); break;
      case 9:  itemText = "-> WI-FI SETTINGS"; break;
      case 10: itemText = "🥟 KHINKALI COOKER"; break;
      case 11: itemText = "[ SAVE & EXIT ]"; break;
    }
    menuSpr.drawString(itemText, 15, (int)yTextPos, 2);
  }

  if (currentScrollY > 5.0) menuSpr.drawString("^", menuSpr.width() - 15, 5, 2);
  if (currentScrollY < (MENU_ITEMS_COUNT - MAX_VISIBLE_ITEMS) * 26.0 - 5.0) menuSpr.drawString("v", menuSpr.width() - 15, 85, 2);

  menuSpr.pushSprite(0, 25);
}

void updateWiFiMenuDisplay() {
  tft.fillScreen(TFT_BLACK); 
  tft.setTextColor(TFT_CYAN, TFT_BLACK); 
  tft.drawString("=== WI-FI CONTROL ===", 55, 8, 2);
  tft.drawFastHLine(0, 26, tft.width(), TFT_CYAN);

  if (wifiSubCursor == 0) { tft.fillRect(4, 38, tft.width()-8, 22, tft.color565(0,60,60)); tft.setTextColor(TFT_GREEN, tft.color565(0,60,60)); }
  else tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(wifiEnabled ? "MODEM: ACTIVE" : "MODEM: DISABLED", 12, 41, 2);
  
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("SSID: ESP32-Scope", 12, 68, 2);
  tft.drawString("PASS: 12345678", 12, 86, 2);
  tft.setTextColor(tft.color565(0, 255, 150), TFT_BLACK);
  tft.drawString("IP:   192.168.4.1", 12, 104, 2);

  if (wifiSubCursor == 1) { tft.fillRect(4, 132, tft.width()-8, 22, tft.color565(40,40,40)); tft.setTextColor(TFT_GREEN, tft.color565(40,40,40)); }
  else tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("[ BACK TO MENU ]", 12, 135, 2);
}

void drawKhinkaliScreen() {
  if (khinkaliRunning && millis() - lastKhinkaliSecTick >= 1000) {
    lastKhinkaliSecTick = millis();
    if (khinkaliTimerSec > 0) {
      khinkaliTimerSec--;
      steamAnimFrame = (steamAnimFrame + 1) % 3;
      
      // Логируем в Serial каждую секунду варки
      Serial.printf("[COOKER] Khinkali active. Time left: %02d:%02d\n", khinkaliTimerSec/60, khinkaliTimerSec%60);
    }
  }

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawString("🥟 KHINKALI COOKING PROFILE 🥟", 15, 8, 2);
  tft.drawFastHLine(0, 26, tft.width(), TFT_YELLOW);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  if (khinkaliRunning && khinkaliTimerSec > 0) {
    if (steamAnimFrame == 0) tft.drawString("  ~   ~   ~  ", 100, 42, 2);
    else if (steamAnimFrame == 1) tft.drawString("   ~   ~   ~ ", 100, 42, 2);
    else tft.drawString(" ~   ~   ~   ", 100, 42, 2);
  }

  tft.setTextColor(tft.color565(200, 180, 140), TFT_BLACK);
  tft.drawString("    ( ( 🥟 ) )    ", 80, 60, 2);
  tft.setTextColor(TFT_BLUE, TFT_BLACK);
  tft.drawString("[~~~~~~~~~~~~~~~]", 80, 78, 2); 

  int mins = khinkaliTimerSec / 60;
  int secs = khinkaliTimerSec % 60;
  char timeStr[16];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", mins, secs);
  
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("TIME REMAINING:", 25, 102, 2);
  
  if (khinkaliTimerSec == 0 && khinkaliRunning) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("!! READY TO EAT !!", 150, 102, 2);
    led.setPixelColor(0, led.Color(255, 0, 0)); led.show();
  } else {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString(timeStr, 160, 102, 2);
  }

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("<- / -> : +/- Time  |  UP : START/PAUSE  |  DOWN : EXIT", 5, 145, 1);
}

void handleButtons() {
  bool rUP    = digitalRead(BTN_UP);
  bool rDOWN  = digitalRead(BTN_DOWN);
  bool rLEFT  = digitalRead(BTN_LEFT);
  bool rRIGHT = digitalRead(BTN_RIGHT);

  if (rUP == LOW && lastStateUP == HIGH) {
    delay(30); 
    if (currentMode == 0) { 
      currentMode = 1; menuCursor = 0; menuScrollOffset = 0; initMenuScreen(); 
      Serial.println("[NAV] Entered Configuration Menu.");
    }
    else if (currentMode == 1) { menuCursor--; if (menuCursor < 0) menuCursor = MENU_ITEMS_COUNT - 1; }
    else if (currentMode == 4) { pinsSubCursor--; if (pinsSubCursor < 0) pinsSubCursor = 3; updatePinsMenuDisplay(); }
    else if (currentMode == 3) { wifiSubCursor--; if (wifiSubCursor < 0) wifiSubCursor = 1; updateWiFiMenuDisplay(); }
    else if (currentMode == 5) { khinkaliRunning = !khinkaliRunning; if (khinkaliRunning) lastKhinkaliSecTick = millis(); Serial.printf("[COOKER] Timer State Changed: Running = %d\n", khinkaliRunning); }
  }
  lastStateUP = rUP;

  if (rDOWN == LOW && lastStateDOWN == HIGH) {
    delay(30);
    if (currentMode == 0) { 
      currentMode = 1; menuCursor = 0; menuScrollOffset = 0; initMenuScreen(); 
      Serial.println("[NAV] Entered Configuration Menu.");
    }
    else if (currentMode == 1) { menuCursor++; if (menuCursor >= MENU_ITEMS_COUNT) menuCursor = 0; }
    else if (currentMode == 4) { pinsSubCursor = (pinsSubCursor + 1) % 4; updatePinsMenuDisplay(); }
    else if (currentMode == 3) { wifiSubCursor = (wifiSubCursor + 1) % 2; updateWiFiMenuDisplay(); }
    else if (currentMode == 5) { currentMode = 1; khinkaliRunning = false; initMenuScreen(); Serial.println("[NAV] Exited Cooker Menu."); } 
  }
  lastStateDOWN = rDOWN;

  if (rLEFT == LOW && lastStateLEFT == HIGH) {
    delay(30);
    if (currentMode == 0) { graphSpeed -= 5; if (graphSpeed < 0) graphSpeed = 0; Serial.printf("[OSC] Changed Sweep Speed: %d ms\n", graphSpeed); } 
    else if (currentMode == 1) {
      if (menuCursor == 1) { zoomIndex = (zoomIndex - 1 + 4) % 4; Serial.printf("[CONFIG] Zoom Set to: %dx\n", zoomLevels[zoomIndex]); }
      else if (menuCursor == 2) { smoothLevel = (smoothLevel - 1 + 4) % 4; Serial.printf("[CONFIG] Filter Set to: %s\n", smoothNames[smoothLevel]); }
      else if (menuCursor == 3) { graphSpeed -= 5; if (graphSpeed < 0) graphSpeed = 30; Serial.printf("[CONFIG] Speed Set to: %d ms\n", graphSpeed); }
      else if (menuCursor == 4) { useGrid = !useGrid; Serial.printf("[CONFIG] Grid State: %d\n", useGrid); }
      else if (menuCursor == 5) { testFreqIndex = (testFreqIndex - 1 + NUM_FREQS) % NUM_FREQS; generatorChanged = true; }
      else if (menuCursor == 6) { ledMode = (ledMode - 1 + 4) % 4; updateLED(); Serial.printf("[CONFIG] LED Mode Set to: %s\n", ledModes[ledMode]); }
      else if (menuCursor == 7) { ledColorIndex = (ledColorIndex - 1 + 7) % 7; updateLED(); Serial.printf("[CONFIG] LED Color Set to: %s\n", ledColorNames[ledColorIndex]); }
      else if (menuCursor == 8) { brightnessIndex = (brightnessIndex - 1 + 3) % 3; updateBrightness(); Serial.printf("[CONFIG] Screen Brightness: %s\n", brightnessNames[brightnessIndex]); }
      else if (menuCursor == 11) { currentMode = 0; tft.fillScreen(TFT_BLACK); xPos = 0; Serial.println("[NAV] Saved and Returned to Oscilloscope Mode."); }
      saveSettings();
    }
    else if (currentMode == 4) { if (pinsSubCursor < 3) { pinEnabled[pinsSubCursor] = !pinEnabled[pinsSubCursor]; saveSettings(); updatePinsMenuDisplay(); Serial.printf("[CHANNELS] Toggled Channel %d State: %d\n", pinsSubCursor, pinEnabled[pinsSubCursor]); } else { currentMode = 1; initMenuScreen(); } }
    else if (currentMode == 3) { if (wifiSubCursor == 0) { toggleWiFi(); updateWiFiMenuDisplay(); } else { currentMode = 1; initMenuScreen(); } }
    else if (currentMode == 5) { khinkaliTimerSec -= 30; if (khinkaliTimerSec < 30) khinkaliTimerSec = 30; } 
  }
  lastStateLEFT = rLEFT;

  if (rRIGHT == LOW && lastStateRIGHT == HIGH) {
    delay(30);
    if (currentMode == 0) { graphSpeed += 5; if (graphSpeed > 30) graphSpeed = 30; Serial.printf("[OSC] Changed Sweep Speed: %d ms\n", graphSpeed); }
    else if (currentMode == 1) {
      if (menuCursor == 0) { currentMode = 4; pinsSubCursor = 0; updatePinsMenuDisplay(); Serial.println("[NAV] Opened Channels Menu."); return; }
      if (menuCursor == 9) { currentMode = 3; wifiSubCursor = 0; updateWiFiMenuDisplay(); Serial.println("[NAV] Opened WiFi Control Menu."); return; }
      if (menuCursor == 10) { currentMode = 5; khinkaliTimerSec = 420; khinkaliRunning = false; Serial.println("[NAV] Opened Khinkali Cooker Profile."); return; } 
      
      if (menuCursor == 1) { zoomIndex = (zoomIndex + 1) % 4; Serial.printf("[CONFIG] Zoom Set to: %dx\n", zoomLevels[zoomIndex]); }
      else if (menuCursor == 2) { smoothLevel = (smoothLevel + 1) % 4; Serial.printf("[CONFIG] Filter Set to: %s\n", smoothNames[smoothLevel]); }
      else if (menuCursor == 3) { graphSpeed = (graphSpeed + 5) % 35; Serial.printf("[CONFIG] Speed Set to: %d ms\n", graphSpeed); }
      else if (menuCursor == 4) { useGrid = !useGrid; Serial.printf("[CONFIG] Grid State: %d\n", useGrid); }
      else if (menuCursor == 5) { testFreqIndex = (testFreqIndex + 1) % NUM_FREQS; generatorChanged = true; }
      else if (menuCursor == 6) { ledMode = (ledMode + 1) % 4; updateLED(); Serial.printf("[CONFIG] LED Mode Set to: %s\n", ledModes[ledMode]); }
      else if (menuCursor == 7) { ledColorIndex = (ledColorIndex + 1) % 7; updateLED(); Serial.printf("[CONFIG] LED Color Set to: %s\n", ledColorNames[ledColorIndex]); }
      else if (menuCursor == 8) { brightnessIndex = (brightnessIndex + 1) % 3; updateBrightness(); Serial.printf("[CONFIG] Screen Brightness: %s\n", brightnessNames[brightnessIndex]); }
      else if (menuCursor == 11) { currentMode = 0; tft.fillScreen(TFT_BLACK); xPos = 0; Serial.println("[NAV] Saved and Returned to Oscilloscope Mode."); }
      saveSettings();
    }
    else if (currentMode == 4) { if (pinsSubCursor < 3) { pinEnabled[pinsSubCursor] = !pinEnabled[pinsSubCursor]; saveSettings(); updatePinsMenuDisplay(); Serial.printf("[CHANNELS] Toggled Channel %d State: %d\n", pinsSubCursor, pinEnabled[pinsSubCursor]); } else { currentMode = 1; initMenuScreen(); } }
    else if (currentMode == 3) { if (wifiSubCursor == 0) { toggleWiFi(); updateWiFiMenuDisplay(); } else { currentMode = 1; initMenuScreen(); } }
    else if (currentMode == 5) { khinkaliTimerSec += 30; } 
  }
  lastStateRIGHT = rRIGHT;
}

void updatePinsMenuDisplay() {
  tft.fillScreen(TFT_BLACK); tft.setTextColor(TFT_CYAN, TFT_BLACK); tft.drawString("=== CHANNELS ===", 80, 8, 2);
  for (int i = 0; i < NUM_ADC_PINS; i++) {
    if (pinsSubCursor == i) { tft.fillRect(0, 38 + i*25 - 2, tft.width(), 22, tft.color565(30,30,50)); tft.setTextColor(TFT_GREEN, tft.color565(30,30,50)); }
    else tft.setTextColor(TFT_WHITE, TFT_BLACK);
    String lbl = (i==0)?"CH4 (GPIO4) [CYAN]":(i==1)?"CH5 (GPIO5) [GREEN]":"CH9 (GPIO9) [MAGENTA]";
    tft.drawString(lbl + (pinEnabled[i] ? ": ON" : ": OFF"), 10, 38 + i * 25, 2);
  }
  if (pinsSubCursor == 3) { tft.fillRect(0, 113 - 2, tft.width(), 22, tft.color565(30,30,50)); tft.setTextColor(TFT_GREEN, tft.color565(30,30,50)); }
  else tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("[ BACK ]", 10, 113, 2);
}
void toggleWiFi() { 
  wifiEnabled = !wifiEnabled; 
  if (wifiEnabled) { 
    WiFi.softAP("ESP32-Scope", "12345678"); 
    server.begin(); 
    Serial.println("[WIFI] Modem Enabled.");
  } else { 
    WiFi.softAPdisconnect(true); 
    Serial.println("[WIFI] Modem Disabled.");
  } 
  saveSettings(); 
}
void updateBrightness() { analogWrite(TFT_BL_PIN, brightnessLevels[brightnessIndex]); }
void updateGenerator() {
  uint32_t freq = testFreqs[testFreqIndex]; uint8_t bits = (freq <= 100) ? 14 : (freq <= 10000) ? 10 : 8; uint32_t duty = (1 << bits) / 2; 
  #if ESP_IDF_VERSION_MAJOR >= 5
    ledcDetach(TEST_WAVE_PIN); delay(2); ledcAttach(TEST_WAVE_PIN, freq, bits); ledcWrite(TEST_WAVE_PIN, duty);
  #else
    ledcSetup(LEDC_CHANNEL, freq, bits); ledcAttachPin(TEST_WAVE_PIN, LEDC_CHANNEL); ledcWrite(LEDC_CHANNEL, duty); 
  #endif
  Serial.printf("[GENERATOR] Frequency Updated to: %s\n", freqNames[testFreqIndex]);
}
void loadSettings() {
  prefs.begin("oscin", true); pinEnabled[0] = prefs.getBool("p0_en", true); pinEnabled[1] = prefs.getBool("p1_en", true); pinEnabled[2] = prefs.getBool("p2_en", true);
  graphSpeed = prefs.getInt("speed", 10); smoothLevel = prefs.getInt("smooth_l", 2); zoomIndex = prefs.getInt("zoom_idx", 0); useGrid = prefs.getBool("grid", true);
  ledMode = prefs.getInt("lmode", 2); ledColorIndex = prefs.getInt("lcolor", 0); testFreqIndex = prefs.getInt("gen_idx", 2); wifiEnabled = prefs.getBool("wifi_en", true);
  brightnessIndex = prefs.getInt("brg_idx", 0); prefs.end();
  Serial.println("[SYSTEM] Settings loaded from EEPROM Flash memory.");
}
void saveSettings() {
  prefs.begin("oscin", false); prefs.putBool("p0_en", pinEnabled[0]); prefs.putBool("p1_en", pinEnabled[1]); prefs.putBool("p2_en", pinEnabled[2]);
  prefs.putInt("speed", graphSpeed); prefs.putInt("smooth_l", smoothLevel); prefs.putInt("zoom_idx", zoomIndex); prefs.putBool("grid", useGrid);            
  prefs.putInt("lmode", ledMode); prefs.putInt("lcolor", ledColorIndex); prefs.putInt("gen_idx", testFreqIndex); prefs.putBool("wifi_en", wifiEnabled);     
  prefs.putInt("brg_idx", brightnessIndex); prefs.end();
  Serial.println("[SYSTEM] Settings successfully saved to Flash NVS.");
}
void updateLED() { if (ledMode == 0) { led.setPixelColor(0, 0); led.show(); } else if (ledMode == 1) { led.setPixelColor(0, led.Color(ledRGBValues[ledColorIndex][0], ledRGBValues[ledColorIndex][1], ledRGBValues[ledColorIndex][2])); led.show(); } }
void handleLEDAnimations() {
  if (ledMode < 2) return; 
  if (millis() - lastLedUpdate >= 20) {
    lastLedUpdate = millis();
    if (ledMode == 2) { led.setPixelColor(0, ledWheel(rainbowHue)); led.show(); rainbowHue += 2; } 
    else if (ledMode == 3) {
      sweepVal += sweepDirection * 4; if (sweepVal >= 255) { sweepVal = 255; sweepDirection = -1; } if (sweepVal <= 10) { sweepVal = 10; sweepDirection = 1; }
      led.setPixelColor(0, led.Color((ledRGBValues[ledColorIndex][0]*sweepVal)/255, (ledRGBValues[ledColorIndex][1]*sweepVal)/255, (ledRGBValues[ledColorIndex][2]*sweepVal)/255)); led.show();
    }
  }
}
uint32_t ledWheel(uint8_t wheelPos) { wheelPos = 255 - wheelPos; if (wheelPos < 85) return led.Color(255 - wheelPos * 3, 0, wheelPos * 3); else if (wheelPos < 170) { wheelPos -= 85; return led.Color(0, wheelPos * 3, 255 - wheelPos * 3); } else { wheelPos -= 170; return led.Color(wheelPos * 3, 255 - wheelPos * 3, 0); } }
void drawGrid() { int yTop = 32; int yBottom = tft.height() - 1; int h = yBottom - yTop; uint16_t gridColor = tft.color565(40, 40, 50); for (int i = 1; i < 4; i++) tft.drawFastHLine(0, yTop + (h * i) / 4, tft.width(), gridColor); for (int i = 1; i < 6; i++) tft.drawFastVLine((tft.width() * i) / 6, yTop, h, gridColor); }

void drawOscilloscope() {
  int yTop = 32; int yBottom = tft.height() - 1; 
  bool anyActive = false; for (int i = 0; i < NUM_ADC_PINS; i++) if (pinEnabled[i]) anyActive = true;
  if (!anyActive) {
    if (xPos == 0) { tft.fillRect(0, yTop, tft.width(), tft.height() - yTop, TFT_BLACK); if (useGrid) drawGrid(); tft.setTextColor(TFT_RED, TFT_BLACK); tft.drawString("NO ACTIVE CH", 70, yTop + 40, 2); }
    xPos++; if (xPos >= tft.width()) xPos = 0; if (graphSpeed > 0) delay(graphSpeed); return;
  }
  int primaryIdx = 0; for (int i = 0; i < NUM_ADC_PINS; i++) { if (pinEnabled[i]) { primaryIdx = i; break; } }
  if (xPos == 0) {
    unsigned long sweepDuration = millis() - sweepStartTime;
    if (sweepDuration > 0 && (lastMaxAdc - lastMinAdc) > 250 && crossings > 0) freqHz = (crossings * 1000.0) / sweepDuration; else freqHz = 0.0;
    vMax = (lastMaxAdc * 3.3) / 4095.0; vMin = (lastMinAdc * 3.3) / 4095.0;
    lastMinAdc = currentMinAdc; lastMaxAdc = currentMaxAdc; currentMinAdc = 4095; currentMaxAdc = 0; crossings = 0; sweepStartTime = millis(); prevX = 0; 
    tft.fillRect(0, yTop, tft.width(), tft.height() - yTop, TFT_BLACK); if (useGrid) drawGrid();
  }
  for (int i = 0; i < NUM_ADC_PINS; i++) {
    if (!pinEnabled[i]) continue;
    int adcValue = 0; int samples = (smoothLevel == 1) ? 5 : (smoothLevel == 2) ? 20 : (smoothLevel == 3) ? 50 : 1;
    if (samples > 1) { long adcTotal = 0; for (int s = 0; s < samples; s++) adcTotal += analogRead(adcPins[i]); adcValue = adcTotal / samples; } else { adcValue = analogRead(adcPins[i]); }
    if (i == primaryIdx) {
      waveBuffer[waveBufferIdx] = adcValue; if (xPos == 0) waveBufferIdx = (waveBufferIdx + 1) % WAVE_BUFFER_SIZE;
      if (adcValue > currentMaxAdc) currentMaxAdc = adcValue; if (adcValue < currentMinAdc) currentMinAdc = adcValue;
      int range = (lastMaxAdc - lastMinAdc); int midpoint = (lastMinAdc + lastMaxAdc) / 2; int hysteresis = (range / 8 < 40) ? 40 : range / 8;
      if (range > 250) { if (!wasAboveMidpoint && (adcValue > (midpoint + hysteresis))) { crossings++; wasAboveMidpoint = true; } else if (wasAboveMidpoint && (adcValue < (midpoint - hysteresis))) { wasAboveMidpoint = false; } }
      voltage = (adcValue * 3.3) / 4095.0;
    }
    int range = (lastMaxAdc - lastMinAdc); float scale = (range > 50) ? 4095.0 / range : 1.0; 
    int zoomedAdc = (int)((adcValue - lastMinAdc) * scale) * zoomLevels[zoomIndex];
    if (zoomedAdc > 4095) zoomedAdc = 4095; if (zoomedAdc < 0) zoomedAdc = 0;
    int yPos = map(zoomedAdc, 0, 4095, yBottom, yTop); if (xPos == 0) prevYPins[i] = yPos;
    tft.drawLine(prevX, prevYPins[i], xPos, yPos, pinColors[i]); prevYPins[i] = yPos;
  }
  tft.setTextColor(TFT_WHITE, TFT_BLACK); char topText[64];
  if (freqHz < 1000.0) snprintf(topText, sizeof(topText), "MIX V:%.1f| ^%.1f _%.1f| %dHz    ", voltage, vMax, vMin, (int)freqHz);
  else snprintf(topText, sizeof(topText), "MIX V:%.1f| ^%.1f _%.1f| %.1fkHz    ", voltage, vMax, vMin, freqHz / 1000.0);
  tft.drawString(topText, 4, 8, 2);
  prevX = xPos; xPos++; if (xPos >= tft.width()) xPos = 0; if (graphSpeed > 0) delay(graphSpeed);
}