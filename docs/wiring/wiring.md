# RC Sailboat — Wiring Reference

_Generated 2026-05-08. Updated 2026-05-09 (INA228 confirmed; pinmap corrected; on-hand inventory complete)._

> ⚠ All component grounds must share a single star-ground point. Verify this physically before powering. Floating grounds cause servo jitter and unreliable I²C communication.

---

## Power Architecture

### Overview

The boat runs a three-rail power architecture fed by a single 3S LiPo (~11.1 V nominal, 9.9–12.6 V range). Battery positive passes through an inline blade fuse, then through the INA228 current-sense shunt, before reaching the main power distribution bus. Three loads tap off that bus: the ESC draws from raw battery, the UBEC converts to 6 V for servo power, and the buck converter produces 5 V for all logic. The ESP32-S3's onboard 3.3 V regulator powers the radio, sensors, and I²C pull-up resistors.

```
3S LiPo (+)
  │
  ├─[Inline Fuse]─[INA228 shunt]─┬─────────────────── Raw battery rail (~11.1 V)
  │                               │
  │                               ├─ Quicrun 1060 ESC ──────────── Brushed motor
  │                               │
  │                               ├─ SPARKHOBBY UBEC ──► 6 V servo rail
  │                               │                        ├─ PCA9685 V+
  │                               │                        ├─ Rudder servo (power)
  │                               │                        └─ Sail winch servo (power)
  │                               │
  │                               └─ Buck 5 V ─────────► 5 V logic rail
  │                                                        ├─ ESP32-S3 (5 V in)
  │                                                        │    └─ 3.3 V out
  │                                                        │         ├─ Bilge sensor
  │                                                        │         └─ I²C pull-ups
  │                                                        ├─ PCA9685 VCC
  │                                                        ├─ INA228 VCC
  │                                                        ├─ ELRS RP3
  │                                                        └─ BN-880 GPS
  │
3S LiPo (−) ──────────────────────────────────────────── Star ground
```

### Power Rail Summary

| Rail | Voltage | Source | Consumers |
|---|---|---|---|
| Raw battery | ~11.1 V (9.9–12.6 V) | 3S LiPo | ESC, UBEC input, Buck input |
| Servo rail | 6 V regulated | SPARKHOBBY UBEC | PCA9685 V+, rudder servo, sail winch servo |
| Logic rail | 5 V regulated | DC-DC buck converter | ESP32-S3, PCA9685 VCC, INA228 VCC, ELRS RP3, BN-880 GPS |
| MCU rail | 3.3 V regulated | ESP32-S3 onboard | I²C pull-ups, bilge sensor VCC, bilge pump gate drive |

### Load and Power Budget

| Load | Rail | Typical draw | Peak draw | Notes |
|---|---|---|---|---|
| INJORA 550 brushed motor | Raw (~11.1 V) | 5–8 A | 20–30 A stall | Stall current sizes the fuse |
| Quicrun 1060 ESC (overhead) | Raw | ~50 mA | ~100 mA | BEC output not used |
| Sail winch servo (HS-785HB) | 6 V servo | 200 mA idle | 2.5 A stall | High draw under sail load in wind |
| Rudder servo (HobbyPark) | 6 V servo | 150 mA idle | 1.5 A stall | |
| **6 V rail total** | | **~350 mA** | **~4 A** | UBEC rated 10 A — adequate headroom |
| ESP32-S3 + AMOLED | 5 V logic | 200–400 mA | 600 mA (WiFi TX) | Display included |
| PCA9685 | 5 V logic | 10 mA | 10 mA | Logic only; servo power is V+ |
| INA228 | 5 V logic | 1 mA | 1 mA | |
| ELRS RP3 | 5 V logic | 80–100 mA | 200 mA | |
| BN-880 GPS + HMC5883L | 5 V logic | 30–50 mA | 60 mA | |
| Bilge pump | 5 V or 6 V | 0.5–1 A | 1.5 A | Pump voltage TBD |
| **5 V rail total** | | **~750 mA** | **~1.5 A** | Buck rated 3 A — adequate headroom |

### Wire Gauge Summary

| Path | Gauge | Reason |
|---|---|---|
| Battery positive (LiPo → fuse → INA228 → distribution bus) | 14 AWG | Main current path; up to 30 A motor stall |
| Battery negative (LiPo − → star ground) | 14 AWG | Return path for same current |
| ESC power leads (B+ and B−) | 14 AWG | Motor peak current |
| Motor leads (ESC → motor) | 14 AWG | Motor current |
| UBEC input leads | 14 AWG | Up to 5 A input at 11.1 V |
| UBEC output and servo power wires | 18 AWG | Up to 4 A peak servo rail |
| Buck converter input leads | 14 AWG | Up to 2 A at battery voltage |
| Buck converter output and 5 V logic branches | 22 AWG | ~1.5 A max logic rail |
| All signal wires (I²C, UART, PWM, ADC) | 26 AWG | <100 mA; minimise capacitance |

