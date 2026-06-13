// ╔══════════════════════════════════════════════════════════════════╗
// ║   CLOUD MUSHROOM — 3C (Master)  v8                              ║
// ║   Hardware : ESP32-S3 DevKitC (Dual-core LX7)                  ║
// ╠══════════════════════════════════════════════════════════════════╣
// ║   PIN MAP                                                        ║
// ║   GPIO4  → Exhaust fan relay (active HIGH)                      ║
// ║   GPIO5  → Humidifier SSR   (active HIGH)                       ║
// ║   GPIO6  ← Blue button  (NEXT / enter SET / confirm save)       ║
// ║   GPIO7  ← Red button   (value UP, hold=fast repeat)            ║
// ║   GPIO15 ← Black button (value DOWN, hold=fast repeat)          ║
// ║   GPIO8  ↔ I2C SDA  → LCD 0x27                                 ║
// ║   GPIO9  → I2C SCL  → LCD 0x27                                 ║
// ║   GPIO12 → Red LED   (alert)                                    ║
// ║   GPIO13 → Green LED (healthy)                                  ║
// ║   GPIO20 → Yellow LED (auto mode)                               ║
// ║   GPIO48 → NeoPixel WS2812B                                     ║
// ╠══════════════════════════════════════════════════════════════════╣
// ║   SET MODE  (press Blue to enter):                              ║
// ║     Blue  = advance to next param (3rd press saves & exits)     ║
// ║     Red   = value +1  (hold for fast repeat)                    ║
// ║     Black = value -1  (hold for fast repeat)                    ║
// ║     Params: Hum Upper% → Hum Lower% → CO2 ppm limit            ║
// ║     5 s inactivity = auto-save & exit                           ║
// ╠══════════════════════════════════════════════════════════════════╣
// ║   NeoPixel colours:                                             ║
// ║     Boot         Magenta  (60,0,60)                             ║
// ║     WiFi OK      Green    (0,80,0)                              ║
// ║     No 2B data   Blue     (0,0,120)                             ║
// ║     All OK       Dim green (0,40,0)                             ║
// ║     SET mode     Cyan     (0,80,80)                             ║
// ║     ESP-NOW RX   Bright green pulse                             ║
// ║     Alert lv1    Yellow   (120,100,0)                           ║
// ║     Alert lv2    Orange   (180,60,0)                            ║
// ║     Alert lv3    Red      (255,0,0)                             ║
// ╠══════════════════════════════════════════════════════════════════╣
// ║   TELEGRAM commands:                                            ║
// ║     /status  /thresholds  /auto  /manual                        ║
// ║     /fan on|off   /hum on|off                                   ║
// ╠══════════════════════════════════════════════════════════════════╣
// ║   LIBRARIES                                                      ║
// ║   • Adafruit NeoPixel                                           ║
// ║   • LiquidCrystal_I2C (Frank de Brabander — works on ESP32)    ║
// ║   • Preferences  (built-in ESP32 core)                          ║
// ║   • ArduinoOTA   (built-in ESP32 core)                          ║
// ║   Board: ESP32S3 Dev Module  Upload: 921600                     ║
// ╚══════════════════════════════════════════════════════════════════╝

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoOTA.h>          // ← OTA ADDED

// ─── USER CONFIG ──────────────────────────────────────────────────
const char WIFI_SSID[] = "Sharmila";
const char WIFI_PASS[] = "sharmilaswifi";
const char TG_TOKEN[]  = "8668346564:AAGso-peHGPDQ55k79_zYH2aQYKYgHx-DKE";
const long TG_CHAT_ID  = 753310811;

// ── OTA CONFIG ────────────────────────────────────────────────────
const char OTA_HOSTNAME[] = "cloud-mushroom-3c";
const char OTA_PASSWORD[] = "ota1234";          // change to something strong
// ─────────────────────────────────────────────────────────────────

uint8_t SLAVE_2B_MAC[6] = {0xD0, 0xEF, 0x76, 0x33, 0x69, 0x48};

#define NEO_PIN        48
#define NEO_COUNT       1
#define NEO_BRIGHT     60

#define LINK_TIMEOUT_MS   15000UL
#define TG_POLL_MS         3000UL
#define CHAN_MAGIC         0xC4

