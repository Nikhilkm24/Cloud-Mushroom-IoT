// ╔══════════════════════════════════════════════════════════════════╗
// ║   CLOUD MUSHROOM — 2B (Sensor Node)  v8                         ║
// ║   Hardware : ESP32 DOIT DevKit V1                               ║
// ╠══════════════════════════════════════════════════════════════════╣
// ║   WHAT'S NEW IN v8 (vs v7)                                      ║
// ║                                                                 ║
// ║   CHANNEL AUTO-DISCOVERY:                                       ║
// ║   2B no longer hard-codes a channel. On boot it scans all       ║
// ║   channels 1-13, sending a broadcast "channel probe" packet     ║
// ║   on each. 3C replies with its current channel. Once found,     ║
// ║   2B locks to that channel and saves it to RTC memory so a      ║
// ║   reboot does not re-scan. If 3C is not yet up (powered after   ║
// ║   2B), it falls back to last known channel or channel 1.        ║
// ║   3C also broadcasts its channel whenever its WiFi reconnects   ║
// ║   to a new AP, so 2B can re-lock dynamically.                   ║
// ║                                                                 ║
// ║   ALL v7 features retained (SHT40, MH-Z19E, relay stagger,     ║
// ║   4-relay queue, error flags, brownout disable).                ║
// ╠══════════════════════════════════════════════════════════════════╣
// ║   PIN MAP                                                        ║
// ║   GPIO12 → SSR relay  (humidifier)    [strapping pin!]          ║
// ║   GPIO14 → Mech relay (exhaust fan)                             ║
// ║   GPIO26 → Intake fan relay                                     ║
// ║   GPIO27 → Cooler relay                                         ║
// ║   GPIO16 ← MH-Z19E TX (UART2 RX)                               ║
// ║   GPIO17 → MH-Z19E RX (UART2 TX)                               ║
// ║   GPIO21 ↔ SHT40 SDA (I2C)                                     ║
// ║   GPIO22 → SHT40 SCL (I2C)                                      ║
// ║   GPIO2  → Onboard LED                                          ║
// ╠══════════════════════════════════════════════════════════════════╣
// ║   LIBRARIES: Sensirion I2C SHT4x (by Sensirion AG)             ║
// ║   Board: ESP32 Dev Module  Upload: 921600  Flash: 80MHz          ║
// ╚══════════════════════════════════════════════════════════════════╝

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Wire.h>
#include "SensirionI2cSht4x.h"
#include "esp_system.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ─── USER CONFIG ──────────────────────────────────────────────────
uint8_t MASTER_MAC[6] = {0x1C, 0xDB, 0xD4, 0x46, 0x22, 0x88};

// Channel scan: tries channels 1-13 for 400ms each looking for 3C
#define CHAN_SCAN_TIMEOUT_MS   400
#define CHAN_SCAN_RETRIES       2    // how many full scans before giving up

#define SHT40_INTERVAL_MS    3000UL
#define CO2_INTERVAL_MS      5000UL
#define ESPNOW_INTERVAL_MS   5000UL
#define CO2_PREHEAT_MS          0UL
#define RELAY_STAGGER_MS     1200UL
// ─── END USER CONFIG ──────────────────────────────────────────────

// ─── CHANNEL PACKET MAGIC ─────────────────────────────────────────
// 2-byte channel probe sent on each channel during scan:  {0xC4, channel}
// 3C replies with the same 2-byte format to confirm its channel.
// This mirrors the logic described in the project brief.
#define CHAN_MAGIC  0xC4

// RTC memory survives deep sleep and soft resets — persists last good channel
RTC_DATA_ATTR uint8_t rtcChannel = 0;   // 0 = unset

// ─── PINS ─────────────────────────────────────────────────────────
#define PIN_SSR      12
#define PIN_MECH     14
#define PIN_FAN      26
#define PIN_COOLER   27
#define PIN_LED       2
#define PIN_CO2_RX   16
#define PIN_CO2_TX   17
#define CO2_BAUD   9600

