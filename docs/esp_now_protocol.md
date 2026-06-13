# ESP-NOW Protocol — Cloud Mushroom Operations Centre

## What is ESP-NOW?

ESP-NOW is Espressif's proprietary peer-to-peer communication protocol built on the 802.11 MAC layer. Unlike WiFi, it does not use TCP/IP, DHCP, or a router. Devices communicate directly with each other using MAC addresses, with ~1ms latency and up to 250 bytes per packet.

Key properties relevant to this project:
- **No router hop** — commands from 3C reach 2B in ~1ms regardless of cloud connectivity
- **connectionless** — no session setup, no handshake, fire-and-forget with optional ACK
- **Co-exists with WiFi STA** — 3C uses both simultaneously (STA for cloud, ESP-NOW for 2B)
- **Range** — ~200m line-of-sight, ~30–50m through walls

---

## Channel Management — The Critical Constraint

See `docs/architecture.md` for full detail. Summary:

```
Router assigns channel → STA driver locks radio to that channel → ESP-NOW must match
peer.channel = 0 on 3C → peer automatically follows STA radio channel ✅
2B scans to discover channel → saves to RTC memory → survives reboots ✅
3C announces channel every 30s → 2B re-syncs if channel ever changes ✅
```

---

## Packet Reference

### SensorPacket — 2B → 3C (21 bytes)

Sent every 5 seconds and on demand (CMD_REQ_DATA).

```c
struct __attribute__((packed)) SensorPacket {
    uint8_t  nodeId;         // [0]     0x2B = sensor node identifier
    float    temperature;    // [1–4]   °C, IEEE 754 single precision
    float    humidity;       // [5–8]   %RH, IEEE 754 single precision
    uint16_t co2ppm;         // [9–10]  CO₂ in ppm, little-endian
    uint8_t  ssrOn;          // [11]    1=humidifier SSR active
    uint8_t  mechOn;         // [12]    1=exhaust fan relay active
    uint8_t  fanIntakeOn;    // [13]    1=intake fan relay active
    uint8_t  coolerOn;       // [14]    1=cooler relay active
    uint8_t  co2Preheating;  // [15]    1=MH-Z19E warm-up not complete
    uint8_t  errorFlags;     // [16]    bit0=SHT40 err, bit1=CO₂ err
    uint32_t uptimeSec;      // [17–20] seconds since 2B boot, little-endian
};
// sizeof = 21 bytes. Validated with static_assert on both nodes.
```

### CmdPacket — 3C → 2B (6 bytes)

Sent on automation triggers, Telegram commands, or manual override.

```c
struct __attribute__((packed)) CmdPacket {
    uint8_t targetId;  // [0]   0x2B = intended recipient
    uint8_t cmd;       // [1]   Command code (see table)
    float   param;     // [2–5] Optional parameter, IEEE 754
};
// sizeof = 6 bytes.
```

### Channel Probe — 2B → broadcast (2 bytes)

Used during channel discovery scan.

```c
uint8_t probe[2] = {0xC4, currentChannel};
// Sent to FF:FF:FF:FF:FF:FF broadcast MAC on each scanned channel
// 3C listens for 0xC4 magic byte and replies with {0xC4, confirmedChannel}
```

---

## Command Code Table

| Code | Name | Direction | Relay Action | Queue |
|------|------|-----------|-------------|-------|
| `0x10` | CMD_SSR_ON | 3C → 2B | Humidifier SSR ON | Yes, 1200ms stagger |
| `0x11` | CMD_SSR_OFF | 3C → 2B | Humidifier SSR OFF | Yes |
| `0x20` | CMD_MECH_ON | 3C → 2B | Exhaust fan relay ON | Yes |
| `0x21` | CMD_MECH_OFF | 3C → 2B | Exhaust fan relay OFF | Yes |
| `0x30` | CMD_FAN_ON | 3C → 2B | Intake fan relay ON | Yes |
| `0x31` | CMD_FAN_OFF | 3C → 2B | Intake fan relay OFF | Yes |
| `0x40` | CMD_COOL_ON | 3C → 2B | Cooler relay ON | Yes |
| `0x41` | CMD_COOL_OFF | 3C → 2B | Cooler relay OFF | Yes |
| `0x50` | CMD_REQ_DATA | 3C → 2B | Request immediate telemetry push | No relay |

---

## Timing Diagram

```
2B                                    3C
│                                     │
│──── SensorPacket (every 5s) ───────►│
│                                     │ runAutomation() evaluates
│◄─── CmdPacket (if action needed) ───│
│                                     │
│  applyPendingRelay()                │
│  (1200ms stagger)                   │
│──── SensorPacket (updated state) ──►│
│                                     │
│                                     │ pollTelegram() every 3s
│                                     │◄── /status from user ──
│                                     │─── response ──────────►
│                                     │
│◄──── announceChannel() every 30s ───│
│  (channel probe reply)              │
```

---

## Known ESP-NOW Limitations

| Limitation | Impact | Mitigation |
|-----------|--------|------------|
| Max 250 bytes/packet | Fine for current structs (21B/6B) | No issue |
| Max 20 peers per node | Only 2 nodes currently | Room for expansion |
| No encryption | Within private LAN — acceptable | Add PMK/LMK if needed in future |
| Same channel as STA | Complex channel management | Solved by probe + announce protocol |
| No guaranteed delivery | ACK optional, no retry by default | 3C detects 15s link loss and alerts |
| Callbacks run in WiFi task | No blocking inside callbacks | Deferred flag pattern used |
