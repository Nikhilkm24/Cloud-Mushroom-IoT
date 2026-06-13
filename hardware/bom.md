# Bill of Materials — Cloud Mushroom Operations Centre

> Complete component list for both nodes with specifications, roles, and sourcing notes.

---

## 3C — Master Controller Node (ESP32-S3 DevKitC)

| # | Component | Model / Spec | Qty | Role | Notes |
|---|-----------|-------------|-----|------|-------|
| 1 | MCU | ESP32-S3 DevKitC, Xtensa LX7 dual-core 240MHz, 4MB Flash | 1 | Master controller | USB-C, onboard RGB LED |
| 2 | LCD Display | 16×2 I2C LCD, PCF8574 backpack, addr 0x27 | 1 | Local UI display | SDA GPIO8, SCL GPIO9 |
| 3 | RGB LED | WS2812B NeoPixel, 5V, single LED | 1 | 18-state visual FSM | GPIO48, 300Ω series R |
| 4 | Red LED | Standard 5mm, 630nm | 1 | Alert indicator | GPIO12, 220Ω series R |
| 5 | Green LED | Standard 5mm, 525nm | 1 | System OK indicator | GPIO13, 220Ω series R |
| 6 | Yellow LED | Standard 5mm, 590nm | 1 | AUTO mode indicator | GPIO20, 220Ω series R |
| 7 | Blue Button | Momentary NO, 12mm | 1 | NEXT / Enter SET mode | GPIO6, INPUT_PULLUP |
| 8 | Red Button | Momentary NO, 12mm | 1 | Value UP in SET mode | GPIO7, INPUT_PULLUP |
| 9 | Black Button | Momentary NO, 12mm | 1 | Value DOWN / Cancel | GPIO15, INPUT_PULLUP |
| 10 | Mechanical Relay Module | Songle SLA-05VDC-SL-C, 5V coil, 10A/250VAC | 1 | Exhaust fan control | GPIO4, COM/NO wiring |
| 11 | Solid State Relay | Fotek SSR-100DA, 3–32V DC in, 24–380VAC | 1 | Humidifier AC control | GPIO5, galvanic isolation |
| 12 | Flyback Diode | 1N4007, 1A, 1000V | 2 | Relay coil protection | One per relay coil |
| 13 | Voltage Regulator | AMS1117-3.3, 800mA LDO, SOT-223 | 1 | 3.3V rail | With I/O decoupling caps |
| 14 | Electrolytic Cap | 100µF, 25V | 2 | Power decoupling | Input + output of LDO |
| 15 | Ceramic Cap | 100nF, 50V | 4 | HF decoupling | Near MCU VCC pins |
| 16 | Pull-up Resistors | 4.7kΩ, 1/4W | 2 | I2C SDA + SCL | To 3.3V rail |
| 17 | Series Resistors | 220Ω, 1/4W | 3 | LED current limiting | One per LED |
| 18 | NeoPixel Series R | 300Ω, 1/4W | 1 | NeoPixel DIN protection | |
| 19 | Perfboard | 7×9 cm zero PCB | 1 | Assembly substrate | |
| 20 | Acrylic Enclosure | 150×100×60mm, clear lid, 4× M3 standoffs | 1 | Master node housing | Cutouts for LCD, buttons, LEDs |
| 21 | Blade Fuse + Holder | 5A, 250V | 1 | Mains overcurrent protection | On AC live line |
| 22 | Screw Terminals | 2-pin, 5.08mm pitch | 4 | AC mains wiring | Never Dupont for mains |
| 23 | USB-C Power Supply | 5V 2A minimum | 1 | Node power | 5V 3A recommended |

---

## 2B — Sensor + Actuator Node (ESP32 DOIT DevKit V1)