// CO2 hysteresis deadband (ppm below co2Limit before fan turns OFF)
#define CO2_HYST_PPM      100
// ─── END USER CONFIG ──────────────────────────────────────────────

// ─── PINS ─────────────────────────────────────────────────────────
#define PIN_RELAY_FAN   4
#define PIN_RELAY_HUM   5
#define PIN_BTN_BLUE    6
#define PIN_BTN_RED     7
#define PIN_BTN_BLACK  15
#define PIN_LED_RED    12
#define PIN_LED_GREEN  13
#define PIN_LED_YELLOW 20

// ─── I2C / LCD ────────────────────────────────────────────────────
#define I2C_SDA  8
#define I2C_SCL  9

// ─── CMD CODES ────────────────────────────────────────────────────
#define CMD_SSR_ON    0x10
#define CMD_SSR_OFF   0x11
#define CMD_MECH_ON   0x20
#define CMD_MECH_OFF  0x21
#define CMD_REQ_DATA  0x50

// ─── PACKET STRUCTS ───────────────────────────────────────────────
struct __attribute__((packed)) SensorPacket {
  uint8_t  nodeId;
  float    temperature;
  float    humidity;
  uint16_t co2ppm;
  uint8_t  ssrOn;
  uint8_t  mechOn;
  uint8_t  fanIntakeOn;
  uint8_t  coolerOn;
  uint8_t  co2Preheating;
  uint8_t  errorFlags;
  uint32_t uptimeSec;
};

struct __attribute__((packed)) CmdPacket {
  uint8_t  targetId;
  uint8_t  cmd;
  float    param;
};

// ─── HARDWARE OBJECTS ─────────────────────────────────────────────
LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_NeoPixel neo(NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);
Preferences       prefs;

// ─── GLOBAL STATE ─────────────────────────────────────────────────
SensorPacket last2B    = {};
bool         got2BData = false;
uint32_t     tLast2B   = 0;
bool         espNowReady = false;

bool fanOn   = false;
bool humOn   = false;
bool autoMode = true;

// Thresholds
float humHigh  = 85.0f;
float humLow   = 75.0f;
float co2Limit = 1200.0f;

// ISR packet buffer
static SensorPacket isrBuf    = {};
static volatile bool isrReady = false;

// ─── NEOPIXEL ─────────────────────────────────────────────────────
enum RgbState { RGB_IDLE, RGB_RX, RGB_TX, RGB_SET };
RgbState rgbState  = RGB_IDLE;
uint32_t tRgbPulse = 0;
#define RGB_PULSE_MS 400UL

void setRGB(uint8_t r, uint8_t g, uint8_t b) {
  neo.setPixelColor(0, neo.Color(r, g, b));
  neo.show();
}

void pulseRGB(uint8_t r, uint8_t g, uint8_t b, RgbState st) {
  setRGB(r, g, b);
  rgbState  = st;
  tRgbPulse = millis();
}

void updateRGBIdle() {
  if (WiFi.status() != WL_CONNECTED)             { setRGB(255,  0,  0); }
  else if (!got2BData)                             { setRGB(  0,  0,120); }
  else {
    bool linkOk = (millis() - tLast2B) < LINK_TIMEOUT_MS;
    if (!linkOk)                                   { setRGB(255,  0,  0); }
    else if (last2B.co2ppm > (uint16_t)co2Limit)  { setRGB(120,100,  0); }
    else if (last2B.humidity < humLow)             { setRGB(120,100,  0); }
    else                                           { setRGB(  0, 40,  0); }
  }
  rgbState = RGB_IDLE;
}

// ─── RELAY CONTROL ────────────────────────────────────────────────
void setFan(bool on) {
  if (fanOn == on) return;
  fanOn = on;
  digitalWrite(PIN_RELAY_FAN, on ? HIGH : LOW);
  Serial.printf("[3C] ExhaustFan → %s\n", on ? "ON" : "OFF");
}

void setHum(bool on) {
  if (humOn == on) return;
  humOn = on;
  digitalWrite(PIN_RELAY_HUM, on ? HIGH : LOW);
  Serial.printf("[3C] Humidifier → %s\n", on ? "ON" : "OFF");
}

