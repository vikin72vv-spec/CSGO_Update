#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_NeoPixel.h>
#include <Keypad.h>
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// --- WIFI, WEB, OTA и ОБНОВЛЕНИЯ ---
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WebServer.h> 
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>

// --- NFC ---
#include <PN5180ISO14443.h> 

// ==========================================
// 🔥 ВЕРСИЯ ПРОШИВКИ 🔥
// ==========================================
#define CURRENT_VERSION 36 

// Ссылки на твой GitHub
const char* ver_url = "https://raw.githubusercontent.com/vikin72vv-spec/CSGO_Update/refs/heads/main/version.txt";
const char* bin_url = "https://raw.githubusercontent.com/vikin72vv-spec/CSGO_Update/refs/heads/main/firmware.bin";

// ==========================================
// 🔥 НАСТРОЙКИ ЗВУКОВ 🔥
// ==========================================
#define SND_BOOT    1  
#define SND_TICK    2  
#define SND_WIN     3  
#define SND_LOSE    4  
#define SND_ERROR   5  
#define SND_ARMED   6  
#define SND_SIREN   7  
#define SND_HACK    8  

// ==========================================
// 🔥 НАСТРОЙКИ WI-FI 🔥
// ==========================================
const char* router_ssid = "Keenetic";   
const char* router_pass = "8tU8c4Z3";   
const char* ap_ssid = "CSGO_BOMB";   
const char* ap_pass = "12345678";     

// --- ЭКРАН ---
#define ST77XX_GRAY   0x8410 
#define ST77XX_ORANGE 0xFA60
#define MATRIX_GREEN  0x07E0 
#define DARK_GREEN    0x03E0 

// --- ПИНЫ ---
#define TFT_RST    -1  
#define TFT_CS     5
#define TFT_DC     2
#define TFT_MOSI   1   
#define TFT_SCLK   4  
#define TFT_BL     3   
#define LED_PIN    16 
#define LED_COUNT  5 
#define NFC_NSS    33  
#define NFC_BUSY   34
#define NFC_RST    13
#define MP3_TX_PIN 17 
#define MP3_RX_PIN 35 
#define BAT_PIN    36  

// --- ОБЪЕКТЫ ---
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
PN5180ISO14443 nfc(NFC_NSS, NFC_BUSY, NFC_RST); 
Adafruit_MPU6050 mpu;
HardwareSerial mp3Serial(1); 
DFRobotDFPlayerMini myDFPlayer;
WebServer server(80); 

const byte ROWS = 4; const byte COLS = 3; 
char keys[ROWS][COLS] = {{'1','2','3'},{'4','5','6'},{'7','8','9'},{'*','0','#'}};
byte rowPins[ROWS] = {14, 32, 15, 26}; byte colPins[COLS] = {27, 12, 25}; 
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// --- ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ---
enum State { 
  BOOT, MENU, SETUP_TIME, 
  GAME_MOTION, GAME_NFC, GAME_SAFE, GAME_WIRES, GAME_SNIPER, GAME_READER, 
  GAME_DOM_SETUP, GAME_DOMINATION, 
  GAME_LOCK, 
  SCREENSAVER, WIN, LOSE 
};
State currentState = BOOT;
int selectedGame = 0; 

String inputCode = "";      
String correctCode = "7355"; 
String masterCode = "2222"; 

unsigned long gameTimer = 0;    
long timeRemaining = 0;         
unsigned long totalTime = 0; 
bool sirenActive = false;    
unsigned long lastLedBlink = 0;
bool ledState = false;
unsigned long lastNfcCheck = 0;
float lastX, lastY, lastZ;

int safeStage = 0; int targetTilt = 0; bool lockPicked = false; 
int wires[4]; bool wiresCut[4]; int wrongWiresCut = 0; 
String webMessage = ""; unsigned long messageTimer = 0;
unsigned long lastActivityTime = 0; 

// РАДАР
int radarAngle = 0;
struct Blip { int x; int y; int life; };
Blip blips[3]; 

// DOMINATION
uint8_t redTeamUID[8]; uint8_t redLen = 0;
uint8_t blueTeamUID[8]; uint8_t blueLen = 0;
unsigned long timeRed = 0; unsigned long timeBlue = 0; int domOwner = 0; 

// LOCK MODE VARIABLES
uint8_t lockKeyUID[8]; uint8_t lockKeyLen = 0; 
uint8_t lastScannedUID[8]; uint8_t lastScannedLen = 0; 
uint8_t sessionKeyUID[8]; uint8_t sessionKeyLen = 0; 
bool lockModeSetup = false; 
unsigned long lockAlarmTimer = 0; 
bool lockAlarmActive = false; 

