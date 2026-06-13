# System Architecture — Cloud Mushroom Operations Centre

## Design Philosophy

The system is built around a **clear separation of concerns** between the two nodes:

- **2B** is dumb-fast: reads sensors, fires relays on command, pushes data. No logic. No decisions.
- **3C** is smart-connected: runs all automation logic, cloud connectivity, UI, and OTA. Makes all decisions.

This separation means 2B can be extremely simple, reliable, and easy to replace. If 2B fails, you replace it and reflash — no configuration needed beyond MAC addresses. All intelligence, thresholds, and state live in 3C's NVS.

---

## Node Communication — ESP-NOW vs WiFi

| Property | ESP-NOW | WiFi (STA) |
|----------|---------|------------|
| Latency | ~1ms | 50–500ms (cloud round-trip) |
| Router dependency | None | Required |
| Range | ~200m LOS, ~50m indoor | Same as router coverage |
| Protocol | 802.11 MAC layer (no TCP/IP) | Full TCP/IP stack |
| Used for | 3C↔2B command and telemetry | 3C↔Telegram, Blynk, OTA |
| Packet size | Max 250 bytes | Unlimited |
| Encryption | None (within trusted LAN) | WPA2 |

The critical design decision: **automation control (3C→2B relay commands) never touches the internet.** If Telegram is down, if Blynk is down, if the router is down — the local automation loop between 3C and 2B continues operating indefinitely on the last received sensor data, until 2B link timeout (15s) suspends automation.

---

## Firmware State Machines

### 3C — Main Loop State

```
loop() every iteration:
  ├── ArduinoOTA.handle()           (check for OTA session)
  ├── if (WiFi disconnected) reconnect()
  ├── announceChannel() every 30s
  ├── handleButtons()               (SET mode FSM)
  ├── pollTelegram() every 3s       (getUpdates)
  ├── if (autoMode && got2BData) runAutomation() every 3s
  ├── updateLCD() every 3s          (rotate screens)
  ├── updateNeoPixel()              (FSM state → colour)
  ├── updateLEDs()                  (mirror NeoPixel alert state)
  └── if (cmdPending) applyCmd()    (process incoming ESP-NOW)
```

### 3C — NeoPixel Priority FSM

Higher priority states override lower ones:

```
Priority 1 (highest): OTA active           → CYAN steady
Priority 2:           SET mode active      → CYAN steady
Priority 3:           WiFi lost            → RED blink
Priority 4:           2B link lost (>15s)  → RED steady
Priority 5:           CO2 + humidity alert → ORANGE steady
Priority 6:           CO2 alert only       → YELLOW steady
Priority 7:           RH low              → LIGHT BLUE steady
Priority 8:           RH high             → PURPLE steady
Priority 9:           All OK, MANUAL mode → DIM CYAN steady
Priority 10 (lowest): All OK, AUTO mode   → DIM GREEN steady
Overlay events:       Packet received     → BRIGHT GREEN pulse 350ms
                      Command sent        → BLUE-WHITE pulse 350ms
```

### 2B — Boot FSM

```
Power ON
  ↓
Brownout detector disabled
  ↓
GPIO init (all relays OFF)
  ↓
WiFi init (no router — STA for ESP-NOW only)
  ↓
discoverChannel():
  RTC channel valid?
    YES → probe that channel (400ms)
      Reply? → LOCK → save to RTC → proceed
      No reply? → fall through to full scan
    NO  → full scan ch1–13
      For each ch: probe → wait 400ms → reply? → LOCK
      No lock found → use rtcChannel or ch.1 default
  ↓
esp_now_init()
esp_now_register_recv_cb(onReceived)
esp_now_register_send_cb(onSent)
  ↓
Register 3C as peer (MASTER_MAC)
  ↓
loop() begins:
  SHT40 read every 3s
  MH-Z19E read every 5s
  pushData() every 5s
  applyPendingRelay() on cmdPending
  LED blink heartbeat
```

---

## Data Flow

```
[SHT40] ──I2C──────────────────────────────────────────────────────────►┐
[MH-Z19E] ──UART──────────────────────────────────────────────────────►┤
                                                                         │
                                                               [2B ESP32]│
                                                                         │
                                                               pushData()│
                                                                         │
                                                     SensorPacket (21B) ▼
                                               ESP-NOW ──────────────────►
                                                                         │
                                                               [3C ESP32-S3]
                                                                    │
                                        ┌──────────────────────────┤
                                        │                          │
                                   runAutomation()            updateLCD()
                                        │                     updateNeoPixel()
                              CmdPacket (6B)               updateLEDs()
                              ESP-NOW ◄──┘               pollTelegram()
                                        │
                                   [2B ESP32]
                               applyPendingRelay()
                                    │  │  │  │
                                   SSR Fan In Cooler
```

---

## NVS Persistence Schema

3C stores all user-configurable thresholds in NVS (ESP32 Non-Volatile Storage via `Preferences` library). These survive reboots and power cuts.

| NVS Key | Type | Default | Description |
|---------|------|---------|-------------|
| `humHigh` | float | 85.0 | Humidity upper threshold (%) |
| `humLow` | float | 75.0 | Humidity lower threshold (%) |
| `co2Limit` | uint16_t | 1200 | CO₂ limit (ppm) |

Values are loaded at boot before automation begins. Updates via SET mode buttons or Telegram `/set` command write immediately to NVS.

---

## ESP-NOW + WiFi STA Coexistence — Technical Detail

This is the most architecturally significant constraint in the system.

The ESP32 radio (a single 2.4GHz transceiver) can run in STA+ESP-NOW mode simultaneously, but both must operate on the **same channel** — because it is literally the same hardware radio.

When STA connects to a router, the radio locks to the router's beacon channel. There is no negotiation — STA ownership of the channel is absolute. Any attempt to change the channel via `esp_wifi_set_channel()` after STA connects is silently overridden by the STA driver during the next beacon synchronization.

Therefore:
1. **3C never fights the STA driver.** After WiFi connects, it reads the actual channel via `esp_wifi_get_channel()` and announces it to 2B.
2. **3C's peer for 2B uses `channel = 0`** — this means "follow the current radio channel automatically." It is always correct regardless of what the router does.
3. **2B actively discovers 3C's channel on boot** via a probe scan — it does not assume a hardcoded channel.
4. **3C re-announces its channel every 30s** — so if the router changes channel (rare but possible), 2B re-synchronizes within 30s without rebooting.

This architecture makes the channel management fully automatic and resilient to router reboots, channel changes, and 2B reboots.