// ─── STATUS LEDs ──────────────────────────────────────────────────
void updateStatusLEDs() {
  bool linkOk  = got2BData && (millis() - tLast2B) < LINK_TIMEOUT_MS;
  bool alertOn = !linkOk ||
                 (got2BData && last2B.co2ppm > (uint16_t)co2Limit) ||
                 (got2BData && last2B.humidity < humLow);
  digitalWrite(PIN_LED_GREEN,  (linkOk && !alertOn) ? HIGH : LOW);
  digitalWrite(PIN_LED_RED,    alertOn ? HIGH : LOW);
  digitalWrite(PIN_LED_YELLOW, autoMode ? HIGH : LOW);
}

// ─── NVS LOAD / SAVE ──────────────────────────────────────────────
void loadThresholds() {
  prefs.begin("thresh", true);
  humHigh  = prefs.getFloat("humH",  85.0f);
  humLow   = prefs.getFloat("humL",  75.0f);
  co2Limit = prefs.getFloat("co2L", 1200.0f);
  prefs.end();
  Serial.printf("[3C] Thresholds loaded  HumH=%.0f HumL=%.0f CO2=%.0f\n",
                humHigh, humLow, co2Limit);
}

void saveThresholds() {
  prefs.begin("thresh", false);
  prefs.putFloat("humH",  humHigh);
  prefs.putFloat("humL",  humLow);
  prefs.putFloat("co2L",  co2Limit);
  prefs.end();
  Serial.printf("[3C] Thresholds saved   HumH=%.0f HumL=%.0f CO2=%.0f\n",
                humHigh, humLow, co2Limit);
}

// ─── AUTOMATION ───────────────────────────────────────────────────
void runAutomation() {
  if (!autoMode || !got2BData) return;

  // ── Humidifier ────────────────────────────────────────────────
  if (!humOn && last2B.humidity < humLow)  setHum(true);
  if ( humOn && last2B.humidity > humHigh) setHum(false);

  // ── Exhaust fan (CO2 hysteresis) ─────────────────────────────
  if (!fanOn && last2B.co2ppm > (uint16_t)co2Limit)
    setFan(true);
  if ( fanOn && last2B.co2ppm < (uint16_t)(co2Limit - CO2_HYST_PPM))
    setFan(false);
}

// ─── LCD — DEFAULT DISPLAY ────────────────────────────────────────
uint8_t  lcdScreen = 0;
uint32_t tLcdCycle = 0;
#define LCD_CYCLE_MS 3000UL

void lcdShowDefault() {
  uint32_t now = millis();
  if ((now - tLcdCycle) >= LCD_CYCLE_MS) {
    tLcdCycle = now;
    lcdScreen = (lcdScreen + 1) % 3;
    lcd.clear();
  }

  lcd.setCursor(0, 0);
  lcd.setCursor(0, 1);

  if (!got2BData) {
    lcd.setCursor(0, 0); lcd.print("Waiting for 2B  ");
    lcd.setCursor(0, 1); lcd.print(autoMode ? "Mode: AUTO      " : "Mode: MANUAL    ");
    return;
  }

  char r0[17], r1[17];
  switch (lcdScreen) {
    case 0:
      snprintf(r0, 17, "T:%.1fC  H:%.0f%%  ", last2B.temperature, last2B.humidity);
      snprintf(r1, 17, "CO2: %4u ppm    ", last2B.co2ppm);
      break;
    case 1:
      snprintf(r0, 17, "Fan:%-3s Hum:%-3s  ", fanOn ? "ON" : "OFF", humOn ? "ON" : "OFF");
      snprintf(r1, 17, "%-16s", autoMode ? "Mode: AUTO" : "Mode: MANUAL");
      break;
    case 2: {
      bool lk = (millis() - tLast2B) < LINK_TIMEOUT_MS;
      snprintf(r0, 17, "2B:%-4s Up:%4lus ", lk ? "LIVE" : "LOST",
               (unsigned long)(millis() / 1000));
      snprintf(r1, 17, "HH:%.0f HL:%.0f C:%.0f", humHigh, humLow, co2Limit);
      break;
    }
  }
  lcd.setCursor(0, 0); lcd.print(r0);
  lcd.setCursor(0, 1); lcd.print(r1);
}

