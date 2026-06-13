# Wiring Notes — Cloud Mushroom Operations Centre

> Pin maps, wiring guidance, and safety requirements for both nodes.

---

## ⚠️ Safety First — AC Mains Wiring Rules

1. **All AC mains connections must use screw terminals** — never Dupont connectors for mains voltage
2. **Fuse the live (L) line** before it reaches any relay or SSR
3. **Relay COM/NO** switches the live line — neutral passes through directly to load
4. **Never work on mains wiring while powered** — discharge capacitors and verify with multimeter before touching
5. **1N4007 flyback diodes** must be fitted across every mechanical relay coil before powering
6. **SSR provides galvanic isolation** — but the output side is live mains; treat accordingly
7. **Verify with multimeter** at every wiring stage before connecting next component

---

## 3C Master — GPIO Assignment

| GPIO | Direction | Connected To | Notes |
|------|-----------|-------------|-------|
| GPIO4 | Output | Exhaust fan relay IN | Active HIGH |
| GPIO5 | Output | Humidifier SSR IN+ | Active HIGH |
| GPIO6 | Input | Blue button → GND | `INPUT_PULLUP`, active LOW |
| GPIO7 | Input | Red button → GND | `INPUT_PULLUP`, active LOW, hold-to-repeat |
| GPIO8 | I2C SDA | LCD 0x27 SDA | 4.7kΩ pull-up to 3.3V |
| GPIO9 | I2C SCL | LCD 0x27 SCL | 4.7kΩ pull-up to 3.3V |
| GPIO12 | Output | Red LED → 220Ω → GND | Active HIGH |
| GPIO13 | Output | Green LED → 220Ω → GND | Active HIGH |
| GPIO15 | Input | Black button → GND | `INPUT_PULLUP`, active LOW |
| GPIO20 | Output | Yellow LED → 220Ω → GND | Active HIGH |
| GPIO48 | Output | NeoPixel DIN | 300Ω series resistor |
| 3.3V | Power | LCD VCC, pull-ups | Via AMS1117-3.3 |
| 5V | Power | Relay VCC | From USB supply |
| GND | Ground | All components | Common ground |

---

## 2B Slave — GPIO Assignment

| GPIO | Direction | Connected To | Notes |
|------|-----------|-------------|-------|
| GPIO2 | Output | Onboard LED | Active HIGH — status blink |
| GPIO12 | Output | SSR humidifier IN | Active HIGH ⚠️ strapping pin — add 10kΩ pull-down |
| GPIO14 | Output | Exhaust fan relay IN | Active HIGH |
| GPIO16 | UART2 RX | MH-Z19E TX | CO₂ sensor, 9600 baud |
| GPIO17 | UART2 TX | MH-Z19E RX | CO₂ sensor, 9600 baud |
| GPIO21 | I2C SDA | SHT40 SDA | 4.7kΩ pull-up to 3.3V |
| GPIO22 | I2C SCL | SHT40 SCL | 4.7kΩ pull-up to 3.3V |
| GPIO26 | Output | Intake fan relay IN | Active HIGH |
| GPIO27 | Output | Cooler relay IN | Active HIGH |
| 3.3V | Power | SHT40 VDD | Via AMS1117-3.3 |
| 5V | Power | Relay modules VCC, MH-Z19E VIN | From USB supply |
| GND | Ground | All components | |

> ⚠️ **GPIO34, GPIO35 on ESP32 DOIT are input-only** — cannot drive relay outputs  
> ⚠️ **GPIO12 strapping pin** — must be LOW at boot; add 10kΩ pull-down to GND  
> ⚠️ **ADC2 on ESP32-S3** — unavailable when WiFi active; use ADC1 pins only on 3C

---

## AC Relay Wiring

### SSR (Solid State Relay) — Humidifier

```
3.3V GPIO ──[300Ω optional]──► SSR DC IN+
GND ─────────────────────────► SSR DC IN−
                                 SSR AC OUT1 ──► Live (L) mains → Humidifier Live
                                 SSR AC OUT2 ──► Humidifier Neutral return
Neutral (N) mains ───────────────────────────► Humidifier Neutral
```

### Mechanical Relay — Fan / Cooler

```
5V GPIO ──► Relay module IN (optocoupler input)
VCC (5V) ──► Relay module VCC
GND ───────► Relay module GND
              Relay COM ──► Live (L) mains
              Relay NO  ──► Load (fan) Live terminal
              Load Neutral ──► Neutral (N) mains
```

### Flyback Diode — Bare Relay Coil

```
VCC (+5V) ──────────────────┬──────────────►  Relay coil (+)
                             │
                          1N4007
                       (cathode at VCC side,
                        anode at GPIO side)
                             │
GPIO (via transistor) ───────┴──────────────►  Relay coil (−)
```

> Pre-built relay modules (blue PCB type) typically include the flyback diode internally. Verify before assuming protection is present.

---

## I2C Bus — 3C

```
ESP32-S3
GPIO8 (SDA) ──[4.7kΩ]──► 3.3V
            ├──────────────► LCD 0x27 SDA
            ├──────────────► DS3231 RTC SDA  (if fitted, 0x68)
            └──────────────► AT24C32 EEPROM SDA (if fitted, 0x57)

GPIO9 (SCL) ──[4.7kΩ]──► 3.3V
            ├──────────────► LCD 0x27 SCL
            ├──────────────► DS3231 RTC SCL
            └──────────────► AT24C32 EEPROM SCL
```

---

## I2C Bus — 2B

```
ESP32 DOIT
GPIO21 (SDA) ──[4.7kΩ]──► 3.3V
             └──────────────► SHT40 SDA (addr 0x44)

GPIO22 (SCL) ──[4.7kΩ]──► 3.3V
             └──────────────► SHT40 SCL
```

---

## MH-Z19E CO₂ Sensor Wiring

```
MH-Z19E Pin 3 (GND) ──► GND
MH-Z19E Pin 4 (VIN) ──► 5V  (sensor requires 5V power supply)
MH-Z19E Pin 5 (TX)  ──► ESP32 GPIO16 (UART2 RX)
MH-Z19E Pin 6 (RX)  ──► ESP32 GPIO17 (UART2 TX)
```

> Signal lines (TX/RX) are 3.3V-compatible even though sensor VIN is 5V. No level shifter required.  
> Do NOT connect MH-Z19E TX → ESP32 TX, or RX → RX. Cross-connect TX→RX and RX→TX.

---

## GPIO12 Boot-Safety Circuit (2B)

```
ESP32 GPIO12 ──┬──[100Ω]──[100nF]──► SSR IN+
               │
              10kΩ
               │
              GND
```

The 10kΩ pull-down ensures GPIO12 is read as LOW by the ESP32 strapping logic during power-on reset, regardless of what the SSR module's input circuit does.

---

## Verified GPIO Constraints Summary

| Constraint | Affected Pins | Impact | Workaround |
|-----------|--------------|--------|------------|
| Strapping pins (ESP32) | GPIO0, GPIO2, GPIO12, GPIO15 | May affect boot mode | Pull-down on GPIO12; leave others floating or pulled correctly |
| Input-only (ESP32) | GPIO34, GPIO35, GPIO36, GPIO39 | Cannot drive outputs | Never use for relays or LEDs |
| ADC2 (ESP32-S3) | GPIO11, GPIO13, GPIO14, GPIO15, GPIO16 | Unavailable with WiFi | Use ADC1 pins for any analog reads on 3C |
| High-current sink | All GPIO | Max 12mA per pin | Always use transistor or relay module driver stage for relay coils |