### Motor Noise Suppression

The INJORA 550 brushed motor generates significant electrical noise that can corrupt UART traffic and I²C signals. Install the following before first powered test.

**Capacitors at motor terminals** (solder directly to the motor body, leads as short as possible):
- One 100 nF ceramic capacitor across the two motor terminals
- One 100 nF ceramic capacitor from motor terminal A to motor case (chassis)
- One 100 nF ceramic capacitor from motor terminal B to motor case (chassis)

Use 50 V rated ceramics. Keep leads under 5 mm — long leads add inductance and reduce effectiveness. Conformal-coat or silicone the joints for waterproofing.

**Ferrite ring placement**

| Location | Priority | Wire(s) | How to apply |
|---|---|---|---|
| Motor leads, close to motor body | High | Both motor wires | Thread both wires through one ferrite ring, or one ring per wire |
| ESC motor output leads, at ESC end | High | Both motor wires | One ring per wire at ESC terminals |
| ESC power input leads, at ESC end | Medium | B+ and B− | One ring per wire at ESC power connector |
| Servo signal bundle (if jitter observed) | Low | All signal wires | Route bundle through one ring |

Use snap-fit ferrite clamps (Fair-Rite 31 material). Clip on motor and ESC leads before final routing — no soldering required.

---

## 3S LiPo Battery

The battery is the sole power source for the entire boat. All power rails derive from it. Battery positive must pass through the inline fuse before any branch point; the negative lead goes directly to the star-ground bus.

| FROM Component | FROM Pin # | FROM Descriptor | TO Component | TO Pin # | TO Descriptor | Notes | Wire Gauge |
|---|---|---|---|---|---|---|---|
| 3S LiPo | + / positive | Battery positive | Inline Fuse | IN | Fuse input | ⚠ Fuse must be the first component after battery positive — before any branch point | 14 AWG |
| 3S LiPo | − / negative | Battery negative | Star Ground | — | Common ground bus | Return path for all loads | 14 AWG |
| 3S LiPo | Balance lead | Cell balance connector | Balance charger only | — | Charger balance port | For charging only — not connected during boat operation | — |

> ⚠ Never connect loads directly to the battery terminals before the inline fuse. The fuse protects against short-circuit current during bench wiring and in-hull shorts.

> Use an XT60 or EC5 connector on the battery leads — a connector that is physically impossible to reverse. Confirm polarity before every connection.

---

## Inline Fuse

The inline fuse sits immediately after the battery positive terminal and before the INA228 shunt. Size the fuse to the motor stall current, not running current. The INJORA 550 at 3S can stall at 20–30 A; a 30 A blade fuse is the minimum; 40 A provides margin without masking a genuine fault.

| FROM Component | FROM Pin # | FROM Descriptor | TO Component | TO Pin # | TO Descriptor | Notes | Wire Gauge |
|---|---|---|---|---|---|---|---|
| Inline Fuse | IN | Fuse input (battery side) | 3S LiPo | + / positive | Battery positive | ⚠ Only on the positive lead — do not fuse the negative lead | 14 AWG |
| Inline Fuse | OUT | Fuse output (load side) | INA228 | IN+ / VS | Shunt high side | Power then flows through the current-sense shunt before distribution | 14 AWG |

> Use an ATC/ATO blade fuse holder rated for the current. Cheap holders can heat up and fail before the fuse blows — check contact resistance. A 40 A fuse in a 30 A-rated holder is not safe.

---

## INA228 Power Monitor (I²C 0x41)

The INA228 is a 20-bit current, voltage, and power monitor wired for high-side sensing on the battery positive lead. Its IN+ and IN− terminals straddle an external shunt resistor; the differential voltage reveals current. The VS pin directly monitors the battery bus voltage up to 85 V. The device is powered from VCC (3.3 V or 5 V). Address 0x41 is set by wiring A0 to VS and A1 to GND.