// ─── LCD — SET DISPLAY ────────────────────────────────────────────
const char* SET_LABEL[3] = { "Hum Upper % ", "Hum Lower % ", "CO2 Limit ppm" };

float setValues[3];
bool     inSetMode   = false;
uint8_t  setParamIdx = 0;
uint32_t tSetLastKey = 0;
#define SET_TIMEOUT_MS 5000UL

void lcdShowSet() {
  lcd.setCursor(0, 0);
  char r0[17];
  snprintf(r0, 17, "%-16s", SET_LABEL[setParamIdx]);
  lcd.print(r0);

  lcd.setCursor(0, 1);
  char r1[17];
  if (setParamIdx == 2)
    snprintf(r1, 17, " [%4.0f]  Bl=Nxt", setValues[setParamIdx]);
  else
    snprintf(r1, 17, "  [%5.1f] Bl=Nxt", setValues[setParamIdx]);
  lcd.print(r1);
}

float getStep(uint8_t idx) { return (idx == 2) ? 50.0f : 1.0f; }
float getMin (uint8_t idx) {
  if (idx == 0) return 51.0f;
  if (idx == 1) return 30.0f;
  return 400.0f;
}
float getMax (uint8_t idx) {
  if (idx == 0) return 99.0f;
  if (idx == 1) return 95.0f;
  return 5000.0f;
}

void adjustValue(uint8_t idx, int dir) {
  setValues[idx] += dir * getStep(idx);
  if (setValues[idx] < getMin(idx)) setValues[idx] = getMin(idx);
  if (setValues[idx] > getMax(idx)) setValues[idx] = getMax(idx);
  if (idx == 0 && setValues[0] <= setValues[1]) setValues[0] = setValues[1] + 1.0f;
  if (idx == 1 && setValues[1] >= setValues[0]) setValues[1] = setValues[0] - 1.0f;
  tSetLastKey = millis();
  lcd.clear();
  lcdShowSet();
}

void enterSetMode() {
  inSetMode   = true;
  setParamIdx = 0;
  setValues[0] = humHigh;
  setValues[1] = humLow;
  setValues[2] = co2Limit;
  tSetLastKey  = millis();
  lcd.clear();
  lcdShowSet();
  setRGB(0, 80, 80);
  Serial.println("[3C] Entered SET mode");
}

void exitSetMode(bool save) {
  inSetMode = false;
  if (save) {
    humHigh  = setValues[0];
    humLow   = setValues[1];
    co2Limit = setValues[2];
    saveThresholds();
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("  Saved!        ");
    delay(700);
  } else {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("  Cancelled     ");
    delay(500);
  }
  lcd.clear();
  tLcdCycle = 0; // force refresh  lcdScreen = 0;
  tLcdCycle = 0;
  updateRGBIdle();
  Serial.printf("[3C] Exited SET mode (saved=%d)\n", save ? 1 : 0);
}

// ─── BUTTON HANDLING ──────────────────────────────────────────────
#define HOLD_DELAY_MS   600UL
#define HOLD_REPEAT_MS  150UL

