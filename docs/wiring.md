# Wiring guide — ESP32-S3 to peripherals

This document tells you which physical wire goes where. Pin numbers refer to GPIO numbers on the Waveshare ESP32-S3-Touch-AMOLED-1.64 dev board. See `pinmap.md` for the rationale behind each assignment.

---

## 1. PCA9685 PWM servo driver

The PCA9685 is a 16-channel I²C PWM controller. It drives all servos and the ESC so the ESP32 generates no PWM directly.

| PCA9685 pin | Connects to | Notes |
|---|---|---|
| VCC | 3.3 V (ESP32 3V3 pin) | Logic supply only |
| GND | GND | Common ground |
| SDA | ESP32 IO6 | Pull-up resistors are on the PCA9685 breakout board |
| SCL | ESP32 IO7 | |
| V+ | Servo power rail (5–6 V BEC) | **Separate from logic VCC** — must supply enough current for all servos |
| OE | GND | Active-low output enable; tie low to keep outputs active |

**Channel wiring** (3-pin servo connector: signal / +5V / GND):

| PCA9685 channel | Device | Connector type |
|---:|---|---|
| 0 | Rudder servo | Standard servo (signal=orange/white, +5V=red, GND=brown/black) |
| 1 | Sail winch servo (HS-785HB or similar) | Standard servo — verify travel range in firmware |
| 2 | Motor ESC signal input | Standard servo connector — arm procedure: send 1500 µs at power-on |
| 3–15 | Unused | Leave disconnected |

> The servo power rail (V+) must NOT be powered from the ESP32 5V pin — use a dedicated BEC (battery eliminator circuit) from the main battery.

---

## 2. INA219 current sensor

The INA219 monitors the main battery current draw.

| INA219 pin | Connects to | Notes |
|---|---|---|
| VCC | 3.3 V (ESP32 3V3 pin) | |
| GND | GND | |
| SDA | ESP32 IO6 | Same I²C bus as PCA9685 |
| SCL | ESP32 IO7 | |
| VIN+ | Battery positive (after fuse) | High-side or low-side depending on your shunt placement |
| VIN− | Load side of shunt resistor | Current flows VIN+ → shunt → VIN− → load |
| A0  | **Solder to VS** | Moves I²C address from 0x40 → 0x41; required to avoid collision with PCA9685 |

> **Important**: solder the A0 address jumper before first power-on. Both PCA9685 and INA219 default to 0x40 — they will conflict on the bus if A0 is left open.

---

## 3. ELRS RP3 receiver (CRSF over UART)

The ELRS receiver communicates with the ESP32 using the CRSF serial protocol at 420000 baud on UART1.

| ELRS RP3 pin | Connects to | Notes |
|---|---|---|
| TX | ESP32 IO17 (UART1 RX) | Receiver transmits → ESP32 receives |
| RX | ESP32 IO18 (UART1 TX) | ESP32 transmits → receiver receives (telemetry) |
| 5V | 5 V rail | Most ELRS receivers run on 5 V; verify your module's datasheet |
| GND | GND | Common ground |

> Use a logic-level shifter if your ELRS receiver is 5 V only and its signal pins are not 3.3 V tolerant. Many modern ELRS receivers are 3.3 V tolerant — check the datasheet.

> UART0 (IO43/IO44) is reserved for USB-CDC serial (the Arduino `Serial` monitor). UART1 (IO17/IO18) is used for CRSF.

---

## 4. Status LED

A single LED indicates firmware heartbeat and failsafe state.

| Component | Connects to | Notes |
|---|---|---|
| LED anode (+) | ESP32 IO9 | |
| 330 Ω resistor | In series with LED | |
| LED cathode (−) | GND | |

LED behavior (to be defined in firmware):
- Slow blink (1 Hz): system armed and operating normally
- Fast blink (5 Hz): failsafe active
- Solid on: initialising / waiting for ELRS link

---

## 5. GPS module (optional)

A standard NMEA-over-UART GPS module (e.g., NEO-6M, NEO-M8N, Quectel L76K) provides position, speed, and heading via satellite.

Build and flash the firmware with `-DGPS_ENABLED` (use the `esp32-s3-gps` PlatformIO environment) to activate this feature.

| GPS module pin | Connects to | Notes |
|---|---|---|
| VCC | 3.3 V or 5 V (check module datasheet) | NEO-6M breakouts typically accept 3.3–5 V; some have an onboard regulator |
| GND | GND | Common ground |
| TX | ESP32 IO1 (UART2 RX) | GPS transmits NMEA sentences → ESP32 receives |
| RX | ESP32 IO2 (UART2 TX) | ESP32 sends config commands → GPS receives; can be left unconnected if no reconfiguration needed |

Default baud rate: **9600 baud** (standard NMEA default). If your module defaults to a different rate, reconfigure it via IO2 before finalising the firmware.

> GPS fix time (cold start) is typically 30–60 seconds outdoors. The module needs an unobstructed view of the sky.

---

## 6. Power summary

| Rail | Source | Consumers |
|---|---|---|
| 3.3 V | ESP32 onboard regulator (3V3 pin) | PCA9685 VCC, INA219 VCC, ELRS receiver (if 3.3 V) |
| 5 V BEC | External BEC from main battery | Servo V+ rail on PCA9685, ELRS receiver (if 5 V) |
| Main battery | LiPo (2S or 3S) | Motor ESC, BEC input |

> Never connect the servo V+ rail to the ESP32's 5V/VBUS pin. Servo current spikes will brown out the ESP32.

---

## 7. I²C bus summary

| Bus | ESP32 pins | Devices |
|---|---|---|
| External (user) | IO6 (SDA), IO7 (SCL) | PCA9685 @ 0x40, INA219 @ 0x41 |
| Onboard (board) | IO47 (SDA), IO48 (SCL) | IMU QMI8658, touch controller CST816S — **do not connect external devices** |