// ===================== WEB HTML =====================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <meta charset="utf-8">
  <style>
    body { background-color: #001100; color: #00FF00; font-family: 'Courier New', monospace; text-align: center; margin: 0; padding: 20px;
      background-image: linear-gradient(rgba(0, 50, 0, 0.5) 1px, transparent 1px), linear-gradient(90deg, rgba(0, 50, 0, 0.5) 1px, transparent 1px);
      background-size: 20px 20px; box-shadow: inset 0 0 150px #000000; min-height: 100vh; }
    h1 { text-shadow: 0 0 10px #00FF00; border: 2px solid #00FF00; display: inline-block; padding: 10px 20px; background: rgba(0,20,0,0.8); }
    .card { background: rgba(0, 10, 0, 0.8); padding: 20px; margin: 20px auto; border: 1px solid #00FF00; box-shadow: 0 0 15px rgba(0, 255, 0, 0.2); max-width: 400px; }
    .btn { background-color: #000; border: 1px solid #00FF00; color: #00FF00; padding: 15px 30px; font-weight: bold; font-size: 18px; margin: 10px; cursor: pointer; text-transform: uppercase; }
    .btn:active { background-color: #00FF00; color: #000; box-shadow: 0 0 20px #00FF00; }
    .btn-red { border-color: #FF0000; color: #FF0000; } .btn-red:active { background-color: #FF0000; color: #000; box-shadow: 0 0 20px #FF0000; }
    input { padding: 10px; background: #000; border: 1px solid #00FF00; color: #00FF00; width: 70%; font-size: 16px; }
    #timer { font-size: 70px; text-shadow: 0 0 15px #00FF00; margin: 15px 0; letter-spacing: 5px; }
  </style>
  <script>
    setInterval(function() {
      fetch("/status").then(response => response.json()).then(data => {
        document.getElementById("timer").innerHTML = data.time;
        document.getElementById("state").innerHTML = data.state;
        document.getElementById("bat").innerHTML = data.bat + "%";
        if(data.dom == 1) { document.getElementById("domStats").innerHTML = "RED: " + data.tr + " | BLUE: " + data.tb; } 
        else { document.getElementById("domStats").innerHTML = ""; }
      });
    }, 2000); 
    function sendCmd(cmd) { fetch("/cmd?val=" + cmd); }
    function sendMsg() { var msg = document.getElementById("msgInput").value; fetch("/msg?text=" + encodeURIComponent(msg)); document.getElementById("msgInput").value = ""; }
    function setGameTime() { var t = document.getElementById("timeInput").value; fetch("/settime?val=" + t); }
    function setLockKey() { fetch("/setlock"); alert("LAST SCANNED CARD SAVED AS LOCK KEY!"); }
  </script>
</head><body>
  <h1>// TACTICAL TERMINAL //</h1>
  <div class="card">
    <div style="border-bottom: 1px dashed #00FF00; padding-bottom:5px;">STATUS MONITOR</div><br>
    <div>MODE: <span id="state">...</span></div>
    <div>BATTERY: <span id="bat">--</span></div>
    <div id="timer">00:00</div>
    <div id="domStats" style="color: cyan;"></div>
  </div>
  <div class="card">
    <div style="border-bottom: 1px dashed #00FF00; padding-bottom:5px;">SECURITY SYSTEM</div><br>
    <button class="btn" onclick="setLockKey()">SAVE LAST CARD AS KEY</button>
  </div>
  <div class="card">
    <div style="border-bottom: 1px dashed #00FF00; padding-bottom:5px;">GAME CONFIG</div><br>
    <input type="number" id="timeInput" placeholder="Minutes">
    <button class="btn" onclick="setGameTime()">SET</button>
  </div>
  <div class="card">
    <div style="border-bottom: 1px dashed #FF0000; padding-bottom:5px; color:#FF0000;">DANGER ZONE</div><br>
    <button class="btn btn-red" onclick="sendCmd('boom')">DETONATE</button>
    <button class="btn" onclick="sendCmd('win')">DEFUSE</button><br><br>
    <input type="text" id="msgInput" placeholder="Send Message...">
    <button class="btn" onclick="sendMsg()">SEND</button>
  </div>
</body></html>
)rawliteral";

// --- ФУНКЦИИ СЕРВЕРА ---
int getBatteryLevel() {
  int raw = analogRead(BAT_PIN); 
  int pct = map(raw, 1860, 2606, 0, 100);
  if(pct > 100) pct = 100; if(pct < 0) pct = 0;
  return pct;
}

void handleRoot() { server.send(200, "text/html", index_html); }

void handleStatus() {
  char jsonBuffer[400]; 
  int mins = timeRemaining / 60000;
  int secs = (timeRemaining % 60000) / 1000;
  int bat = getBatteryLevel();
  const char* stateStr = "BOOT"; 
  if (currentState == MENU) stateStr = "MENU";
  else if (currentState == SCREENSAVER) stateStr = "RADAR SCAN";
  else if (currentState == GAME_LOCK) {
      if(lockModeSetup) stateStr = "LOCK SETUP";
      else stateStr = "SECURED";
  }
  else if (currentState == GAME_DOMINATION) stateStr = "DOMINATION";
  else if (currentState >= GAME_MOTION && currentState <= GAME_READER) stateStr = "ARMED";
  else if (currentState == WIN) stateStr = "WIN";
  else if (currentState == LOSE) stateStr = "BOOM";

  if (currentState == GAME_DOMINATION) {
      int tRed = timeRed / 1000; int tBlue = timeBlue / 1000;
      snprintf(jsonBuffer, sizeof(jsonBuffer), "{\"time\":\"%02d:%02d\",\"state\":\"%s\",\"bat\":\"%d\",\"dom\":\"1\",\"tr\":\"%ds\",\"tb\":\"%ds\"}", mins, secs, stateStr, bat, tRed, tBlue);
  } else {
      snprintf(jsonBuffer, sizeof(jsonBuffer), "{\"time\":\"%02d:%02d\",\"state\":\"%s\",\"bat\":\"%d\",\"dom\":\"0\"}", mins, secs, stateStr, bat);
  }
  server.send(200, "application/json", jsonBuffer);
}

void handleCmd() { String val = server.arg("val"); if (val == "boom") currentState = LOSE; if (val == "win") currentState = WIN; server.send(200, "text/plain", "OK"); }
void handleMsg() { String text = server.arg("text"); if (text.length() > 0) { webMessage = text; messageTimer = millis(); myDFPlayer.play(SND_SIREN); } server.send(200, "text/plain", "OK"); }
void handleSetTime() { String val = server.arg("val"); int mins = val.toInt(); if (mins > 0) { timeRemaining = mins * 60 * 1000; totalTime = timeRemaining; } server.send(200, "text/plain", "OK"); }
void handleSetLock() { if (lastScannedLen > 0) { memcpy(lockKeyUID, lastScannedUID, lastScannedLen); lockKeyLen = lastScannedLen; myDFPlayer.play(SND_TICK); } server.send(200, "text/plain", "OK"); }

// --- ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ---
void drawHeader(String text, uint16_t color) {
  tft.fillScreen(ST77XX_BLACK); tft.fillRect(0, 0, 160, 20, color);
  tft.setCursor(5, 6); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print(text);
  int bat = getBatteryLevel(); uint16_t batColor = ST77XX_GREEN;
  if(bat < 40) batColor = ST77XX_YELLOW; if(bat < 20) batColor = ST77XX_RED;
  tft.drawRect(125, 25, 30, 10, ST77XX_WHITE); tft.fillRect(127, 27, map(bat, 0, 100, 0, 26), 6, batColor); 
  tft.setCursor(95, 27); tft.setTextColor(batColor); tft.setTextSize(1); tft.print(bat); tft.print("%");
}

void drawLockIcon(bool open, uint16_t color) {
  int cx = 80, cy = 64; tft.fillScreen(ST77XX_BLACK);
  tft.fillCircle(cx, cy-20, 25, color); tft.fillCircle(cx, cy-20, 15, ST77XX_BLACK);
  if (open) tft.fillRect(cx, cy-45, 30, 30, ST77XX_BLACK); else tft.fillRect(cx-10, cy-20, 20, 25, ST77XX_BLACK);
  tft.fillRoundRect(cx-30, cy-10, 60, 50, 5, color); tft.fillCircle(cx, cy+15, 6, ST77XX_BLACK); tft.fillRect(cx-2, cy+15, 4, 15, ST77XX_BLACK);
  tft.setCursor(35, 110); tft.setTextColor(color); tft.setTextSize(2); if(open) tft.print("UNLOCKED"); else tft.print("SECURED");
}

bool compareUID(uint8_t *uid1, uint8_t len1, uint8_t *uid2, uint8_t len2) {
    if (len1 != len2 || len1 == 0) return false;
    for (uint8_t i = 0; i < len1; i++) if (uid1[i] != uid2[i]) return false;
    return true;
}

// 🔥 ФУНКЦИЯ ПРОВЕРКИ ОБНОВЛЕНИЙ 🔥
void checkUpdate() {
  tft.fillScreen(ST77XX_BLACK); tft.setCursor(10, 50); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("CHECKING UPDATE...");
  
  WiFiClientSecure client;
  client.setInsecure(); // Пропускаем проверку сертификата для надежности
  HTTPClient http;
  
  // 1. Проверяем версию
  if (http.begin(client, ver_url)) {
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      int newVersion = payload.toInt();
      if (newVersion > CURRENT_VERSION) {
        // НАШЛИ НОВУЮ ВЕРСИЮ!
        tft.fillScreen(ST77XX_RED); tft.setCursor(10, 10); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(2); tft.println("NEW UPDATE!"); 
        tft.setTextSize(1); tft.setCursor(10, 40); tft.print("v"); tft.print(CURRENT_VERSION); tft.print(" -> v"); tft.println(newVersion);
        tft.drawRect(10, 95, 140, 10, ST77XX_WHITE); 
        
        // 2. Качаем бинарник
        http.end(); // Закрываем старое соединение
        
        // Настраиваем обновление
        int contentLength = 0;
        if (http.begin(client, bin_url)) {
           httpCode = http.GET();
           if (httpCode == HTTP_CODE_OK) {
               contentLength = http.getSize();
               if (contentLength > 0 && Update.begin(contentLength)) {
                   WiFiClient *stream = http.getStreamPtr();
                   size_t written = 0;
                   uint8_t buff[128];
                   
                   while(http.connected() && (written < contentLength)) {
                       size_t size = stream->available();
                       if(size) {
                           int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
                           Update.write(buff, c);
                           written += c;
                           // Рисуем прогресс
                           int percent = (written * 100) / contentLength;
                           int w = map(percent, 0, 100, 0, 136); tft.fillRect(12, 97, w, 6, ST77XX_YELLOW);
                       }
                       delay(1);
                   }
                   if (Update.end()) {
                       tft.fillScreen(ST77XX_GREEN); tft.setCursor(20,50); tft.setTextColor(ST77XX_BLACK); tft.print("SUCCESS!"); delay(2000); ESP.restart();
                   }
               }
           }
        }
      }
    }
    http.end();
  }
}

void playSmartBoot() {
  tft.fillScreen(ST77XX_BLACK); tft.setTextColor(MATRIX_GREEN); tft.setTextSize(1);
  tft.setCursor(0, 0); tft.println("INIT KERNEL... [OK]"); delay(200);
  tft.print("SEARCHING UPLINK");
  WiFi.mode(WIFI_STA); WiFi.begin(router_ssid, router_pass);
  int retries = 0; while (WiFi.status() != WL_CONNECTED && retries < 15) { delay(500); tft.print("."); retries++; }
  if (WiFi.status() == WL_CONNECTED) {
    tft.println(" [OK]"); tft.setTextColor(ST77XX_ORANGE); tft.println("UPLINK ESTABLISHED");
    tft.setTextColor(ST77XX_WHITE); tft.print("IP: "); tft.println(WiFi.localIP()); myDFPlayer.play(SND_BOOT);
    
    // 🔥 ЗАПУСК ПРОВЕРКИ ОБНОВЛЕНИЯ 🔥
    checkUpdate();
    
  } else {
    tft.println(" [FAIL]"); tft.setTextColor(ST77XX_ORANGE); tft.println("EMERGENCY AP MODE...");
    WiFi.disconnect(); WiFi.mode(WIFI_AP); WiFi.softAP(ap_ssid, ap_pass, 1, 0, 4); 
    tft.setTextColor(ST77XX_WHITE); tft.print("IP: "); tft.println(WiFi.softAPIP()); myDFPlayer.play(SND_BOOT);
  }
  delay(1000); 
}

void animateDefuseProcess() {
  tft.fillScreen(ST77XX_BLACK); tft.setCursor(10, 20); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(2); tft.print("SYSTEM"); tft.setCursor(10, 40); tft.print("HACKING...");
  tft.drawRect(10, 80, 140, 20, ST77XX_WHITE); myDFPlayer.play(SND_HACK); 
  for(int i=0; i<=100; i++) {
     server.handleClient(); ArduinoOTA.handle(); yield(); 
     int w = map(i, 0, 100, 0, 136); tft.fillRect(12, 82, w, 16, ST77XX_GREEN);
     if (i % 5 == 0) { for(int k=0; k<LED_COUNT; k++) strip.setPixelColor(k, strip.Color(0, random(50,255), 0)); strip.show(); }
     delay(60); 
  }
  delay(500);
}

void showWebMessage() {
  tft.fillScreen(ST77XX_RED); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(2);
  tft.setCursor(10, 10); tft.println("INCOMING"); tft.setCursor(10, 30); tft.println("MESSAGE:");
  tft.setTextSize(2); tft.setTextColor(ST77XX_BLACK); tft.setCursor(5, 60); tft.println(webMessage);
  strip.fill(strip.Color(255, 0, 255)); strip.show();
}

void resetActivity() {
  lastActivityTime = millis();
  if (currentState == SCREENSAVER) { currentState = MENU; drawMenu(); }
}

void drawRadarGrid(int cx, int cy, int r) {
    tft.drawCircle(cx, cy, r, DARK_GREEN); tft.drawCircle(cx, cy, r/2, DARK_GREEN);
    tft.drawLine(cx-r, cy, cx+r, cy, DARK_GREEN); tft.drawLine(cx, cy-r, cx, cy+r, DARK_GREEN);
}

void setup() {
  Serial.begin(115200);
  tft.initR(INITR_BLACKTAB); tft.setRotation(1); tft.fillScreen(ST77XX_BLACK);
  pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH); 
  strip.begin(); strip.show(); strip.setBrightness(10); 
  mp3Serial.begin(9600, SERIAL_8N1, MP3_RX_PIN, MP3_TX_PIN); myDFPlayer.begin(mp3Serial); myDFPlayer.volume(20); 
  Wire.begin(); mpu.begin(); mpu.setAccelerometerRange(MPU6050_RANGE_8_G); mpu.setGyroRange(MPU6050_RANGE_500_DEG); 
  nfc.begin(); nfc.reset(); nfc.setupRF();

  playSmartBoot();

  server.on("/", handleRoot); server.on("/status", handleStatus);
  server.on("/cmd", handleCmd); server.on("/msg", handleMsg);
  server.on("/settime", handleSetTime);
  server.on("/setlock", handleSetLock);
  server.begin();

  ArduinoOTA.setHostname("CSGOBomb");
  ArduinoOTA.onStart([]() { 
      tft.fillScreen(ST77XX_RED); 
      tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(2); tft.setCursor(10, 10); tft.println("WARNING!");
      tft.setTextSize(1); tft.setCursor(10, 40); tft.println("FIRMWARE UPDATE");
      tft.setCursor(10, 60); tft.println("DO NOT TURN OFF");
      tft.drawRect(10, 95, 140, 10, ST77XX_WHITE); strip.fill(strip.Color(255, 0, 0)); strip.show(); 
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      int percent = (progress / (total / 100));
      tft.fillRect(60, 80, 40, 10, ST77XX_RED); tft.setCursor(60, 80); tft.setTextColor(ST77XX_WHITE); tft.print(percent); tft.print("%");
      int w = map(percent, 0, 100, 0, 136); tft.fillRect(12, 97, w, 6, ST77XX_YELLOW);
  });
  ArduinoOTA.onEnd([]() { ESP.restart(); }); 
  ArduinoOTA.begin();
  
  currentState = BOOT; lastActivityTime = millis(); 
  for(int i=0; i<3; i++) blips[i].life = 0;
}

void drawTimer() {
    tft.fillRect(10, 40, 140, 45, ST77XX_BLACK); 
    tft.setCursor(20, 45); tft.setTextSize(4); tft.setTextColor(ST77XX_RED);
    long totalSeconds = timeRemaining / 1000; int m = totalSeconds / 60; int s = totalSeconds % 60;
    if (m < 10) tft.print("0"); tft.print(m); tft.print(":"); if (s < 10) tft.print("0"); tft.print(s);
}

void handleTimer() {
  if (millis() - gameTimer >= 1000) {
    gameTimer = millis(); timeRemaining -= 1000;
    if(currentState == GAME_DOMINATION) {
        if(domOwner == 1) timeRed += 1000; if(domOwner == 2) timeBlue += 1000;
        tft.fillRect(0, 30, 160, 100, ST77XX_BLACK);
        tft.setCursor(5, 30); tft.setTextSize(2); tft.setTextColor(ST77XX_RED); tft.print("RED: "); tft.print(timeRed/1000);
        tft.setCursor(5, 60); tft.setTextSize(2); tft.setTextColor(ST77XX_BLUE); tft.print("BLUE: "); tft.print(timeBlue/1000);
        tft.setCursor(40, 100); tft.setTextColor(ST77XX_WHITE); int m = (timeRemaining/1000)/60; int s = (timeRemaining/1000)%60;
        if(m<10)tft.print("0"); tft.print(m); tft.print(":"); if(s<10)tft.print("0"); tft.print(s);
    } else {
        if (timeRemaining < 20000 && !sirenActive) { myDFPlayer.play(SND_SIREN); sirenActive = true; } 
        else if (!sirenActive) { myDFPlayer.play(SND_TICK); }
        if(currentState != GAME_SAFE) drawTimer();
    }
    if (timeRemaining <= 0) {
        if(currentState == GAME_DOMINATION) {
            tft.fillScreen(ST77XX_BLACK); tft.setCursor(10, 50); tft.setTextSize(3);
            if(timeRed > timeBlue) { tft.setTextColor(ST77XX_RED); tft.print("RED WIN!"); } else { tft.setTextColor(ST77XX_BLUE); tft.print("BLUE WIN!"); }
            myDFPlayer.play(SND_WIN); delay(5000); myDFPlayer.play(SND_BOOT); currentState=BOOT;
        } else { currentState = LOSE; }
    }
  }
}

void handleGlobalDefuse(char key) {
  if (key) {
    resetActivity(); 
    if (key >= '0' && key <= '9') { inputCode += key; tft.setCursor(10, 90); tft.setTextSize(2); tft.setTextColor(ST77XX_WHITE); tft.print(inputCode); } 
    else if (key == '*') { inputCode = ""; tft.fillRect(10, 90, 140, 20, ST77XX_BLACK); } 
    else if (key == '#') {
      if (inputCode == correctCode || inputCode == masterCode) { currentState = WIN; } 
      else { tft.fillRect(10, 90, 140, 20, ST77XX_BLACK); tft.setCursor(10, 90); tft.setTextColor(ST77XX_RED); tft.print("ERROR"); myDFPlayer.play(SND_ERROR); delay(500); tft.fillRect(10, 90, 140, 20, ST77XX_BLACK); inputCode = ""; }
    }
  }
  if (millis() - lastNfcCheck > 250) {
    lastNfcCheck = millis();
    if (nfc.isCardPresent()) { 
        resetActivity(); 
        uint8_t uid[8]; uint8_t len = nfc.readCardSerial(uid);
        if (len > 0) {
            memcpy(lastScannedUID, uid, len); lastScannedLen = len; 
            animateDefuseProcess(); currentState = WIN; 
        }
    }
  }
}

void drawMenu() {
  tft.fillScreen(ST77XX_BLACK);
  drawHeader("SELECT MODE", MATRIX_GREEN); tft.setTextSize(1); tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 30); tft.println("1. DOMINATION");
  tft.setCursor(10, 42); tft.println("2. MOTION");
  tft.setCursor(10, 54); tft.println("3. KEYCARD");
  tft.setCursor(10, 66); tft.println("4. SAFE CRACK");
  tft.setCursor(10, 78); tft.println("5. WIRES");
  tft.setCursor(10, 90); tft.println("6. SNIPER");
  tft.setCursor(10, 102); tft.println("7. READER");
  tft.setCursor(10, 114); tft.println("8. LOCK (SEC)"); 
}

void loop() {
  ArduinoOTA.handle(); server.handleClient(); 
  
  sensors_event_t a, g, temp; mpu.getEvent(&a, &g, &temp);
  float motion = abs(a.acceleration.x - lastX) + abs(a.acceleration.y - lastY) + abs(a.acceleration.z - lastZ);
  lastX = a.acceleration.x; lastY = a.acceleration.y; lastZ = a.acceleration.z;
  
  if (currentState != GAME_LOCK && motion > 2.0) resetActivity(); 

  if (webMessage != "") { if (millis() - messageTimer < 5000) { static bool shown = false; if (!shown) { showWebMessage(); shown = true; } return; } else { webMessage = ""; tft.fillScreen(ST77XX_BLACK); strip.clear(); strip.show(); } }
  
  if (currentState == MENU && millis() - lastActivityTime > 120000) { currentState = SCREENSAVER; tft.fillScreen(ST77XX_BLACK); radarAngle = 0; drawRadarGrid(80, 64, 60); }
  
  char key = keypad.getKey(); 
  if (key) resetActivity(); 
  
  if (currentState == SCREENSAVER || currentState == MENU) { if (millis() - lastNfcCheck > 500) { lastNfcCheck = millis(); if (nfc.isCardPresent()) { 
      uint8_t uid[8]; uint8_t len = nfc.readCardSerial(uid);
      if(len > 0) { memcpy(lastScannedUID, uid, len); lastScannedLen = len; resetActivity(); myDFPlayer.play(SND_TICK); }
  }}} 
  
  switch (currentState) {
    case BOOT: drawMenu(); currentState = MENU; break;
    case SCREENSAVER:
      {
        int cx = 80; int cy = 64; int r = 60;
        float radOld = (radarAngle - 5) * 0.0174533; tft.drawLine(cx, cy, cx + r * cos(radOld), cy + r * sin(radOld), ST77XX_BLACK); 
        if (radarAngle % 90 == 0) drawRadarGrid(cx, cy, r); 
        tft.drawLine(cx, cy, cx + r * cos(radarAngle * 0.0174533), cy + r * sin(radarAngle * 0.0174533), 0x07E0); 
        if (random(0, 30) == 0) { for(int i=0; i<3; i++) { if (blips[i].life == 0) { float dist = random(10, r-5); float ang = random(0, 360) * 0.0174533; blips[i].x = cx + dist * cos(ang); blips[i].y = cy + dist * sin(ang); blips[i].life = 20; break; } } }
        for(int i=0; i<3; i++) { if (blips[i].life > 0) { tft.fillCircle(blips[i].x, blips[i].y, 2, ST77XX_RED); blips[i].life--; if(blips[i].life == 0) tft.fillCircle(blips[i].x, blips[i].y, 2, ST77XX_BLACK); } }
        radarAngle += 5; if (radarAngle >= 360) radarAngle = 0; delay(50);
      }
      break;
    
    // 🔥 РЕЖИМ ЗАМКА (ВЕРНУЛ ЗВУК + ВИЗУАЛ ОШИБКИ) 🔥
    case GAME_LOCK:
      if (lockModeSetup) {
          // 1. РЕГИСТРАЦИЯ КЛЮЧА
          if (nfc.isCardPresent()) {
              sessionKeyLen = nfc.readCardSerial(sessionKeyUID);
              if (sessionKeyLen > 0) {
                  myDFPlayer.play(SND_ARMED); // Вернул звук постановки
                  lockModeSetup = false; 
                  drawLockIcon(false, ST77XX_GREEN);
              }
          }
      } 
      else if (lockAlarmActive) {
          // 2. ТРЕВОГА (ОРЁТ)
          if (millis() - lockAlarmTimer > 5000) { lockAlarmActive = false; drawLockIcon(false, ST77XX_GREEN); } 
          else {
              if ((millis() / 250) % 2 == 0) { tft.fillScreen(ST77XX_RED); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(3); tft.setCursor(30, 50); tft.print("ALARM!"); strip.fill(strip.Color(255,0,0)); } 
              else { tft.fillScreen(ST77XX_BLACK); strip.clear(); }
              strip.show();
          }
      } 
      else {
          // 3. ОХРАНА (ТИШИНА, НО ПРОВЕРКА КАРТЫ С ОШИБКОЙ)
          if (motion > 12.0) { lockAlarmActive = true; lockAlarmTimer = millis(); myDFPlayer.play(SND_SIREN); }
          
          if (millis() - lastNfcCheck > 500) {
              lastNfcCheck = millis();
              if (nfc.isCardPresent()) {
                  uint8_t uid[8]; uint8_t len = nfc.readCardSerial(uid);
                  if (len > 0) {
                      if (compareUID(uid, len, sessionKeyUID, sessionKeyLen)) { 
                          myDFPlayer.play(SND_WIN); drawLockIcon(true, ST77XX_BLUE); delay(2000); currentState = MENU; drawMenu(); 
                      } else { 
                          // 🔥 ОШИБКА КАРТЫ
                          myDFPlayer.play(SND_ERROR); 
                          tft.fillRect(10, 100, 140, 20, ST77XX_BLACK);
                          tft.setCursor(20, 100); tft.setTextColor(ST77XX_RED); tft.setTextSize(2); tft.print("WRONG KEY!");
                          delay(1000);
                          drawLockIcon(false, ST77XX_GREEN); // Перерисовка
                      }
                  }
              }
          }
          if(key) {
             if(key >= '0' && key <= '9') { inputCode += key; tft.setCursor(55, 110); tft.setTextSize(2); tft.setTextColor(ST77XX_WHITE); tft.print("*"); }
             else if(key == '*') { inputCode = ""; drawLockIcon(false, ST77XX_GREEN); }
             else if(key == '#') {
                 if(inputCode == correctCode || inputCode == masterCode) {
                     myDFPlayer.play(SND_WIN); drawLockIcon(true, ST77XX_BLUE); delay(2000); currentState = MENU; drawMenu(); 
                 } else {
                     myDFPlayer.play(SND_ERROR); inputCode = ""; drawLockIcon(false, ST77XX_GREEN);
                 }
             }
          }
      }
      break;

    case MENU:
      if (key) {
        int sel = key - '0';
        if (sel == 1) { currentState = GAME_DOM_SETUP; tft.fillScreen(ST77XX_BLACK); tft.setCursor(10, 50); tft.setTextSize(2); tft.setTextColor(ST77XX_RED); tft.println("SCAN RED..."); redLen = 0; blueLen = 0; }
        else if (sel == 2) { selectedGame = 1; currentState = SETUP_TIME; }
        else if (sel == 3) { selectedGame = 2; currentState = SETUP_TIME; }
        else if (sel == 4) { selectedGame = 3; currentState = SETUP_TIME; }
        else if (sel == 5) { selectedGame = 4; currentState = SETUP_TIME; }
        else if (sel == 6) { selectedGame = 5; currentState = SETUP_TIME; }
        else if (sel == 7) { currentState = GAME_READER; drawHeader("CARD READER", ST77XX_BLUE); tft.setCursor(5, 60); tft.setTextSize(2); tft.setTextColor(ST77XX_WHITE); tft.print("SCAN CARD..."); }
        else if (sel == 8) { 
            currentState = GAME_LOCK; 
            lockModeSetup = true; 
            lockAlarmActive = false;
            inputCode = ""; 
            tft.fillScreen(ST77XX_BLACK);
            tft.setCursor(10, 50); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(2);
            tft.println("SCAN KEY"); tft.println("TO ARM...");
        }
        
        if(currentState == SETUP_TIME) { drawHeader("SET TIMER", ST77XX_ORANGE); inputCode = ""; tft.setCursor(10, 50); tft.setTextSize(3); tft.print("MIN: "); }
      }
      break;
    
    case GAME_DOM_SETUP: if(redLen==0){if(nfc.isCardPresent()){redLen=nfc.readCardSerial(redTeamUID);if(redLen>0){myDFPlayer.play(SND_TICK);tft.fillScreen(ST77XX_BLACK);tft.setCursor(10,50);tft.setTextColor(ST77XX_BLUE);tft.println("SCAN BLUE...");delay(1000);}}} else if(blueLen==0){if(nfc.isCardPresent()){uint8_t temp[8];uint8_t l=nfc.readCardSerial(temp);if(!compareUID(temp,l,redTeamUID,redLen)){memcpy(blueTeamUID,temp,l);blueLen=l;myDFPlayer.play(SND_TICK);selectedGame=7;currentState=SETUP_TIME;drawHeader("SET ROUND",ST77XX_ORANGE);inputCode="";tft.setCursor(10,50);tft.setTextColor(ST77XX_WHITE);tft.setTextSize(3);tft.print("MIN: ");}}} break;
    case GAME_DOMINATION: handleTimer(); if(millis()-lastNfcCheck>300){lastNfcCheck=millis();if(nfc.isCardPresent()){uint8_t uid[8];uint8_t len=nfc.readCardSerial(uid);if(len>0){if(compareUID(uid,len,redTeamUID,redLen)){if(domOwner!=1){domOwner=1;myDFPlayer.play(SND_TICK);}}else if(compareUID(uid,len,blueTeamUID,blueLen)){if(domOwner!=2){domOwner=2;myDFPlayer.play(SND_TICK);}}else{myDFPlayer.play(SND_ERROR);delay(2000);nfc.reset();nfc.setupRF();}}}} break;
    
    case SETUP_TIME: 
      if(key){
        if(key>='0'&&key<='9'){inputCode+=key;tft.print(key);}
        else if(key=='#'){
          long m=inputCode.toInt();if(m==0)m=1;timeRemaining=m*60*1000;totalTime=timeRemaining;gameTimer=millis();inputCode="";sirenActive=false;drawHeader("ACTIVE",ST77XX_RED);myDFPlayer.play(SND_ARMED);delay(3000); 
          if(selectedGame==1){currentState=GAME_MOTION;sensors_event_t a,g,t;mpu.getEvent(&a,&g,&t);lastX=a.acceleration.x;lastY=a.acceleration.y;lastZ=a.acceleration.z;} 
          if(selectedGame==2){currentState=GAME_NFC;nfc.reset();nfc.setupRF();} 
          if(selectedGame==3){currentState=GAME_SAFE;safeStage=0;targetTilt=random(-7,7);lockPicked=false;} 
          if(selectedGame==4){currentState=GAME_WIRES;wrongWiresCut=0;for(int i=0;i<4;i++){wires[i]=2;wiresCut[i]=false;}wires[random(0,4)]=1;int bad=random(0,4);while(wires[bad]==1)bad=random(0,4);wires[bad]=0;} 
          if(selectedGame==5){currentState=GAME_SNIPER;sensors_event_t a,g,t;mpu.getEvent(&a,&g,&t);lastX=a.acceleration.x;lastY=a.acceleration.y;lastZ=a.acceleration.z;} 
          if(selectedGame==7){currentState=GAME_DOMINATION;timeRed=0;timeBlue=0;domOwner=0;}
        }else if(key=='*'){ESP.restart();}
      } 
      break;
      
    case GAME_MOTION: handleTimer(); {sensors_event_t a,g,t;mpu.getEvent(&a,&g,&t);float diff=abs(a.acceleration.x-lastX)+abs(a.acceleration.y-lastY)+abs(a.acceleration.z-lastZ);if(diff>12.0)currentState=LOSE;lastX=a.acceleration.x;lastY=a.acceleration.y;lastZ=a.acceleration.z;} handleGlobalDefuse(key); break;
    case GAME_NFC: handleTimer(); handleGlobalDefuse(key); break;
    
    case GAME_SAFE: 
      if(millis()-gameTimer>=1000){gameTimer=millis();timeRemaining-=1000;if(timeRemaining<=0)currentState=LOSE;if(!sirenActive)myDFPlayer.play(SND_TICK);} 
      {sensors_event_t a,g,t;mpu.getEvent(&a,&g,&t);int tilt=(int)a.acceleration.y;
      tft.fillRect(0,30,160,100,ST77XX_BLACK);tft.setCursor(10,30);tft.setTextSize(1);tft.setTextColor(ST77XX_RED);tft.print("TIME: ");tft.print(timeRemaining/1000);
      tft.setCursor(10,50);tft.setTextSize(2);tft.setTextColor(ST77XX_WHITE);tft.print("LOCK: ");tft.print(safeStage+1);tft.print("/3");
      int diff=abs(tilt-targetTilt);int bar=0;uint16_t col=ST77XX_RED;if(diff<5){bar=50;col=ST77XX_ORANGE;}if(diff<3){bar=100;col=ST77XX_YELLOW;}
      if(diff<2){bar=140;col=ST77XX_BLUE;lockPicked=true;for(int i=0;i<LED_COUNT;i++)strip.setPixelColor(i,strip.Color(0,0,255));strip.show();}else{lockPicked=false;strip.clear();strip.show();}
      tft.fillRect(10,80,bar,10,col);tft.drawRect(10,80,140,10,ST77XX_WHITE);
      if(key=='#'){if(lockPicked){myDFPlayer.play(SND_BOOT);safeStage++;targetTilt=random(-8,8);if(safeStage>=3)currentState=WIN;}else{myDFPlayer.play(SND_ERROR);timeRemaining-=15000;}}} 
      handleGlobalDefuse(key); 
      break;
      
    case GAME_WIRES: 
      handleTimer(); 
      {uint16_t cols[]={ST77XX_RED,ST77XX_GREEN,ST77XX_BLUE,ST77XX_YELLOW};for(int i=0;i<4;i++){int y=90+i*10;if(!wiresCut[i]){tft.fillCircle(10,y,4,cols[i]);tft.fillRect(10,y-2,140,4,cols[i]);tft.fillCircle(150,y,4,cols[i]);}else{tft.fillCircle(10,y,4,ST77XX_GRAY);tft.fillRect(10,y-2,60,4,ST77XX_GRAY);tft.fillRect(90,y-2,60,4,ST77XX_GRAY);tft.fillCircle(150,y,4,ST77XX_GRAY);}} 
      if(key>='1'&&key<='4'){int w=key-'1';if(!wiresCut[w]){wiresCut[w]=true;if(wires[w]==1)currentState=WIN;else if(wires[w]==0)currentState=LOSE;else{wrongWiresCut++;if(wrongWiresCut>=2){myDFPlayer.play(SND_SIREN);sirenActive=true;}else myDFPlayer.play(SND_ERROR);}}}} 
      break;
      
    case GAME_SNIPER: {sensors_event_t a,g,t;mpu.getEvent(&a,&g,&t);float mv=abs(a.acceleration.x-lastX)+abs(a.acceleration.y-lastY)+abs(a.acceleration.z-lastZ);lastX=a.acceleration.x;lastY=a.acceleration.y;lastZ=a.acceleration.z;if(millis()-gameTimer>=1000){gameTimer=millis();timeRemaining-=1000;}if(mv>2.0){timeRemaining-=50;tft.fillCircle(150,50,5,ST77XX_RED);}else tft.fillCircle(150,50,5,ST77XX_BLACK);if(timeRemaining<=0)currentState=LOSE;drawTimer();handleGlobalDefuse(key);} break;
    case GAME_READER: if(nfc.isCardPresent()){uint8_t uid[8];uint8_t len=nfc.readCardSerial(uid);if(len>0){resetActivity();myDFPlayer.play(SND_BOOT);tft.fillScreen(ST77XX_BLUE);tft.setTextColor(ST77XX_WHITE);tft.setCursor(5,5);tft.print("CARD DATA:");tft.setCursor(5,45);for(int i=0;i<len;i++){if(uid[i]<0x10)tft.print("0");tft.print(uid[i],HEX);tft.print(" ");}delay(6000);drawHeader("CARD READER",ST77XX_BLUE);}} if(key=='*')ESP.restart(); break;
    
    case WIN: tft.fillScreen(ST77XX_GREEN);tft.setCursor(20,50);tft.setTextColor(ST77XX_BLACK);tft.setTextSize(3);tft.print("DEFUSED");for(int i=0;i<5;i++)strip.setPixelColor(i,strip.Color(0,255,0));strip.show();myDFPlayer.play(SND_WIN);delay(5000);myDFPlayer.play(SND_BOOT);currentState=BOOT; break;
    case LOSE: myDFPlayer.play(SND_LOSE);{unsigned long s=millis();while(millis()-s<5000){tft.fillScreen(ST77XX_RED);tft.setCursor(30,50);tft.setTextColor(ST77XX_BLACK);tft.setTextSize(3);tft.print("BOOM!");for(int i=0;i<5;i++)strip.setPixelColor(i,strip.Color(255,0,0));strip.show();delay(300);tft.fillScreen(ST77XX_BLACK);strip.clear();strip.show();delay(300);}}myDFPlayer.play(SND_BOOT);currentState=BOOT; break;
    default: currentState=BOOT; break;
  }
}