| FROM Component | FROM Pin # | FROM Descriptor | TO Component | TO Pin # | TO Descriptor | Notes | Wire Gauge |
|---|---|---|---|---|---|---|---|
| INA228 | IN+ / VS | Shunt high side + battery voltage sense | Inline Fuse | OUT | Fuse output | Battery current flows from fuse through shunt to load bus; tie IN+ and VS together at this point; 100 nF bypass cap from VS to GND close to pin | 14 AWG |
| INA228 | IN− | Shunt low side (load bus side) | Power distribution bus | + | Battery distribution positive | All loads (ESC, UBEC, Buck) tap from this side of the shunt | 14 AWG |
| INA228 | VCC | Logic power supply | Buck Converter | OUT+ | 5 V regulated output | 100 nF bypass cap close to VCC pin | 22 AWG |
| INA228 | GND | Ground | Star Ground | — | Common ground bus | | 22 AWG |
| INA228 | SDA | I²C data | ESP32-S3 | GPIO47 | I²C data (shared bus) | Shared with PCA9685, HMC5883L, QMI8658, FT3168 | 26 AWG |
| INA228 | SCL | I²C clock | ESP32-S3 | GPIO48 | I²C clock (shared bus) | | 26 AWG |
| INA228 | A0 | I²C address bit 0 | INA228 | VS | VS pin | Tie A0 to VS for address 0x41; short jumper on breakout board | 26 AWG |
| INA228 | A1 | I²C address bit 1 | Star Ground | — | Common ground bus | Tie A1 to GND for address 0x41 | 26 AWG |

> ⚠ The default I²C address 0x40 conflicts with the PCA9685. Confirm A0 = VS and A1 = GND are set correctly before powering. Use `i2cdetect` to verify address 0x41 appears on the bus.

> The INA228 requires an external shunt resistor between IN+ and IN−. For 15 A running and 30 A stall, a 5 mΩ shunt gives a 75–150 mV full-scale differential — within the ±163.84 mV range. Use a precision (1% or better) low-inductance shunt. The shunt value must be configured in firmware (`INA228_SHUNT_OHMS`).

---

## SPARKHOBBY UBEC (10 A, 6 V output)

The UBEC converts raw battery voltage (9.9–12.6 V at 3S) to a stable 6 V for servo power. It feeds the PCA9685 V+ rail, which distributes 6 V to both servos through their 3-pin connectors.

| FROM Component | FROM Pin # | FROM Descriptor | TO Component | TO Pin # | TO Descriptor | Notes | Wire Gauge |
|---|---|---|---|---|---|---|---|
| UBEC | IN+ | Battery input positive | Power distribution bus | + | Battery distribution positive | | 14 AWG |
| UBEC | IN− | Battery input negative | Star Ground | — | Common ground bus | | 14 AWG |
| UBEC | OUT+ | 6 V regulated output | PCA9685 | V+ | Servo power rail input | ⚠ Do not also connect the ESC BEC red wire to this rail — two BECs on the same rail will fight each other | 18 AWG |
| UBEC | OUT− | Regulated output ground | Star Ground | — | Common ground bus | | 18 AWG |

> ⚠ The Quicrun 1060 ESC has an internal BEC on its 3-wire signal connector (the red wire). That wire must be cut or removed from the connector before plugging into PCA9685 CH2. Two BEC outputs on the same rail will damage both.

> The SPARKHOBBY UBEC is rated 10 A continuous. Two servos peaking at 2.5 A each = 5 A peak combined. Monitor UBEC temperature during first sail test in wind.

---

## DC-DC Buck Converter (5 V, 3 A)

The buck converter steps raw battery voltage down to 5 V for all logic: ESP32-S3, PCA9685 VCC, INA228 VCC, ELRS receiver, and BN-880 GPS. The ESP32-S3's onboard regulator then produces 3.3 V for lower-voltage peripherals.

| FROM Component | FROM Pin # | FROM Descriptor | TO Component | TO Pin # | TO Descriptor | Notes | Wire Gauge |
|---|---|---|---|---|---|---|---|
| Buck Converter | IN+ | Battery input positive | Power distribution bus | + | Battery distribution positive | | 14 AWG |
| Buck Converter | IN− | Battery input negative | Star Ground | — | Common ground bus | | 14 AWG |
| Buck Converter | OUT+ | 5 V regulated output | ESP32-S3 | 5V pin | 5 V power input | 100 µF bulk bypass cap at the 5 V junction point recommended | 22 AWG |
| Buck Converter | OUT+ | 5 V regulated output | PCA9685 | VCC | Logic power supply | Branch from the 5 V rail | 22 AWG |
| Buck Converter | OUT+ | 5 V regulated output | INA228 | VCC | Logic power supply | Branch from the 5 V rail | 22 AWG |
| Buck Converter | OUT+ | 5 V regulated output | ELRS RP3 | 5V | Power input | Branch from the 5 V rail | 22 AWG |
| Buck Converter | OUT+ | 5 V regulated output | BN-880 GPS | VCC | Power input | Branch from the 5 V rail | 22 AWG |
| Buck Converter | OUT− | Regulated output ground | Star Ground | — | Common ground bus | | 22 AWG |

> For wire runs on the 5 V rail longer than ~20 cm, add a 100 µF electrolytic (+ toward 5 V) and a 100 nF ceramic in parallel at the branching junction. This prevents voltage droops during WiFi transmission bursts from the ESP32-S3.

---

## ESP32-S3 — Waveshare AMOLED 1.64"