void handleButtons() {
  static bool  lastBlue  = true, lastRed  = true, lastBlack = true;
  static uint32_t tRedHold = 0, tBlkHold = 0;
  static bool  redHolding = false, blkHolding = false;

  bool nowBlue  = digitalRead(PIN_BTN_BLUE);
  bool nowRed   = digitalRead(PIN_BTN_RED);
  bool nowBlack = digitalRead(PIN_BTN_BLACK);

  if (lastBlue == HIGH && nowBlue == LOW) {
    if (!inSetMode) {
      enterSetMode();
    } else {
      setParamIdx++;
      if (setParamIdx >= 3) {
        exitSetMode(true);
        lastBlue = nowBlue; lastRed = nowRed; lastBlack = nowBlack;
        return;
      }
      tSetLastKey = millis();
      lcd.clear();
      lcdShowSet();
    }
  }
  lastBlue = nowBlue;

  if (inSetMode) {
    if (lastRed == HIGH && nowRed == LOW) {
      adjustValue(setParamIdx, +1);
      tRedHold   = millis();
      redHolding = false;
    } else if (nowRed == LOW) {
      if (!redHolding && (millis() - tRedHold) >= HOLD_DELAY_MS) {
        redHolding = true;
        tRedHold   = millis();
      }
      if (redHolding && (millis() - tRedHold) >= HOLD_REPEAT_MS) {
        tRedHold = millis();
        adjustValue(setParamIdx, +1);
      }
    } else {
      redHolding = false;
    }
  }
  lastRed = nowRed;

  if (inSetMode) {
    if (lastBlack == HIGH && nowBlack == LOW) {
      if (setParamIdx == 0) {
        exitSetMode(false);
        lastBlack = nowBlack;
        return;
      }
      adjustValue(setParamIdx, -1);
      tBlkHold   = millis();
      blkHolding = false;
    } else if (nowBlack == LOW) {
      if (!blkHolding && (millis() - tBlkHold) >= HOLD_DELAY_MS) {
        blkHolding = true;
        tBlkHold   = millis();
      }
      if (blkHolding && (millis() - tBlkHold) >= HOLD_REPEAT_MS) {
        tBlkHold = millis();
        adjustValue(setParamIdx, -1);
      }
    } else {
      blkHolding = false;
    }
  }
  lastBlack = nowBlack;

  if (inSetMode && (millis() - tSetLastKey) >= SET_TIMEOUT_MS) {
    Serial.println("[3C] SET timeout — auto-saving");
    exitSetMode(true);
  }
}

// ─── CHANNEL ANNOUNCE ─────────────────────────────────────────────
void announceChannel() {
  uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  if (!esp_now_is_peer_exist(bcast)) {
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, bcast, 6);
    p.channel = 0;
    p.encrypt = false;
    esp_now_add_peer(&p);
  }
  uint8_t curCh; wifi_second_chan_t sec;
  esp_wifi_get_channel(&curCh, &sec);
  uint8_t msg[2] = {CHAN_MAGIC, curCh};
  esp_now_send(bcast, msg, 2);
  Serial.printf("[3C] Channel announced: %d\n", curCh);
}

// ─── SEND CMD TO 2B ───────────────────────────────────────────────
void send2BCmd(uint8_t cmd, float param = 0.0f) {
  if (!espNowReady) return;
  CmdPacket c = {0x2B, cmd, param};
  esp_now_send(SLAVE_2B_MAC, (const uint8_t*)&c, sizeof(CmdPacket));
}

// ─── ESP-NOW CALLBACKS ────────────────────────────────────────────
void onSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  pulseRGB(0, 120, 255, RGB_TX);
}

void onReceived(const esp_now_recv_info_t *recvInfo,
                const uint8_t *data, int len) {
  if (len == 2 && data[0] == CHAN_MAGIC) {
    uint8_t curCh; wifi_second_chan_t sec;
    esp_wifi_get_channel(&curCh, &sec);
    uint8_t reply[2] = {CHAN_MAGIC, curCh};
    uint8_t sender[6];
    memcpy(sender, recvInfo->src_addr, 6);
    if (!esp_now_is_peer_exist(sender)) {
      esp_now_peer_info_t p = {};
      memcpy(p.peer_addr, sender, 6);
      p.channel = 0; p.encrypt = false;
      esp_now_add_peer(&p);
    }
    esp_now_send(sender, reply, 2);
    return;
  }

  if (len >= (int)sizeof(SensorPacket) && data[0] == 0x2B) {
    if (!isrReady) {
      memcpy(&isrBuf, data, sizeof(SensorPacket));
      isrReady = true;
    }
    pulseRGB(0, 220, 80, RGB_RX);
  }
}

// ─── TELEGRAM ─────────────────────────────────────────────────────
long     tgLastUpdateId = 0;
uint32_t tLastTgPoll    = 0;

void tgSend(const String &text) {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = String("https://api.telegram.org/bot") + TG_TOKEN + "/sendMessage";
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  String body = String("{\"chat_id\":") + TG_CHAT_ID +
                ",\"text\":\"" + text + "\",\"parse_mode\":\"Markdown\"}";
  http.POST(body);
  http.end();
}

