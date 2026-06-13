# Telegram Bot — Complete Command Reference
## Cloud Mushroom Operations Centre

The Telegram bot provides full remote monitoring and control of the system from any smartphone or computer. All commands are plain text — no additional app required beyond Telegram.

---

## Setup

1. Open Telegram and find the Cloud Mushroom bot (saved link from installation)
2. Send `/start` — receive welcome message confirming 3C is online
3. 3C sends an automatic startup message every time it reboots: `"3C v9 online | SSID: Sharmila"`

---

## Information Commands

| Command | Response |
|---------|----------|
| `/start` | Welcome message + confirms 3C online |
| `/help` | Full command list printed in chat |
| `/status` | Live T, H, CO₂, relay states, mode, 2B link |
| `/thresholds` | Current humHigh, humLow, CO₂ limit values |
| `/uptime` | Runtime since last reboot (3C and 2B) |
| `/link` | 2B link status, last packet age, error flags, CO₂ warm-up state |
| `/rgb` | NeoPixel colour guide printed in chat |

---

## Relay Control Commands

> Sending any relay command **automatically switches to MANUAL mode**. Send `/auto` to re-enable automation.

| Command | Action |
|---------|--------|
| `/fan on` | Exhaust fan ON → switches to MANUAL |
| `/fan off` | Exhaust fan OFF → switches to MANUAL |
| `/hum on` | Humidifier ON → switches to MANUAL |
| `/hum off` | Humidifier OFF → switches to MANUAL |
| `/all on` | Both fan AND humidifier ON (500ms stagger) |
| `/all off` | Both fan AND humidifier OFF (500ms stagger) |
| `/auto` | Switch to AUTO mode — automation engine resumes |
| `/manual` | Switch to MANUAL mode — automation paused |

---

## Threshold Commands

Remote threshold updates are saved to NVS immediately and survive reboots. No physical access needed.

| Command | Example | Valid Range | Effect |
|---------|---------|-------------|--------|
| `/set humhigh <val>` | `/set humhigh 88` | 51–99 (must be > humlow) | Set humidity upper threshold |
| `/set humlow <val>` | `/set humlow 72` | 30–95 (must be < humhigh) | Set humidity lower threshold |
| `/set co2 <val>` | `/set co2 1000` | 400–5000 | Set CO₂ alert limit |

---

## Example Bot Responses

### `/status`
```
--- Live Status ---
Temp : 16.5 C
Hum  : 91.3 %
CO2  : 896 ppm
Fan  : OFF
Hum  : ON
Mode : MANUAL
2B   : LIVE
ERR  : 0x00
```

### `/thresholds`
```
--- Thresholds ---
Hum High : 85 %
Hum Low  : 75 %
CO2 Limit: 1200 ppm
```

### `/link`
```
--- 2B Link ---
Status  : LIVE
Last pkt: 3s ago
CO2 warm: NO
Err flags: 0x00
2B uptime: 3842s
```

### `/set humhigh 88`
```
✅ Hum High set to 88%
Saved to NVS.
```

---

## Notes

- Telegram polling interval: every 3 seconds (getUpdates long-poll)
- Command response latency: ~3–6 seconds (polling interval + network)
- If 3C is offline (WiFi down), commands queue in Telegram and are processed when 3C reconnects
- Bot token and chat ID are stored in firmware — see `3C_OTA_V9.ino` for configuration
- Only the configured `chat_id` can control the system — unauthorized users receive no response