The ESP32-S3 is the boat's main controller. It runs the CRSF parser (ELRS UART1), PCA9685 servo driver (I²C), GPS parser (UART2), bilge monitor (ADC), and WiFi direct-mode web server. It is powered from the 5 V logic rail; the onboard regulator produces 3.3 V for peripherals.

| FROM Component | FROM Pin # | FROM Descriptor | TO Component | TO Pin # | TO Descriptor | Notes | Wire Gauge |
|---|---|---|---|---|---|---|---|
| ESP32-S3 | 5V / 5V pin | 5 V power input | Buck Converter | OUT+ | 5 V regulated output | 100 nF bypass cap close to board 5 V pin | 22 AWG |
| ESP32-S3 | GND | Ground | Star Ground | — | Common ground bus | | 22 AWG |
| ESP32-S3 | GPIO47 / SDA | I²C data | I²C shared bus | SDA | Shared data line | One 4.7 kΩ pull-up to 3.3 V per bus (not per device); check breakout boards for existing pull-ups before adding — most PCA9685/INA228 breakouts have onboard pull-ups | 26 AWG |
| ESP32-S3 | GPIO48 / SCL | I²C clock | I²C shared bus | SCL | Shared clock line | One 4.7 kΩ pull-up to 3.3 V per bus | 26 AWG |
| ESP32-S3 | GPIO16 / CRSF_RX | CRSF UART1 receive | ELRS RP3 | TX | Receiver UART transmit | UART1, 420 000 baud, 8N1; MCU RX ← receiver TX | 26 AWG |
| ESP32-S3 | GPIO17 / CRSF_TX | CRSF UART1 transmit | ELRS RP3 | RX | Receiver UART receive | UART1; bidirectional CRSF for telemetry | 26 AWG |
| ESP32-S3 | GPIO15 / GPS_RX | GPS UART2 receive | BN-880 GPS | TX | GPS NMEA transmit | UART2, 9 600 baud; MCU RX ← GPS TX | 26 AWG |
| ESP32-S3 | GPIO18 / GPS_TX | GPS UART2 transmit | BN-880 GPS | RX | GPS config receive | UART2; optional — only needed to send UBX config commands | 26 AWG |
| ESP32-S3 | GPIO2 / BILGE_SENSOR | Bilge sensor ADC (ADC1_CH1) | Bilge Sensor | SIG | Sensor signal output | analogRead(); 0 V (dry) to ~3.3 V (wet) | 26 AWG |
| ESP32-S3 | GPIO3 / BILGE_PUMP | Bilge pump MOSFET gate driver | Bilge Pump MOSFET | Gate | MOSFET gate | 10 kΩ pull-down from gate to GND prevents floating gate during boot | 26 AWG |
| ESP32-S3 | GPIO4 / BAT_ADC | Battery voltage ADC | Onboard voltage divider | — | PCB resistor divider | Internal to Waveshare board — no external wiring needed | — |
| ESP32-S3 | GPIO38–41 / SPI3 | TF card SPI | TF Card (onboard) | CS/MOSI/MISO/SCLK | SPI interface | Internal PCB traces — no external wiring | — |
| ESP32-S3 | GPIO9–14, 21 / QSPI | AMOLED display | CO5300 (onboard) | — | QSPI display | Internal PCB traces | — |
| ESP32-S3 | GPIO46 / IMU_INT1 | IMU interrupt 1 | QMI8658 (onboard) | INT1 | IMU interrupt | Internal — IMU not currently used in firmware | — |

> ⚠ GPIO0 is the BOOT strapping pin and the onboard BOOT button. Do not drive it during power-up. GPIO19/20 are USB D−/D+; GPIO43/44 are the USB-Serial console (UART0). Do not repurpose these without deliberate intent.

> The I²C bus on GPIO47/48 carries five devices: FT3168 (0x38, onboard touch), PCA9685 (0x40), INA228 (0x41), HMC5883L (0x1E, inside BN-880), QMI8658 (0x6A or 0x6B, onboard IMU). Run `i2cdetect` at first power-up and verify all five addresses appear.

---

## PCA9685 PWM Driver (I²C 0x40)

The PCA9685 generates PWM signals for the rudder servo (CH0), sail winch servo (CH1), and motor ESC (CH2). It has two independent power inputs: VCC for logic (5 V from buck) and V+ for servo/motor power (6 V from UBEC). Swapping these will destroy the chip.