void processTgCmd(const String &raw) {
  String t = raw; t.trim(); t.toLowerCase();
  Serial.printf("[3C][TG] %s\n", t.c_str());

  if (t == "/status") {
    String m = "Temp: " + String(last2B.temperature,1) + "C\n" +
               "Hum:  " + String(last2B.humidity,1) + "%\n" +
               "CO2:  " + String(last2B.co2ppm) + " ppm\n" +
               "Fan:  " + String(fanOn?"ON":"OFF") + "\n" +
               "Hum:  " + String(humOn?"ON":"OFF") + "\n" +
               "Mode: " + String(autoMode?"AUTO":"MANUAL");
    tgSend(m);

  } else if (t == "/thresholds") {
    String m = "HumHigh: " + String(humHigh,1) + "%\n" +
               "HumLow:  " + String(humLow,1)  + "%\n" +
               "CO2 lim: " + String((int)co2Limit) + " ppm\n" +
               "CO2 off: " + String((int)(co2Limit - CO2_HYST_PPM)) + " ppm";
    tgSend(m);

  } else if (t.startsWith("/set ")) {
    String args = t.substring(5);  // everything after "/set "
    args.trim();

    // split into key and value at the space
    int sp = args.indexOf(' ');
    if (sp < 0) {
      tgSend("Usage: /set humhigh <val> | /set humlow <val> | /set co2 <val>");
      return;
    }
    String key = args.substring(0, sp);
    float  val = args.substring(sp + 1).toFloat();

    if (key == "humhigh") {
      if (val <= humLow || val < 51 || val > 99) {
        tgSend("Invalid: humhigh must be 51-99 and > humlow (" + String(humLow,0) + "%)");
        return;
      }
      humHigh = val;
      saveThresholds();
      tgSend("HumHigh set to " + String(humHigh,1) + "%");

    } else if (key == "humlow") {
      if (val >= humHigh || val < 30 || val > 95) {
        tgSend("Invalid: humlow must be 30-95 and < humhigh (" + String(humHigh,0) + "%)");
        return;
      }
      humLow = val;
      saveThresholds();
      tgSend("HumLow set to " + String(humLow,1) + "%");

    } else if (key == "co2") {
      if (val < 400 || val > 5000) {
        tgSend("Invalid: co2 must be 400-5000 ppm");
        return;
      }
      co2Limit = val;
      saveThresholds();
      tgSend("CO2 limit set to " + String((int)co2Limit) + " ppm  (fan OFF below " +
             String((int)(co2Limit - CO2_HYST_PPM)) + " ppm)");

    } else {
      tgSend("Unknown key. Use: humhigh | humlow | co2");
    }

  } else if (t == "/fan on")  { autoMode=false; setFan(true);  tgSend("Fan ON (manual)"); }
  else if (t == "/fan off")   { autoMode=false; setFan(false); tgSend("Fan OFF (manual)"); }
  else if (t == "/hum on")    { autoMode=false; setHum(true);  tgSend("Humidifier ON (manual)"); }
  else if (t == "/hum off")   { autoMode=false; setHum(false); tgSend("Humidifier OFF (manual)"); }
  else if (t == "/auto")      { autoMode=true;  tgSend("Auto mode ON"); }
  else if (t == "/manual")    { autoMode=false; tgSend("Manual mode ON"); }
  else {
    tgSend("/status /thresholds /auto /manual\n/fan on|off /hum on|off\n/set humhigh <val> | humlow <val> | co2 <val>");
  }
}

void pollTelegram() {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  String url = String("https://api.telegram.org/bot") + TG_TOKEN +
               "/getUpdates?timeout=0&limit=5&offset=" + String(tgLastUpdateId+1);
  http.begin(client, url);
  if (http.GET() != 200) { http.end(); return; }
  String payload = http.getString();
  http.end();

  int idx = 0;
  while (true) {
    int p = payload.indexOf("\"update_id\":", idx);
    if (p < 0) break;
    int us = p + 12;
    int ue = payload.indexOf(',', us);
    long uid = payload.substring(us, ue).toInt();

    int tp = payload.indexOf("\"text\":\"", us);
    if (tp > 0) {
      int ts = tp + 8;
      int te = payload.indexOf('"', ts);
      String txt = payload.substring(ts, te);
      if (uid > tgLastUpdateId) {
        tgLastUpdateId = uid;
        processTgCmd(txt);
      }
    } else {
      if (uid > tgLastUpdateId) tgLastUpdateId = uid;
    }
    idx = ue + 1;
  }
}

