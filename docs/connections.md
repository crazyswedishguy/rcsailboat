# Pin-by-pin connections

One table per component. Every external pin is listed; onboard-only signals (e.g. display, IMU, touch) are omitted as they require no bench wiring.

---

## 3S LiPo battery

| Pin / terminal | Connects to | Wire gauge | Notes |
|---|---|---|---|
| T-Plug + (main) | Bus bar shunt — high-side terminal | 12–14 AWG silicone | Full battery current; use heavy silicone wire for flexibility |
| T-Plug − (main) | GND rail (ESC − and UBEC −) | 12–14 AWG silicone | Single negative bus |
| Balance tap (JST-XH 4-pin) | Charging port balance pigtail | 26 AWG | For in-hull charging; no firmware connection |

---

## Bus bar shunt — 50A / 75 mV / 1.5 mΩ

| Terminal | Connects to | Wire gauge | Notes |
|---|---|---|---|
| High-side power terminal (+) | LiPo T-Plug + | 12–14 AWG silicone | Incoming battery positive |
| Low-side power terminal (−) | ESC T-Plug + **and** UBEC Input + | 12–14 AWG silicone | Split point; both ESC and UBEC connect here |
| Sense terminal + | INA228 IN+ | 24–26 AWG | Short run; route away from motor wires |
| Sense terminal − | INA228 IN− | 24–26 AWG | Short run; route away from motor wires |

---

## INA228 breakout — I²C current/voltage sensor (address 0x41)

| Pin | Connects to | Wire gauge | Notes |
|---|---|---|---|
| VIN | ESP32-S3 3.3 V | 24 AWG | Logic supply |
| GND | GND rail | 24 AWG | |
| SCL | ESP32-S3 GPIO48 | 24–26 AWG | Shared I²C bus |
| SDA | ESP32-S3 GPIO47 | 24–26 AWG | Shared I²C bus |
| VBUS | Not connected | — | Internal bus-voltage measurement node; no external connection required |
| VIN+ | Bus bar shunt — sense terminal + | 24–26 AWG | High-side sense (battery positive) |
| VIN− | Bus bar shunt — sense terminal − | 24–26 AWG | Low-side sense (after shunt) |
| ALRT | Not connected | — | Open-drain alert; leave floating unless alarm output needed |
| A0 pad | Bridge to VS | — | Solder bridge on breakout; sets address bit → 0x41 |
| A1 pad | GND (or leave open) | — | Default GND; confirms address 0x41 |

---

## HOBBYWING Quicrun 1060 — Brushed ESC

| Pin / terminal | Connects to | Wire gauge | Notes |
|---|---|---|---|
| T-Plug + (battery input) | Bus bar shunt — low-side power terminal | 12–14 AWG silicone | Full motor + battery current |
| T-Plug − (battery input) | GND rail | 12–14 AWG silicone | |
| Motor output A (red) | Motor terminal 1 | 12–14 AWG silicone | Swap A/B if motor direction is wrong |
| Motor output B (black) | Motor terminal 2 | 12–14 AWG silicone | |
| 3-pin — Signal (white/orange) | PCA9685 Ch2 signal pin | 24–26 AWG | 50 Hz PWM from PCA9685 |
| 3-pin — +BEC (red) | **NOT CONNECTED — tape off** | — | SBEC disabled; UBEC owns the servo rail; connecting both causes a voltage conflict |
| 3-pin — GND (black/brown) | GND rail | 24–26 AWG | Common ground |

---

## UBEC — Waterproof 10A, 6V output

| Pin / terminal | Connects to | Wire gauge | Notes |
|---|---|---|---|
| Input + | Bus bar shunt — low-side power terminal | 14–16 AWG silicone | Parallel with ESC input; set UBEC output to 6V before wiring |
| Input − | GND rail | 14–16 AWG | |
| Output + (6V) | PCA9685 V+ (servo power rail) | 18–20 AWG | Up to ~2.5A servo load |
| Output + (6V) | 5V buck converter Input + | 22 AWG | Branch wire from same tap point |
| Output − | GND rail | 18–20 AWG | |