| FROM Component | FROM Pin # | FROM Descriptor | TO Component | TO Pin # | TO Descriptor | Notes | Wire Gauge |
|---|---|---|---|---|---|---|---|
| PCA9685 | VCC | Logic power supply | Buck Converter | OUT+ | 5 V regulated output | ⚠ Logic only — do not connect servo power here; 100 nF bypass cap close to VCC pin | 22 AWG |
| PCA9685 | GND | Logic ground | Star Ground | — | Common ground bus | | 22 AWG |
| PCA9685 | V+ | Servo/motor power rail | UBEC | OUT+ | 6 V regulated output | ⚠ Servo power only — do not connect 5 V logic VCC here | 18 AWG |
| PCA9685 | GND (V+ side) | Servo power ground | Star Ground | — | Common ground bus | Both GND pins on the PCA9685 must be connected | 18 AWG |
| PCA9685 | SDA | I²C data | ESP32-S3 | GPIO47 | I²C data (shared bus) | Shared with INA228, HMC5883L, QMI8658, FT3168 | 26 AWG |
| PCA9685 | SCL | I²C clock | ESP32-S3 | GPIO48 | I²C clock (shared bus) | | 26 AWG |
| PCA9685 | /OE | Output enable (active low) | Star Ground | — | Common ground bus | Tie to GND to enable outputs permanently; assign a free GPIO instead if software servo cutoff is wanted | 26 AWG |
| PCA9685 | CH0 / signal | PWM channel 0 | Rudder Servo | Signal | PWM input | 50 Hz, 1000–2000 µs; signal wire only (no power on this pin) | 26 AWG |
| PCA9685 | CH0 / V+ | Servo connector power | Rudder Servo | Power (red) | Servo motor power | 6 V from V+ rail via PCA9685 servo connector | 18 AWG |
| PCA9685 | CH0 / GND | Servo connector ground | Rudder Servo | GND (brown/black) | Servo ground | | 18 AWG |
| PCA9685 | CH1 / signal | PWM channel 1 | Sail Winch Servo | Signal | PWM input | 50 Hz, 1000–2000 µs | 26 AWG |
| PCA9685 | CH1 / V+ | Servo connector power | Sail Winch Servo | Power (red) | Servo motor power | 6 V from V+ rail | 18 AWG |
| PCA9685 | CH1 / GND | Servo connector ground | Sail Winch Servo | GND (brown/black) | Servo ground | | 18 AWG |
| PCA9685 | CH2 / signal | PWM channel 2 | Motor ESC | Signal | ESC PWM input | ⚠ Remove ESC red wire (BEC+) from connector before plugging in | 26 AWG |
| PCA9685 | CH2 / GND | Signal connector ground | Motor ESC | GND (black) | Signal ground | ESC must share ground with PCA9685 for PWM reference | 26 AWG |
| PCA9685 | CH2 / V+ | Servo connector power | — | — | Not connected | ⚠ DO NOT connect to ESC signal connector — ESC has its own power supply | — |
| PCA9685 | CH3–CH15 | PWM channels 3–15 | — | — | Unused | Leave unconnected | — |

> ⚠ VCC and V+ are independent power inputs. VCC = 5 V (logic); V+ = 6 V (servo power). Connecting UBEC 6 V to the VCC pin will likely destroy the chip. Double-check the PCB markings before soldering.

> ⚠ The I²C SDA and SCL lines on the ESP32-S3 are 3.3 V. Many PCA9685 breakout boards (including Adafruit and clones) have 10 kΩ pull-ups on SDA/SCL tied to VCC (5 V). A 5 V pull-up voltage on the ESP32-S3 I²C pins exceeds the 3.3 V + 5% maximum input rating. Either: (a) power PCA9685 VCC from 3.3 V instead of 5 V — the PCA9685 supports 2.3–5.5 V; or (b) use the Logic Level Shifter on the I²C lines. Option (a) is simpler and the 3.3 V PWM output is still accepted by all standard servos.

---

## Logic Level Shifter (3.3 V ↔ 5 V)

The logic level shifter bridges 3.3 V ESP32 I²C lines and 5 V PCA9685 I²C lines. Whether it is needed depends on PCA9685 VCC configuration — see the PCA9685 rationale above. If PCA9685 VCC is wired to 3.3 V, this module is not needed for I²C.

| FROM Component | FROM Pin # | FROM Descriptor | TO Component | TO Pin # | TO Descriptor | Notes | Wire Gauge |
|---|---|---|---|---|---|---|---|
| Logic Level Shifter | LV | Low-voltage reference | ESP32-S3 | 3V3 | 3.3 V output | LV supply for the 3.3 V side | 26 AWG |
| Logic Level Shifter | HV | High-voltage reference | Buck Converter | OUT+ | 5 V output | HV supply for the 5 V side | 26 AWG |
| Logic Level Shifter | GND | Ground | Star Ground | — | Common ground bus | | 26 AWG |
| Logic Level Shifter | LV1 | Low-voltage SDA | ESP32-S3 | GPIO47 | I²C SDA | 3.3 V side of the I²C data line | 26 AWG |
| Logic Level Shifter | HV1 | High-voltage SDA | PCA9685 | SDA | I²C data | 5 V side of the I²C data line | 26 AWG |
| Logic Level Shifter | LV2 | Low-voltage SCL | ESP32-S3 | GPIO48 | I²C SCL | 3.3 V side of the I²C clock line | 26 AWG |
| Logic Level Shifter | HV2 | High-voltage SCL | PCA9685 | SCL | I²C clock | 5 V side of the I²C clock line | 26 AWG |