// ─── OTA SETUP ────────────────────────────────────────────────────
void setupOTA() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("[3C][OTA] Start updating " + type);
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("OTA Update...   ");
    lcd.setCursor(0, 1); lcd.print("Do not power off");
    setRGB(0, 80, 80);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\n[3C][OTA] Done — rebooting");
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("OTA Complete!   ");
    lcd.setCursor(0, 1); lcd.print("Rebooting...    ");
    setRGB(0, 80, 0);
    delay(800);
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    uint8_t pct = progress / (total / 100);
    Serial.printf("[3C][OTA] %u%%\r", pct);
    char r1[17];
    snprintf(r1, 17, "Progress: %3u%%  ", pct);
    lcd.setCursor(0, 1); lcd.print(r1);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[3C][OTA] Error[%u]: ", error);
    const char* msg = "Unknown error   ";
    if      (error == OTA_AUTH_ERROR)    { Serial.println("Auth Failed");    msg = "Auth Failed     "; }
    else if (error == OTA_BEGIN_ERROR)   { Serial.println("Begin Failed");   msg = "Begin Failed    "; }
    else if (error == OTA_CONNECT_ERROR) { Serial.println("Connect Failed"); msg = "Connect Failed  "; }
    else if (error == OTA_RECEIVE_ERROR) { Serial.println("Receive Failed"); msg = "Receive Failed  "; }
    else if (error == OTA_END_ERROR)     { Serial.println("End Failed");     msg = "End Failed      "; }
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("OTA ERROR:      ");
    lcd.setCursor(0, 1); lcd.print(msg);
    setRGB(255, 0, 0);
    delay(2000);
  });

  ArduinoOTA.begin();
  Serial.printf("[3C][OTA] Ready  host=%s\n", OTA_HOSTNAME);
}
// ─────────────────────────────────────────────────────────────────

// ─── SETUP ────────────────────────────────────────────────────────
uint32_t lcdCycle = 0;

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\n================================");
  Serial.println(" Cloud Mushroom 3C  v8");
  Serial.println("================================");

  pinMode(PIN_RELAY_FAN, OUTPUT); digitalWrite(PIN_RELAY_FAN, LOW);
  pinMode(PIN_RELAY_HUM, OUTPUT); digitalWrite(PIN_RELAY_HUM, LOW);

  pinMode(PIN_BTN_BLUE,  INPUT_PULLUP);
  pinMode(PIN_BTN_RED,   INPUT_PULLUP);
  pinMode(PIN_BTN_BLACK, INPUT_PULLUP);

  pinMode(PIN_LED_RED,    OUTPUT); digitalWrite(PIN_LED_RED,    LOW);
  pinMode(PIN_LED_GREEN,  OUTPUT); digitalWrite(PIN_LED_GREEN,  LOW);
  pinMode(PIN_LED_YELLOW, OUTPUT); digitalWrite(PIN_LED_YELLOW, LOW);

  neo.begin();
  neo.setBrightness(NEO_BRIGHT);
  setRGB(60, 0, 60);

  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("Cloud Mushroom  ");
  lcd.setCursor(0,1); lcd.print("3C v8 Booting...");

  loadThresholds();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lcd.clear();
  lcd.setCursor(0,0); lcd.print("Connecting WiFi ");
  lcd.setCursor(0,1); lcd.print(WIFI_SSID);
  Serial.printf("[3C] Connecting to %s\n", WIFI_SSID);
  uint8_t tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) { delay(500); tries++; }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[3C] WiFi OK  IP=%s\n", WiFi.localIP().toString().c_str());
    setRGB(0, 80, 0);
    lcd.clear();
    lcd.setCursor(0,0); lcd.print("WiFi OK         ");
    lcd.setCursor(0,1); lcd.print(WiFi.localIP().toString());
    setupOTA();                    // ← OTA ADDED (only when WiFi is up)
  } else {
    Serial.println("[3C][WARN] WiFi failed — ESP-NOW only");
    setRGB(255, 0, 0);
    lcd.clear();
    lcd.setCursor(0,0); lcd.print("WiFi FAILED     ");
    lcd.setCursor(0,1); lcd.print("ESP-NOW only    ");
  }
  delay(800);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[3C][FATAL] ESP-NOW init failed");
    lcd.clear(); lcd.setCursor(0,0); lcd.print("ESP-NOW FAILED  ");
    while (true) { setRGB(255,0,0); delay(200); setRGB(0,0,0); delay(200); }
  }
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onReceived);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, SLAVE_2B_MAC, 6);
  peer.channel = 0;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) == ESP_OK) {
    espNowReady = true;
    Serial.println("[3C] Peer 2B registered");
  } else {
    Serial.println("[3C][WARN] Failed to add 2B peer");
  }

  if (WiFi.status() == WL_CONNECTED) {
    delay(200); announceChannel();
  }

  if (espNowReady) { delay(300); send2BCmd(CMD_REQ_DATA); }

  if (WiFi.status() == WL_CONNECTED) {
    tgSend("*3C v8 online*  SSID: " + String(WIFI_SSID));
  }

  lcd.clear();
  lcd.setCursor(0,0); lcd.print("Waiting for 2B  ");
  lcd.setCursor(0,1); lcd.print("Blue = Setup    ");
  Serial.println("[3C] Boot complete — v8");
}

