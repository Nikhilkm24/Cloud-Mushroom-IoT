# OTA Firmware Update Guide — 3C Master Controller

## Overview

3C supports wireless firmware updates via **ArduinoOTA** — after the initial USB flash, all future firmware updates can be deployed over WiFi without physical access to the controller or USB cable.

> **2B does NOT support OTA.** 2B must always be reflashed via USB.

---

## Requirements

- Arduino IDE installed on a laptop/PC
- Laptop connected to the **same WiFi network** as 3C ("Sharmila")
- 3C powered on and running (NeoPixel dim green or dim cyan = ready)

---

## Step-by-Step OTA Update

### 1. Open the firmware file
Open `firmware/3C_Master/3C_OTA_V9.ino` in Arduino IDE.

### 2. Select board settings
- **Board:** `ESP32S3 Dev Module`
- **Flash Mode:** QIO 80MHz
- **Flash Size:** 4MB
- **Upload Speed:** 921600

### 3. Select network port
Go to `Tools → Port`. You should see:

```
cloud-mushroom-3c at 192.168.x.x (ESP32-S3 Dev Module)
```

Select this **network port** (not a COM/tty port).

> If the network port doesn't appear: verify 3C is connected to WiFi (NeoPixel not red-blinking), verify your laptop is on the same WiFi network, and wait ~30 seconds for mDNS to resolve.

### 4. Upload

Click **Upload** (→ arrow). Arduino IDE will prompt:

```
Password for OTA update:
```

Enter: `ota1234`

### 5. Monitor progress

- Arduino IDE upload bar shows OTA progress percentage
- **3C LCD** shows: `OTA: XX%`
- **NeoPixel** turns solid CYAN during the entire flash session
- When complete, LCD shows: `OTA Done! Rebooting...`
- 3C automatically reboots and resumes normal operation

---

## During OTA — What Happens to the System

| Subsystem | Behaviour During OTA |
|-----------|---------------------|
| Automation | **Suspended** — relays hold last state |
| Telegram bot | **Not responding** — polling paused |
| ESP-NOW (2B link) | **Active** — 2B continues sensing but commands not processed |
| LCD | Shows OTA progress percentage |
| NeoPixel | Solid CYAN |
| Status LEDs | Hold last state |

**Total OTA time:** approximately 20–40 seconds for a typical firmware update.

---

## Safety Rules

> ⚠️ **NEVER cut power during OTA.** If power is lost mid-flash, the ESP32-S3 partition table may become corrupted and the device may enter a bootloop, requiring USB recovery flash.

> ⚠️ **Do not trigger OTA during active humidification.** The automation engine suspends during OTA — if the humidifier is ON when OTA starts, it will remain ON until 3C reboots and resumes automation. For safety, send `/manual` then `/hum off` via Telegram before initiating OTA.

> ⚠️ **OTA only works when 3C is on the same WiFi network.** Ensure the network port is visible in Arduino IDE before uploading.

---

## Troubleshooting OTA

| Problem | Likely Cause | Solution |
|---------|-------------|----------|
| Network port not visible | 3C not on WiFi, or mDNS not resolving | Verify 3C WiFi (NeoPixel not red-blinking); wait 30s; restart Arduino IDE |
| "Wrong password" error | Incorrect OTA password | Password is `ota1234` |
| Upload starts then fails mid-way | WiFi interference or signal drop | Move closer to router; reduce WiFi load |
| 3C enters bootloop after OTA | Power cut during flash | Reflash via USB: connect USB-C, select COM port, flash normally |
| OTA completes but firmware seems unchanged | Binary not rebuilt | Clean + rebuild in Arduino IDE before uploading |

---

## Recovery — USB Reflash (if OTA fails)

If 3C does not recover after a failed OTA:

1. Connect USB-C cable from 3C to laptop
2. In Arduino IDE: `Tools → Port` → select the COM/tty port
3. Hold the **BOOT** button on ESP32-S3, then press **RESET** (or just hold BOOT while pressing upload)
4. Upload normally at 921600 baud
5. After flash completes, press RESET to reboot normally