> Install only if PCA9685 VCC = 5 V. If PCA9685 VCC = 3.3 V, wire ESP32 GPIO47/48 directly to PCA9685 SDA/SCL and leave this module unused.

---

## ELRS Receiver — RadioMaster RP3

The RP3 receives 2.4 GHz ELRS packets from the base-station transmitter module and exposes them as CRSF frames over UART. The ESP32-S3 reads CRSF to recover servo channel values and link statistics. The RP3 accepts 5 V on its power pin and has an internal LDO for its 3.3 V RF front-end.

| FROM Component | FROM Pin # | FROM Descriptor | TO Component | TO Pin # | TO Descriptor | Notes | Wire Gauge |
|---|---|---|---|---|---|---|---|
| ELRS RP3 | 5V | Power input | Buck Converter | OUT+ | 5 V regulated output | ⚠ Do not power from 3.3 V — RP3 requires 5 V input for its internal LDO | 22 AWG |
| ELRS RP3 | GND | Ground | Star Ground | — | Common ground bus | | 22 AWG |
| ELRS RP3 | TX | CRSF UART transmit | ESP32-S3 | GPIO16 / CRSF_RX | UART1 receive | 420 000 baud, 8N1; MCU RX ← RP3 TX | 26 AWG |
| ELRS RP3 | RX | CRSF UART receive | ESP32-S3 | GPIO17 / CRSF_TX | UART1 transmit | Bidirectional CRSF for telemetry uplink | 26 AWG |

> Route the ELRS antenna as far as possible from the motor, ESC, and battery leads. Nearby metal and high-current wiring disrupts 2.4 GHz reception. Route the antenna vertically if possible, clear of the hull sides.

---

## BN-880 GPS Module (GPS + HMC5883L Compass, I²C 0x1E)

The BN-880 combines a u-blox M8N GPS receiver (UART, NMEA at 9600 baud) with an HMC5883L digital compass (I²C, address 0x1E). Both are powered from the 5 V logic rail. The module accepts 3.3–5 V and has an internal regulator.

| FROM Component | FROM Pin # | FROM Descriptor | TO Component | TO Pin # | TO Descriptor | Notes | Wire Gauge |
|---|---|---|---|---|---|---|---|
| BN-880 GPS | VCC | Power input | Buck Converter | OUT+ | 5 V regulated output | Module accepts 3.3–5 V; 5 V preferred | 22 AWG |
| BN-880 GPS | GND | Ground | Star Ground | — | Common ground bus | | 22 AWG |
| BN-880 GPS | TX (GPS) | NMEA UART transmit | ESP32-S3 | GPIO15 / GPS_RX | UART2 receive | UART2, 9 600 baud; MCU RX ← GPS TX | 26 AWG |
| BN-880 GPS | RX (GPS) | NMEA UART receive | ESP32-S3 | GPIO18 / GPS_TX | UART2 transmit | Optional — only needed to send UBX configuration commands | 26 AWG |
| BN-880 GPS | SDA (compass) | HMC5883L I²C data | ESP32-S3 | GPIO47 | I²C data (shared bus) | Compass at address 0x1E on shared bus | 26 AWG |
| BN-880 GPS | SCL (compass) | HMC5883L I²C clock | ESP32-S3 | GPIO48 | I²C clock (shared bus) | | 26 AWG |

> Mount the GPS antenna with a clear sky view and route the module away from the motor, ESC, and high-current battery wires. The HMC5883L compass is extremely sensitive to magnetic fields from motor current — physical separation improves heading accuracy.

---

## Motor ESC — HobbyWing Quicrun 1060

The Quicrun 1060 is a brushed ESC supporting 2–3S LiPo. It takes a standard 50 Hz PWM signal from PCA9685 CH2 and drives the brushed motor. Its internal BEC is not used — the external UBEC supplies servo power.