// ─── LOOP ─────────────────────────────────────────────────────────
uint32_t tLastLcd    = 0;
uint32_t tLastAuto   = 0;
uint32_t tLastLeds   = 0;
uint32_t tLastHb     = 0;
uint32_t tLastChanAnn = 0;
bool     wifiWasCon  = false;

void loop() {
  uint32_t now = millis();

  ArduinoOTA.handle();           // ← OTA ADDED (first thing in loop)

  // ── Consume ISR packet ──────────────────────────────────────────
  if (isrReady) {
    memcpy(&last2B, &isrBuf, sizeof(SensorPacket));
    got2BData = true;
    tLast2B   = now;
    isrReady  = false;
    Serial.printf("[3C←2B] T=%.1f  H=%.1f  CO2=%u  ERR=0x%02X\n",
                  last2B.temperature, last2B.humidity,
                  last2B.co2ppm, last2B.errorFlags);
  }

  handleButtons();

  if (!inSetMode && (now - tLastLcd) >= 500UL) {
    tLastLcd = now;
    lcdShowDefault();
  }

  if ((now - tLastAuto) >= 3000UL) {
    tLastAuto = now;
    runAutomation();
  }

  if ((now - tLastLeds) >= 2000UL) {
    tLastLeds = now;
    updateStatusLEDs();
    if (!inSetMode && (rgbState == RGB_IDLE ||
        (now - tRgbPulse) > RGB_PULSE_MS)) {
      updateRGBIdle();
    }
  }

  if (!inSetMode && (rgbState == RGB_RX || rgbState == RGB_TX) &&
      (now - tRgbPulse) > RGB_PULSE_MS) {
    updateRGBIdle();
  }

  bool wifiNow = (WiFi.status() == WL_CONNECTED);
  if (wifiNow && !wifiWasCon) {
    Serial.println("[3C] WiFi reconnected — announcing channel");
    announceChannel();
    tgSend("*3C WiFi reconnected*");
  }
  if (!wifiNow) {
    static uint32_t tRetry = 0;
    if ((now - tRetry) >= 30000UL) { tRetry = now; WiFi.reconnect(); }
  }
  wifiWasCon = wifiNow;

  if (wifiNow && (now - tLastChanAnn) >= 30000UL) {
    tLastChanAnn = now;
    announceChannel();
  }

  if (espNowReady && (now - tLastHb) >= 10000UL) {
    tLastHb = now;
    send2BCmd(CMD_REQ_DATA);
  }

  if (wifiNow && (now - tLastTgPoll) >= TG_POLL_MS) {
    tLastTgPoll = now;
    pollTelegram();
  }

  delay(5);
}