// ─── CMD CODES ────────────────────────────────────────────────────
#define CMD_SSR_ON    0x10
#define CMD_SSR_OFF   0x11
#define CMD_MECH_ON   0x20
#define CMD_MECH_OFF  0x21
#define CMD_FAN_ON    0x30
#define CMD_FAN_OFF   0x31
#define CMD_COOL_ON   0x40
#define CMD_COOL_OFF  0x41
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

// ─── STATE ────────────────────────────────────────────────────────
SensirionI2cSht4x sht40;
HardwareSerial    co2Serial(2);
SensorPacket      txPkt = {};

bool     ssrOn         = false;
bool     mechOn        = false;
bool     fanIntakeOn   = false;
bool     coolerOn      = false;
bool     co2Preheating = (CO2_PREHEAT_MS > 0);
bool     espNowReady   = false;
uint8_t  errorFlags    = 0x00;
uint8_t  lockedChannel = 0;

float    lastTemp     = 0.0f;
float    lastHumidity = 0.0f;
uint16_t lastCO2      = 0;

uint32_t tLastSHT40    = 0;
uint32_t tLastCO2      = 0;
uint32_t tLastSend     = 0;
uint32_t tPreheatStart = 0;

// Channel scan state
volatile bool chanReplyReceived = false;
volatile uint8_t chanReplyVal   = 0;

// ─── RELAY STAGGER QUEUE ──────────────────────────────────────────
enum RelayPending {
  REL_NONE,
  REL_SSR_ON,  REL_SSR_OFF,
  REL_MECH_ON, REL_MECH_OFF,
  REL_FAN_ON,  REL_FAN_OFF,
  REL_COOL_ON, REL_COOL_OFF
};

#define RELAY_QUEUE_SIZE 8
RelayPending relayQueue[RELAY_QUEUE_SIZE] = {};
uint8_t      qHead = 0, qTail = 0;
uint32_t     tLastRelayFired = 0;

void enqueueRelay(RelayPending action) {
  uint8_t next = (qTail + 1) % RELAY_QUEUE_SIZE;
  if (next == qHead) { Serial.println("[2B][WARN] Relay queue full"); return; }
  relayQueue[qTail] = action;
  qTail = next;
}

void applyPendingRelay() {
  if (qHead == qTail) return;
  uint32_t now = millis();
  if (tLastRelayFired > 0 && (now - tLastRelayFired) < RELAY_STAGGER_MS) return;

  RelayPending action = relayQueue[qHead];
  qHead = (qHead + 1) % RELAY_QUEUE_SIZE;

  switch (action) {
    case REL_SSR_ON:
      if (!ssrOn)       { ssrOn = true;       digitalWrite(PIN_SSR,    HIGH); Serial.println("[2B] SSR ON"); }  break;
    case REL_SSR_OFF:
      if (ssrOn)        { ssrOn = false;       digitalWrite(PIN_SSR,    LOW);  Serial.println("[2B] SSR OFF"); } break;
    case REL_MECH_ON:
      if (!mechOn)      { mechOn = true;       digitalWrite(PIN_MECH,   HIGH); Serial.println("[2B] ExhaustFan ON"); }  break;
    case REL_MECH_OFF:
      if (mechOn)       { mechOn = false;      digitalWrite(PIN_MECH,   LOW);  Serial.println("[2B] ExhaustFan OFF"); } break;
    case REL_FAN_ON:
      if (!fanIntakeOn) { fanIntakeOn = true;  digitalWrite(PIN_FAN,    HIGH); Serial.println("[2B] IntakeFan ON"); }   break;
    case REL_FAN_OFF:
      if (fanIntakeOn)  { fanIntakeOn = false; digitalWrite(PIN_FAN,    LOW);  Serial.println("[2B] IntakeFan OFF"); }  break;
    case REL_COOL_ON:
      if (!coolerOn)    { coolerOn = true;     digitalWrite(PIN_COOLER, HIGH); Serial.println("[2B] Cooler ON"); }      break;
    case REL_COOL_OFF:
      if (coolerOn)     { coolerOn = false;    digitalWrite(PIN_COOLER, LOW);  Serial.println("[2B] Cooler OFF"); }     break;
    default: break;
  }
  tLastRelayFired = now;
}