| FROM Component | FROM Pin # | FROM Descriptor | TO Component | TO Pin # | TO Descriptor | Notes | Wire Gauge |
|---|---|---|---|---|---|---|---|
| Motor ESC | B+ (red) | Battery positive input | Power distribution bus | + | Battery distribution positive | ⚠ Ferrite bead on this lead at ESC end | 14 AWG |
| Motor ESC | B− (black) | Battery negative input | Star Ground | — | Common ground bus | ⚠ Ferrite bead on this lead at ESC end | 14 AWG |
| Motor ESC | Motor A (yellow) | Motor output terminal A | Brushed Motor | Terminal 1 | Motor winding terminal | Swapping A and B reverses motor direction | 14 AWG |
| Motor ESC | Motor B (orange/blue) | Motor output terminal B | Brushed Motor | Terminal 2 | Motor winding terminal | Ferrite bead on each lead close to motor | 14 AWG |
| Motor ESC | Signal (white/yellow) | PWM signal input | PCA9685 | CH2 / signal | PWM channel 2 output | 50 Hz, 1000–2000 µs; 3.3 V logic level compatible | 26 AWG |
| Motor ESC | GND (black, signal) | Signal ground | Star Ground | — | Common ground bus | ESC must share ground with PCA9685 for PWM reference | 26 AWG |
| Motor ESC | BEC+ (red, signal) | Internal BEC positive | — | — | DO NOT CONNECT | ⚠ Cut or remove this wire before connecting signal lead. Connecting to the UBEC servo rail will put two regulators in parallel | — |

> ⚠ The ESC signal connector has three wires: white (signal), black (ground), red (BEC+). The red wire must be removed from the connector before plugging into PCA9685 CH2. Only signal and ground should connect.

> The Quicrun 1060 must see neutral throttle (~1500 µs) for several seconds at power-on before it will arm. The firmware sends `SERVO_PULSE_MID_US` on CH2 during initialization — confirm this happens before attempting motor control.

---

## Brushed Motor — INJORA 550 (21T, Waterproof)

The INJORA 550 is a two-terminal brushed motor. Swapping the two leads reverses direction. Install noise-suppression capacitors on the motor body before routing the leads.

| FROM Component | FROM Pin # | FROM Descriptor | TO Component | TO Pin # | TO Descriptor | Notes | Wire Gauge |
|---|---|---|---|---|---|---|---|
| Brushed Motor | Terminal 1 | Motor winding terminal A | Motor ESC | Motor A (yellow) | ESC motor output A | Ferrite bead close to motor body; swap terminals to reverse direction | 14 AWG |
| Brushed Motor | Terminal 2 | Motor winding terminal B | Motor ESC | Motor B (orange/blue) | ESC motor output B | Ferrite bead close to motor body | 14 AWG |

> Install three 100 nF ceramic capacitors on the motor body (see Motor Noise Suppression section above): one across the terminals, one from each terminal to the motor case. Keep capacitor leads under 5 mm. Conformal-coat joints after installation.

---

## Rudder Servo — HobbyPark 7.5 kg Waterproof

The rudder servo controls boat steering. It receives 6 V power via the PCA9685 CH0 servo connector and a 50 Hz PWM signal on CH0. Full starboard is the firmware failsafe position.

| FROM Component | FROM Pin # | FROM Descriptor | TO Component | TO Pin # | TO Descriptor | Notes | Wire Gauge |
|---|---|---|---|---|---|---|---|
| Rudder Servo | Power (red) | Servo motor power | PCA9685 | CH0 / V+ | Servo connector power (6 V) | 6 V from UBEC via PCA9685 V+ bus | 18 AWG |
| Rudder Servo | GND (brown/black) | Servo ground | PCA9685 | CH0 / GND | Servo connector ground | | 18 AWG |
| Rudder Servo | Signal (orange/yellow) | PWM signal input | PCA9685 | CH0 / signal | PWM channel 0 | 50 Hz, 1000–2000 µs | 26 AWG |

> After wiring, confirm that `RUDDER = +1.0` (SERVO_PULSE_MAX_US, 2000 µs) corresponds to full-starboard deflection. If deflection is backward, reverse the servo horn or flip `failsafe_pos::RUDDER` in `config.h`.

---

## Sail Winch Servo — Hitec HS-785HB

The Hitec HS-785HB is a multi-turn winch servo for sheet control. It draws significantly more current under sail load than the rudder servo. It is powered from PCA9685 CH1 on the 6 V servo rail.

| FROM Component | FROM Pin # | FROM Descriptor | TO Component | TO Pin # | TO Descriptor | Notes | Wire Gauge |
|---|---|---|---|---|---|---|---|
| Sail Winch Servo | Power (red) | Servo motor power | PCA9685 | CH1 / V+ | Servo connector power (6 V) | 6 V from UBEC via PCA9685 V+ bus; high current draw under sheet tension | 18 AWG |
| Sail Winch Servo | GND (brown/black) | Servo ground | PCA9685 | CH1 / GND | Servo connector ground | | 18 AWG |
| Sail Winch Servo | Signal (orange/yellow) | PWM signal input | PCA9685 | CH1 / signal | PWM channel 1 | 50 Hz, 1000–2000 µs | 26 AWG |

> ⚠ The HS-785HB can draw up to 2.5 A stall from the 6 V rail under sail load. Monitor UBEC temperature during first sail in wind.