---

## 5V Buck converter — 3A, input 5–30V

| Pin / terminal | Connects to | Wire gauge | Notes |
|---|---|---|---|
| Input + | UBEC Output + (6V) | 22 AWG | Pre-filtered by UBEC; do not connect directly to battery |
| Input − | GND rail | 22 AWG | |
| Output + (5V) | ESP32-S3 USB-C VBUS | 22 AWG | Via USB-C pigtail (VBUS + GND only; no data lines) |
| Output + (5V) | PCA9685 VCC | 22–24 AWG | Logic supply for PCA9685 IC (must be ≤5.5V; never connect 6V UBEC here) |
| Output + (5V) | ELRS receiver VCC | 22–24 AWG | Must be 5V; ESP32 3.3V output is below most ELRS receiver minimum |
| Output − | GND rail | 22 AWG | |

---

## ESP32-S3 — Waveshare AMOLED 1.64 (external connections only)

| Pin | Connects to | Wire gauge | Notes |
|---|---|---|---|
| USB-C VBUS | 5V buck Output + | 22 AWG | Via USB-C pigtail; sole power input; do not also connect MX1.25 LiPo port |
| USB-C GND | GND rail | 22 AWG | |
| 3.3V out | INA228 VIN | 24 AWG | Logic supply for INA228 |
| 3.3V out | BN-880 VCC | 24 AWG | GPS + compass logic supply |
| GND | GND rail | 24 AWG | |
| GPIO47 (SDA) | I²C bus SDA | 24–26 AWG | Shared: PCA9685, INA228, BN-880 compass |
| GPIO48 (SCL) | I²C bus SCL | 24–26 AWG | Shared: PCA9685, INA228, BN-880 compass |
| GPIO16 (UART1 RX) | ELRS receiver TX | 24–26 AWG | MCU receives CRSF frames |
| GPIO17 (UART1 TX) | ELRS receiver RX | 24–26 AWG | MCU sends telemetry |
| GPIO15 (UART2 RX) | BN-880 GPS TXD | 24–26 AWG | NMEA sentences from GPS |
| GPIO18 (UART2 TX) | BN-880 GPS RXD | 24–26 AWG | Optional GPS config commands |
| GPIO2 | Bilge sensor terminal A | 24–26 AWG | Input with internal pull-up; float switch to GND |
| GPIO3 | Bilge pump relay IN | 24–26 AWG | Digital output driving relay gate |

---

## PCA9685 — 16-channel 12-bit PWM servo driver (address 0x40)

| Pin | Connects to | Wire gauge | Notes |
|---|---|---|---|
| VCC | 5V buck Output + | 22–24 AWG | IC logic supply; must be 2.3–5.5V — never connect 6V UBEC directly |
| GND | GND rail | 22–24 AWG | |
| SDA | ESP32-S3 GPIO47 | 24–26 AWG | I²C data |
| SCL | ESP32-S3 GPIO48 | 24–26 AWG | I²C clock |
| V+ | UBEC Output + (6V) | 18–20 AWG | Servo power rail |
| GND (V+ rail) | GND rail | 18–20 AWG | Must share GND with ESP32 |
| /OE | GND | 24 AWG | Tie low; keeps all outputs permanently enabled |
| Ch0 — Signal | Rudder servo signal (white/orange) | 24–26 AWG | 50 Hz PWM |
| Ch0 — V+ | Rudder servo power (red) | 20–22 AWG | From V+ servo rail via channel header |
| Ch0 — GND | Rudder servo GND (black) | 24 AWG | |
| Ch1 — Signal | Sail winch servo signal | 24–26 AWG | 50 Hz PWM |
| Ch1 — V+ | Sail winch servo power (red) | 20–22 AWG | |
| Ch1 — GND | Sail winch servo GND | 24 AWG | |
| Ch2 — Signal | ESC 3-pin white signal wire | 24–26 AWG | 50 Hz PWM to ESC |
| Ch2 — GND | GND rail (via ESC 3-pin black) | 24 AWG | |
| Ch3–15 | Not connected | — | Leave disconnected |