// ─── ISR DEFERRED CMD ─────────────────────────────────────────────
volatile bool cmdPending   = false;
CmdPacket     cmdBuf       = {};
volatile bool ledBlinkFlag = false;

// ─── HELPERS ──────────────────────────────────────────────────────
void disableBrownout() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
}

void hardResetRelays() {
  for (int pin : {PIN_SSR, PIN_MECH, PIN_FAN, PIN_COOLER}) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
}

void blinkLED(uint8_t n, uint16_t ms) {
  for (uint8_t i = 0; i < n; i++) {
    digitalWrite(PIN_LED, HIGH); delay(ms);
    digitalWrite(PIN_LED, LOW);  delay(ms);
  }
}

// ─── ESP-NOW CALLBACKS ────────────────────────────────────────────
void onSent(const wifi_tx_info_t *txInfo, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) ledBlinkFlag = true;
}

void onReceived(const esp_now_recv_info_t *recvInfo, const uint8_t *data, int len) {
  // ── Channel reply from 3C (2 bytes: magic + channel)
  if (len == 2 && data[0] == CHAN_MAGIC) {
    chanReplyVal      = data[1];
    chanReplyReceived = true;
    return;
  }

  // ── Command packet from 3C
  if (len == sizeof(CmdPacket)) {
    if (!cmdPending) {
      memcpy(&cmdBuf, data, sizeof(CmdPacket));
      cmdPending = true;
    }
    return;
  }
}

// ─── CHANNEL DISCOVERY ────────────────────────────────────────────
// Scans channels 1-13 sending a 2-byte probe {CHAN_MAGIC, ch} as broadcast.
// Waits CHAN_SCAN_TIMEOUT_MS for a reply from 3C.
// Returns the confirmed channel, or 0 if 3C not found.
bool addBroadcastPeer() {
  uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  if (esp_now_is_peer_exist(bcast)) return true;
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, bcast, 6);
  p.channel = 0;
  p.encrypt = false;
  return (esp_now_add_peer(&p) == ESP_OK);
}

uint8_t discoverChannel() {
  uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  addBroadcastPeer();

  for (int retry = 0; retry < CHAN_SCAN_RETRIES; retry++) {
    for (uint8_t ch = 1; ch <= 13; ch++) {
      Serial.printf("[2B] Scanning ch=%d\n", ch);
      esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

      // Update broadcast peer channel
      esp_now_peer_info_t p = {};
      if (esp_now_get_peer(bcast, &p) == ESP_OK) {
        p.channel = 0;  // 0 = current channel
        esp_now_mod_peer(&p);
      }

      uint8_t probe[2] = {CHAN_MAGIC, ch};
      chanReplyReceived = false;
      esp_now_send(bcast, probe, 2);

      uint32_t t0 = millis();
      while (!chanReplyReceived && (millis() - t0) < (uint32_t)CHAN_SCAN_TIMEOUT_MS) {
        delay(10);
      }

      if (chanReplyReceived) {
        Serial.printf("[2B] 3C found on ch=%d (reply ch=%d)\n", ch, chanReplyVal);
        return ch;
      }
    }
  }
  return 0; // not found
}

void lockChannel(uint8_t ch) {
  lockedChannel = ch;
  rtcChannel    = ch;

  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

  // Update 3C peer
  esp_now_peer_info_t p = {};
  if (esp_now_get_peer(MASTER_MAC, &p) == ESP_OK) {
    p.channel = ch;
    esp_now_mod_peer(&p);
  } else {
    memcpy(p.peer_addr, MASTER_MAC, 6);
    p.channel = ch;
    p.encrypt = false;
    esp_now_add_peer(&p);
  }
  Serial.printf("[2B] Locked to ch=%d\n", ch);
}

// ─── SENSOR FUNCTIONS ─────────────────────────────────────────────
bool readSHT40(float &t, float &h) {
  return (sht40.measureHighPrecision(t, h) == 0);
}

static const uint8_t CO2_CMD[9] =
  {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};