> Confirm that `SAIL = −1.0` (SERVO_PULSE_MIN_US, 1000 µs) physically eases (lets out) the sail. The failsafe in `config.h` (`failsafe_pos::SAIL = -1.0f`) assumes minimum pulse = sail eased. If your winch is rigged the opposite way, flip the sign.

---

## Bilge Sensor — DYIables Resistive Water Sensor

The DYIables sensor is a resistive probe: its SIG output rises toward VCC as water conductivity increases. The ESP32-S3 reads this as an analog voltage via ADC1 channel 1 (GPIO2) using `analogRead()`.

| FROM Component | FROM Pin # | FROM Descriptor | TO Component | TO Pin # | TO Descriptor | Notes | Wire Gauge |
|---|---|---|---|---|---|---|---|
| Bilge Sensor | VCC | Power input | ESP32-S3 | 3V3 | 3.3 V output | Low current draw; powered directly from MCU 3.3 V pin | 26 AWG |
| Bilge Sensor | GND | Ground | Star Ground | — | Common ground bus | | 26 AWG |
| Bilge Sensor | SIG | Analog signal output | ESP32-S3 | GPIO2 / BILGE_SENSOR | ADC1 channel 1 | Analog voltage; firmware uses analogRead() | 26 AWG |

> Mount the sensor probe at the lowest accessible point of the hull interior. Secure the lead so the probe cannot shift position while underway.

---

## Bilge Pump MOSFET Circuit

GPIO3 drives an N-channel MOSFET gate to switch the bilge pump. Use a logic-level N-MOSFET with a gate threshold well below 3.3 V (e.g., IRLZ44N for larger pumps, 2N7002 SOT-23 for smaller ones). A 10 kΩ pull-down resistor prevents the gate from floating and activating the pump at boot.

| FROM Component | FROM Pin # | FROM Descriptor | TO Component | TO Pin # | TO Descriptor | Notes | Wire Gauge |
|---|---|---|---|---|---|---|---|
| Bilge Pump MOSFET | Gate | MOSFET gate | ESP32-S3 | GPIO3 / BILGE_PUMP | GPIO output | 10 kΩ pull-down from gate to GND to prevent floating gate at boot | 26 AWG |
| Bilge Pump MOSFET | Drain | MOSFET drain | Bilge Pump | − (negative) | Pump motor negative | MOSFET switches the pump return path | 22 AWG |
| Bilge Pump MOSFET | Source | MOSFET source | Star Ground | — | Common ground bus | | 22 AWG |
| Bilge Pump | + (positive) | Pump motor positive | TBD | — | 5 V or 6 V rail (pump voltage TBD) | ⚠ Verify pump voltage rating before connecting — do not connect a 5 V pump to the 6 V servo rail | 22 AWG |
| Bilge Pump | − (negative) | Pump motor negative | Bilge Pump MOSFET | Drain | MOSFET drain | | 22 AWG |

> Add a flyback diode (1N4007 or Schottky) across the pump motor terminals: anode to pump −, cathode to pump +. This protects the MOSFET from inductive kickback when the pump is switched off.

> ⚠ Pump voltage rating is not yet specified in the BOM. Determine the pump's rated voltage before connecting it to a power rail. A 5 V pump on the 6 V servo rail will overvolt and may fail.

---

## TF Card — Onboard (SPI mode)

The TF card slot is on the Waveshare PCB and is internally wired to GPIO38–41 via PCB traces. No external wiring is required. Listed here for completeness.

| FROM Component | FROM Pin # | FROM Descriptor | TO Component | TO Pin # | TO Descriptor | Notes | Wire Gauge |
|---|---|---|---|---|---|---|---|
| TF Card | CS | SPI chip select | ESP32-S3 | GPIO38 / SD_CS | SPI3 CS | Internal PCB trace — no external wire | — |
| TF Card | MOSI | SPI data input (card) | ESP32-S3 | GPIO39 / SD_MOSI | SPI3 MOSI | Internal PCB trace | — |
| TF Card | MISO | SPI data output (card) | ESP32-S3 | GPIO40 / SD_MISO | SPI3 MISO | Internal PCB trace | — |
| TF Card | SCLK | SPI clock | ESP32-S3 | GPIO41 / SD_SCLK | SPI3 SCLK | Internal PCB trace | — |
| TF Card | VCC | Power | ESP32-S3 | 3V3 | 3.3 V output | Via PCB trace | — |
| TF Card | GND | Ground | ESP32-S3 | GND | Internal ground | Via PCB trace | — |

> 4-bit SDIO mode is not available on this board — the SDIO data pins share nets with SPI through 0 Ω resistors. Use SPI mode only (`SD_CS = GPIO38, SD_MOSI = GPIO39, SD_MISO = GPIO40, SD_SCLK = GPIO41`).

---

_End of RC Sailboat Wiring Reference._