| # | Component | Model / Spec | Qty | Role | Notes |
|---|-----------|-------------|-----|------|-------|
| 1 | MCU | ESP32 DOIT DevKit V1, Xtensa LX6 dual-core 240MHz, 4MB Flash | 1 | Sensor + actuator node | Micro-USB, onboard LED GPIO2 |
| 2 | Temp/Humidity Sensor | Sensirion SHT40, ±0.2°C, ±1.8%RH, I2C 0x44 | 1 | Primary environmental sensing | GPIO21 SDA, GPIO22 SCL |
| 3 | CO₂ Sensor | Winsen MH-Z19E, NDIR, 0–5000 ppm, ±50 ppm | 1 | CO₂ measurement | UART2, GPIO16 RX, GPIO17 TX |
| 4 | SSR (Humidifier) | Fotek SSR-100DA, 3–32V DC in, 24–380VAC | 1 | Humidifier AC control | GPIO12 ⚠️ strapping pin |
| 5 | Mechanical Relay | Songle SLA-05VDC-SL-C, 5V coil, 10A/250VAC | 3 | Fan / intake / cooler control | GPIO14, GPIO26, GPIO27 |
| 6 | Flyback Diode | 1N4007, 1A, 1000V | 4 | One per relay coil | |
| 7 | Voltage Regulator | AMS1117-3.3, 800mA LDO | 1 | 3.3V for sensors | |
| 8 | Electrolytic Cap | 100µF, 25V | 2 | Decoupling | |
| 9 | Ceramic Cap | 100nF, 50V | 4 | HF decoupling | |
| 10 | Pull-up Resistors | 4.7kΩ, 1/4W | 2 | I2C SDA + SCL | |
| 11 | GPIO12 Pull-down | 10kΩ, 1/4W | 1 | Boot-mode protection | Prevents GPIO12 HIGH at boot |
| 12 | RC Filter | 100Ω + 100nF | 1 | Relay transient filter | GPIO12 to relay IN |
| 13 | Perfboard | 7×9 cm zero PCB | 1 | Assembly substrate | |
| 14 | IP54 Enclosure | Plastic, ~120×80×50mm | 1 | Fruiting room protection | Moisture-resistant |
| 15 | Blade Fuse + Holder | 5A, 250V | 1 | Mains overcurrent protection | |
| 16 | Screw Terminals | 2-pin, 5.08mm pitch | 6 | AC mains wiring | |
| 17 | USB-A Power Supply | 5V 2A minimum | 1 | Node power | 5V 3A recommended |

---

## Sensor Specifications Summary

| Sensor | Parameter | Range | Accuracy | Interface | Address / Port |
|--------|-----------|-------|----------|-----------|----------------|
| Sensirion SHT40 | Temperature | -40 to +125°C | ±0.2°C | I2C | 0x44 |
| Sensirion SHT40 | Relative Humidity | 0–100% RH | ±1.8% RH | I2C | 0x44 |
| Winsen MH-Z19E | CO₂ concentration | 0–5000 ppm | ±50 ppm (±5%) | UART 9600 baud | UART2 |

---

## Power Budget

| Node | Load | Typical Current |
|------|------|----------------|
| 3C | ESP32-S3 (WiFi + ESP-NOW active) | 150 mA |
| 3C | 16×2 LCD backlight | 20 mA |
| 3C | NeoPixel (dim green idle) | 5 mA |
| 3C | Status LEDs (×3 active) | 15 mA |
| 3C | Relay coil (1× active) | 70 mA |
| **3C Total** | | **~260 mA** |
| 2B | ESP32 (ESP-NOW active) | 120 mA |
| 2B | SHT40 | 1 mA |
| 2B | MH-Z19E | 40 mA |
| 2B | Relay coils (4× worst case) | 280 mA |
| **2B Total** | | **~440 mA** |

> Use **5V 2A minimum** for both nodes. **5V 3A** recommended for 2B to handle all 4 relays firing simultaneously during stagger queue draining.

---

## Assembly Order (Zero PCB / Perfboard)

Follow this sequence to minimise rework:

1. **Power rails** — solder 5V and GND bus lines across the board first
2. **Voltage regulator** — AMS1117-3.3 with input/output decoupling capacitors
3. **Passives** — pull-up resistors, series resistors, RC filters, decoupling caps
4. **Socketed headers** — install all IC sockets and pin headers before inserting components
5. **Protection diodes** — 1N4007 flyback diodes across each relay coil
6. **Relay modules** — mechanical relays and SSR with screw terminal wiring
7. **Sensors** — SHT40 (I2C), then MH-Z19E (UART) connections
8. **Voltage verification** — measure all rails (5V, 3.3V) before connecting MCU
9. **MCU** — plug ESP32 into headers
10. **First boot test** — Serial Monitor verification before closing enclosure