uint8_t co2Checksum(const uint8_t *buf) {
  uint8_t s = 0;
  for (uint8_t i = 1; i < 8; i++) s += buf[i];
  return (~s) + 1;
}

uint16_t readCO2() {
  while (co2Serial.available()) co2Serial.read();
  co2Serial.write(CO2_CMD, 9);
  uint32_t t0 = millis();
  while (co2Serial.available() < 9) {
    if ((millis() - t0) > 500UL) { Serial.println("[2B][CO2] timeout"); return 0; }
    delay(5);
  }
  uint8_t buf[9];
  co2Serial.readBytes(buf, 9);
  if (buf[0] != 0xFF || buf[1] != 0x86) return 0;
  if (buf[8] != co2Checksum(buf)) return 0;
  return ((uint16_t)buf[2] << 8) | buf[3];
}

// ─── ESP-NOW PUSH ─────────────────────────────────────────────────
void pushData() {
  txPkt.nodeId        = 0x2B;
  txPkt.temperature   = lastTemp;
  txPkt.humidity      = lastHumidity;
  txPkt.co2ppm        = lastCO2;
  txPkt.ssrOn         = ssrOn         ? 1 : 0;
  txPkt.mechOn        = mechOn        ? 1 : 0;
  txPkt.fanIntakeOn   = fanIntakeOn   ? 1 : 0;
  txPkt.coolerOn      = coolerOn      ? 1 : 0;
  txPkt.co2Preheating = co2Preheating ? 1 : 0;
  txPkt.errorFlags    = errorFlags;
  txPkt.uptimeSec     = millis() / 1000UL;

  esp_err_t r = esp_now_send(MASTER_MAC, (const uint8_t*)&txPkt, sizeof(SensorPacket));
  if (r != ESP_OK) {
    Serial.printf("[2B][WARN] esp_now_send err=0x%X\n", r);
    return;
  }
  Serial.printf("[2B]→3C  T=%.1f°C H=%.1f%% CO2=%u SSR=%d MCH=%d FAN=%d COOL=%d UP=%lus\n",
                txPkt.temperature, txPkt.humidity, txPkt.co2ppm,
                txPkt.ssrOn, txPkt.mechOn, txPkt.fanIntakeOn,
                txPkt.coolerOn, (unsigned long)txPkt.uptimeSec);
}

// ─── COMMAND HANDLER ──────────────────────────────────────────────
void handleCmd(const CmdPacket &c) {
  Serial.printf("[2B] CMD 0x%02X\n", c.cmd);
  switch (c.cmd) {
    case CMD_SSR_ON:   enqueueRelay(REL_SSR_ON);   break;
    case CMD_SSR_OFF:  enqueueRelay(REL_SSR_OFF);  break;
    case CMD_MECH_ON:  enqueueRelay(REL_MECH_ON);  break;
    case CMD_MECH_OFF: enqueueRelay(REL_MECH_OFF); break;
    case CMD_FAN_ON:   enqueueRelay(REL_FAN_ON);   break;
    case CMD_FAN_OFF:  enqueueRelay(REL_FAN_OFF);  break;
    case CMD_COOL_ON:  enqueueRelay(REL_COOL_ON);  break;
    case CMD_COOL_OFF: enqueueRelay(REL_COOL_OFF); break;
    case CMD_REQ_DATA: pushData();                  break;
    default:
      Serial.printf("[2B][WARN] Unknown CMD 0x%02X\n", c.cmd);
  }
}