---

## ELRS receiver

| Pin | Connects to | Wire gauge | Notes |
|---|---|---|---|
| VCC | 5V buck Output + | 22–24 AWG | Must be 4.5–6V; do not use ESP32 3.3V |
| GND | GND rail | 22–24 AWG | |
| TX | ESP32-S3 GPIO16 (UART1 RX) | 24–26 AWG | CRSF frames from receiver to MCU |
| RX | ESP32-S3 GPIO17 (UART1 TX) | 24–26 AWG | Telemetry from MCU to receiver |

---

## BN-880 GPS module (u-blox M8N + HMC5883L compass)

| Pin | Connects to | Wire gauge | Notes |
|---|---|---|---|
| VCC | ESP32-S3 3.3V | 24 AWG | GPS and compass logic supply |
| GND | GND rail | 24 AWG | |
| TXD | ESP32-S3 GPIO15 (UART2 RX) | 24–26 AWG | NMEA sentences at 9600 baud |
| RXD | ESP32-S3 GPIO18 (UART2 TX) | 24–26 AWG | Optional config commands |
| SDA | I²C bus SDA (GPIO47) | 24–26 AWG | HMC5883L compass |
| SCL | I²C bus SCL (GPIO48) | 24–26 AWG | HMC5883L compass |

---

## Rudder servo

| Pin / wire | Connects to | Wire gauge | Notes |
|---|---|---|---|
| Signal (white/orange) | PCA9685 Ch0 signal | 24–26 AWG | 50 Hz, 1–2 ms pulse |
| Power (red) | PCA9685 Ch0 V+ (6V servo rail) | 20–22 AWG | |
| GND (black/brown) | PCA9685 Ch0 GND | 24 AWG | |

---

## Sail winch servo

| Pin / wire | Connects to | Wire gauge | Notes |
|---|---|---|---|
| Signal (white/orange) | PCA9685 Ch1 signal | 24–26 AWG | 50 Hz, 1–2 ms pulse |
| Power (red) | PCA9685 Ch1 V+ (6V servo rail) | 20–22 AWG | Winch can draw up to ~2A under load |
| GND (black/brown) | PCA9685 Ch1 GND | 24 AWG | |

---

## Brushed motor

| Terminal | Connects to | Wire gauge | Notes |
|---|---|---|---|
| Terminal 1 | ESC Motor output A (red) | 12–14 AWG silicone | Swap with terminal 2 if direction is wrong |
| Terminal 2 | ESC Motor output B (black) | 12–14 AWG silicone | |
| Case / ground tab | GND rail | 22 AWG | Required for motor capacitor common-mode suppression |

**Motor noise suppression (solder at motor terminals):**

| Capacitor | Placement | Value |
|---|---|---|
| C1 | Across terminal 1 to terminal 2 | 100 nF ceramic (X7R, ≥25V) |
| C2 | Terminal 1 to motor case | 100 nF ceramic (X7R, ≥25V) |
| C3 | Terminal 2 to motor case | 100 nF ceramic (X7R, ≥25V) |

---

## Bilge float switch

| Terminal | Connects to | Wire gauge | Notes |
|---|---|---|---|
| A | ESP32-S3 GPIO2 | 24–26 AWG | Internal pull-up enabled; dry = HIGH, wet = LOW |
| B | GND rail | 24–26 AWG | |

---

## Bilge pump relay module

| Pin | Connects to | Wire gauge | Notes |
|---|---|---|---|
| IN (signal) | ESP32-S3 GPIO3 | 24–26 AWG | Logic-level input; HIGH = relay on |
| VCC (coil supply) | 5V buck Output + | 22–24 AWG | Check module datasheet — most relay modules want 5V |
| GND | GND rail | 22–24 AWG | |
| COM | 6V UBEC Output + (servo rail) | 20 AWG | Pump power source |
| NO (normally open) | Bilge pump + terminal | 20 AWG | Connects when relay energised |
| Pump − | GND rail | 20 AWG | Return path for pump |