// ─── SETUP ────────────────────────────────────────────────────────
void setup() {
  disableBrownout();
  hardResetRelays();

  Serial.begin(115200);
  delay(400);
  Serial.println("\n================================");
  Serial.println(" Cloud Mushroom 2B  v8");
  Serial.println("================================");

  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("[2B] Reset: %d\n", reason);

  // SHT40
  Wire.begin(21, 22);
  sht40.begin(Wire, SHT40_I2C_ADDR_44);
  float t, h;
  if (readSHT40(t, h)) {
    lastTemp = t; lastHumidity = h;
    Serial.printf("[2B] SHT40 OK — T=%.2f°C  H=%.2f%%\n", t, h);
  } else {
    errorFlags |= 0x01;
    Serial.println("[2B][WARN] SHT40 not responding");
  }

  // MH-Z19E CO2
  co2Serial.begin(CO2_BAUD, SERIAL_8N1, PIN_CO2_RX, PIN_CO2_TX);
  tPreheatStart = millis();
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  // WiFi — STA, no router connection needed
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  Serial.printf("[2B] MAC=%s\n", WiFi.macAddress().c_str());

  // ESP-NOW init first (needed for channel scan)
  if (esp_now_init() != ESP_OK) {
    Serial.println("[2B][FATAL] ESP-NOW init failed — halting");
    while (true) { blinkLED(5, 100); delay(500); }
  }
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onReceived);

  // ── Channel discovery ─────────────────────────────────────────
  uint8_t ch = 0;

  // 1. Try RTC-saved channel first (fast path after reboot)
  if (rtcChannel >= 1 && rtcChannel <= 13) {
    Serial.printf("[2B] Trying saved ch=%d first\n", rtcChannel);
    esp_wifi_set_channel(rtcChannel, WIFI_SECOND_CHAN_NONE);
    addBroadcastPeer();
    uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    uint8_t probe[2] = {CHAN_MAGIC, rtcChannel};
    chanReplyReceived = false;
    esp_now_send(bcast, probe, 2);
    uint32_t t0 = millis();
    while (!chanReplyReceived && (millis() - t0) < 600UL) delay(10);
    if (chanReplyReceived) ch = rtcChannel;
  }

  // 2. Full scan if RTC channel didn't work
  if (ch == 0) {
    Serial.println("[2B] Starting full channel scan...");
    ch = discoverChannel();
  }

  // 3. Fallback: use saved channel even without confirmation
  if (ch == 0 && rtcChannel >= 1 && rtcChannel <= 13) {
    ch = rtcChannel;
    Serial.printf("[2B] 3C not found — using last saved ch=%d (3C may not be up yet)\n", ch);
  }

  // 4. Last resort: channel 1
  if (ch == 0) {
    ch = 1;
    Serial.println("[2B] No channel found — defaulting to ch=1. Start 3C first next time.");
  }

  lockChannel(ch);
  espNowReady = true;

  blinkLED(3, 150);
  Serial.printf("[2B] Boot complete — v8 — ch=%d\n", ch);
}

// ─── LOOP ─────────────────────────────────────────────────────────
void loop() {
  uint32_t now = millis();

  if (ledBlinkFlag) {
    ledBlinkFlag = false;
    digitalWrite(PIN_LED, HIGH); delay(30); digitalWrite(PIN_LED, LOW);
  }

  if (cmdPending) {
    cmdPending = false;
    handleCmd(cmdBuf);
  }

  applyPendingRelay();

  if (co2Preheating && (now - tPreheatStart) >= CO2_PREHEAT_MS) {
    co2Preheating = false;
    Serial.println("[2B] CO2 preheat complete");
    blinkLED(2, 80);
  }

  if ((now - tLastSHT40) >= SHT40_INTERVAL_MS) {
    tLastSHT40 = now;
    float temp, hum;
    if (readSHT40(temp, hum)) {
      lastTemp = temp; lastHumidity = hum;
      errorFlags &= ~0x01;
      Serial.printf("[2B] SHT40  T=%.2f°C  H=%.2f%%\n", temp, hum);
    } else {
      errorFlags |= 0x01;
      Serial.println("[2B][WARN] SHT40 read failed");
    }
  }

  if (!co2Preheating && (now - tLastCO2) >= CO2_INTERVAL_MS) {
    tLastCO2 = now;
    uint16_t ppm = readCO2();
    if (ppm > 0 && ppm < 6001) {
      lastCO2 = ppm;
      errorFlags &= ~0x02;
      Serial.printf("[2B] CO2   %u ppm\n", ppm);
    } else {
      errorFlags |= 0x02;
      Serial.printf("[2B][WARN] CO2 bad: %u\n", ppm);
    }
  }

  if (espNowReady && (now - tLastSend) >= ESPNOW_INTERVAL_MS) {
    tLastSend = now;
    pushData();
  }
}
