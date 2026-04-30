# Wiring reference

Complete connection guide for the RC sailboat bench assembly. Pin numbers are GPIO numbers on the Waveshare ESP32-S3-Touch-AMOLED-1.64 dev board. See `pinmap.md` for the rationale behind each assignment.

---

## Power architecture

The boat runs from a **3S LiPo** (11.1 V nominal, 9.0–12.6 V range, T-Plug connector). A dedicated **10A UBEC** (set to 6V) is the sole electronics power source — it feeds both the servo rail and the 5V buck converter. The Quicrun 1060 ESC handles motor control only; its built-in SBEC red wire is left **disconnected** from the servo rail to prevent a BEC voltage conflict.

```
  3S LiPo ──→ Quicrun 1060 ESC ──→ Motor
  3S LiPo ──→ 10A UBEC (6V) ──→ INA219 ──→ PCA9685 V+ (servo rail) ──→ Rudder servo
                             │                                        └──→ Sail winch servo
                             └──→ 5V Buck converter ──→ ESP32 (USB-C), PCA9685 VCC, ELRS receiver
                                                    └──→ INA219, BN-880 (via ESP32 3.3V)
```

| Rail | Voltage | Source | Consumers |
|---|---|---|---|
| Motor | 9–12.6 V (direct battery) | Quicrun 1060 ESC | Brushed drive motor |
| Servo power | 6 V | 10A UBEC (via INA219 shunt) | PCA9685 V+ rail, all servo power pins |
| Logic 5 V | 5 V | 3A 5V buck converter (fed from UBEC 6V output) | ESP32 (via USB-C), PCA9685 VCC, ELRS receiver |
| Logic 3.3 V | 3.3 V | ESP32 onboard regulator | INA219, BN-880 GPS/compass |

### UBEC power budget (10A rated — total load is well within spec)

| Load | Typical draw | Peak draw |
|---|---|---|
| Rudder servo | 100–200 mA | ~500 mA |
| Sail winch servo | 200–500 mA | ~1.5–2 A |
| 5V buck converter (all logic + ELRS) | ~400 mA | ~600 mA |
| **Total** | **~700 mA–1.1 A** | **~2.5–3 A** |

The 10A UBEC runs at ~10–30% load under normal conditions, so its output stays solidly at 6V with minimal ripple. This clean, regulated 6V is what makes feeding the buck converter from the UBEC (rather than directly from the battery) the better choice: the UBEC absorbs battery-side motor noise before it reaches the logic supply.

### Wire gauges

| Path | Recommended gauge | Reason |
|---|---|---|
| Battery → ESC (T-Plug) | 14–12 AWG silicone | Motor can draw 20+ A |
| ESC → motor | 14–12 AWG silicone | Same high current path |
| Battery → UBEC input | 16–14 AWG silicone | UBEC rated 10A; wire to match |
| UBEC out → INA219 → PCA9685 V+ | 20–18 AWG | Up to 3A servo + logic load |
| UBEC out → buck converter input | 22–20 AWG | <600 mA logic load |
| Buck output → ESP32 USB-C | 22–20 AWG | <500 mA |
| Buck output → ELRS receiver | 22–20 AWG | <200 mA |
| Signal and I²C wires | 24–26 AWG | Low current only |

### Ferrite cores and motor noise suppression

Brushed motors generate significant wideband RF noise from commutator sparking. This noise propagates back through the motor wires, through the ESC, and onto the battery line where it can reach other electronics. Address it at the source first, then at sensitive receivers.

**Motor capacitors (most effective, do this first):**

Solder three 100 nF ceramic capacitors (X7R or similar, rated ≥25V) directly at the motor terminals:
- One cap across the two motor terminals
- One cap from each motor terminal to the motor case/body (must have metal case or ground tab)

This is standard practice for brushed motors in RC vehicles and suppresses the highest-frequency commutator noise more effectively than ferrites alone.

**Ferrite rings (use in addition to capacitors):**

| Location | Priority | How to apply |
|---|---|---|
| Motor wires (ESC → motor) | **Critical** | 3–5 turns of each wire through a Type 31 or Type 43 ferrite ring, as close to the motor as possible |
| Battery leads to ESC | **High** | 2–3 turns through a ferrite ring close to the ESC input, to stop back-propagation onto the battery bus |
| ELRS receiver power wire | **High** | Single pass or 2 turns; ELRS operates at 2.4 GHz and motor EMI can desensitize it |
| Buck converter output to ESP32 | **Medium** | Single pass; filters switching noise from the buck converter itself |
| GPS UART wires (BN-880) | **Low** | Single pass if experiencing GPS dropouts; usually not needed |
| I²C wires, servo signal wires | **Not needed** | I²C is robust; servo PWM at 50 Hz is not affected by RF noise |

Suitable ferrite rings: Fair-Rite 0431164281 (Type 31, snap-on), or any 5–10mm clip-on ferrite suitable for wire gauge in use. You do not need large or expensive ferrites for these signal and low-power wires.

---

## ESC — HOBBYWING Quicrun 1060 Brushed (SBEC, T-Plug, 2–3S)

| Quicrun 1060 connection | Connects to | Notes |
|---|---|---|
| T-Plug power input | 3S LiPo T-Plug | Heavy-gauge silicone wire |
| Motor A (large red wire) | Motor terminal 1 | Swap A/B if motor spins the wrong way |
| Motor B (large black wire) | Motor terminal 2 | |
| 3-pin connector — signal (white/orange) | PCA9685 Ch2 signal pin | PWM signal from PCA9685 to ESC |
| 3-pin connector — +BEC (red) | **Leave unconnected** | **SBEC not used — UBEC owns the servo rail. Connecting both causes a BEC conflict. Tape off or cut this wire.** |
| 3-pin connector — GND (black/brown) | GND rail | Common ground |

> Only the signal and GND wires from the ESC 3-pin connector are used. The red +BEC wire must not be connected to any rail — the UBEC already supplies 6V to the PCA9685 V+ bus, and connecting the SBEC in parallel would cause two voltage regulators to fight over the same rail.

---

## UBEC — Waterproof 10A, 6V output

The UBEC connects in parallel with the ESC directly to the 3S battery. Set its output to **6V** before wiring. It is the sole power source for all electronics — servos, logic, and sensors.

| UBEC connection | Connects to | Notes |
|---|---|---|
| Input + | 3S LiPo + (parallel with ESC) | Use 16–14 AWG silicone wire |
| Input − | GND rail | |
| Output + (6V) | INA219 V+ | In-series current monitoring before the servo rail |
| Output + (6V) | 5V buck converter input + | Logic supply chain |
| Output − | GND rail | |

---

## 5V Buck converter — 3A, 5V-30V in → 5V out

Powers the ESP32 and all logic devices. Input comes from the **UBEC's 6V output**, not directly from the battery. The UBEC, running at only 10–30% of its 10A capacity, holds its output solidly at 6V — giving the buck converter a clean, stable, pre-filtered input. Taking input from the UBEC (rather than the raw battery) means motor switching noise is absorbed by the UBEC before it can reach the logic supply.

| Buck converter connection | Connects to | Notes |
|---|---|---|
| Input + | UBEC output + (6V) | Tap at the same point as the servo rail or via a short branch wire |
| Input − | GND rail | |
| Output + (5 V) | ESP32 USB-C VBUS | Use a USB-C power pigtail or cable |
| Output + (5 V) | PCA9685 VCC | Logic supply for the PCA9685 IC |
| Output + (5 V) | ELRS receiver VCC | See ELRS section below |
| Output − | GND rail | |

> **Powering the ESP32:** The Waveshare AMOLED board accepts 5 V via its USB-C port. Connect the buck converter's 5 V output to a short USB-C cable or pigtail (VBUS + GND only — no data lines needed). Do not connect a LiPo to the MX1.25 battery connector at the same time.

> **GPIO4 battery ADC note:** The onboard BAT_ADC divider on GPIO4 is calibrated for a 3.7 V LiPo cell. With 3S voltage (up to 12.6 V) the divider output would exceed the ESP32's 3.3 V ADC limit — **do not wire the 3S battery to GPIO4.** Battery voltage is monitored by the INA219 instead.

---

## INA219 current sensor — electronics rail only

The INA219's default 0.1 Ω shunt is rated for ~3.2 A. The motor draws 20–60 A through the ESC and must never be measured by the INA219. Wire the INA219 in series on the **UBEC output line** to monitor combined servo + logic current (~0.7–3A range), which stays within the shunt's spec.

| INA219 connection | Connects to | Notes |
|---|---|---|
| VCC | ESP32 3.3 V | Logic supply |
| GND | GND | |
| SDA | GPIO47 | Shared I²C bus |
| SCL | GPIO48 | Shared I²C bus |
| V+ | UBEC output + (6 V) | High side of shunt — before servo rail and buck converter |
| V− | PCA9685 V+ rail and buck converter input | After shunt — current flows V+ → shunt → V− → all loads |
| A0 pad | **Bridge to VS** | Shifts I²C address 0x40 → 0x41 (avoids clash with PCA9685) |

---

## I²C bus — GPIO47 (SDA) / GPIO48 (SCL)

One bus carries all I²C devices. Connect SDA and SCL in parallel across every device. A single pair of 4.7 kΩ pull-up resistors to 3.3 V is sufficient (the PCA9685 breakout typically includes them).

| Device | Module | I²C address | Power supply |
|---|---|---|---|
| FT3168 touch | Onboard | 0x38 | Onboard — no wiring needed |
| QMI8658 IMU | Onboard | 0x6A or 0x6B | Onboard — no wiring needed |
| PCA9685 servo driver | Teyleten Robot PCA9685 breakout | **0x40** | VCC: 5 V (from buck); V+: 6 V (from UBEC via INA219) |
| INA219 current sensor | External breakout | **0x41** | VCC: 3.3 V — A0 pad must be bridged |
| HMC5883L compass | On BN-880 GPS module | **0x1E** | VCC: 3.3 V |

> **Address conflict:** INA219 ships with default address 0x40, same as PCA9685. Bridge the A0 solder jumper on the INA219 breakout before wiring.

---

## PCA9685 servo driver — Teyleten Robot 16-channel 12-bit PWM

The PCA9685 has two separate power inputs:

| PCA9685 pin | Connects to | Notes |
|---|---|---|
| VCC | 5 V (from buck converter) | Logic power for the IC — must be 2.3–5.5 V, do not connect 6V directly |
| GND | GND | |
| SDA | GPIO47 | I²C data |
| SCL | GPIO48 | I²C clock |
| V+ | UBEC output (after INA219 shunt) | Servo power rail — 6 V |
| GND (power rail) | GND | Must share GND with ESP32 |
| /OE | GND | Tie low for always-enabled outputs |

### Channel assignments

| Channel | Load | Notes |
|---|---|---|
| 0 | Rudder servo | Standard RC servo, 50 Hz, 1–2 ms pulse |
| 1 | Sail winch servo | Standard RC servo |
| 2 | Motor ESC signal | ESC signal input (white wire from Quicrun 1060 3-pin connector) |
| 3–15 | Unused | Leave disconnected |

---

## UART1 — ELRS receiver, 420 000 baud, 8N1

| Signal | ESP32-S3 GPIO | ELRS receiver pin | Notes |
|---|---|---|---|
| MCU RX ← Rx TX | GPIO16 | TX | |
| MCU TX → Rx RX | GPIO17 | RX | Required for telemetry passthrough |
| GND | GND | GND | |
| Power | **5 V (from buck converter output)** | VCC | Most ELRS receivers are rated 4.5–6V. Do not power from ESP32 3.3V — this is below most receivers' operating range. Verify your specific receiver's datasheet. |

---

## UART2 — GPS (BN-880), 9 600 baud, 8N1

| Signal | ESP32-S3 GPIO | BN-880 pin | Notes |
|---|---|---|---|
| MCU RX ← GPS TX | GPIO15 | TXD | NMEA sentences |
| MCU TX → GPS RX | GPIO18 | RXD | Optional; used for module config |
| GND | GND | GND | |
| Power | 3.3 V | VCC | |

The BN-880 also carries an HMC5883L compass chip. Wire the BN-880's SDA/SCL pads to the I²C bus (GPIO47/48) as well.

---

## SPI — SD card (onboard TF slot)

No external wiring. The TF card slot is soldered to the board.

| Signal | GPIO |
|---|---|
| CS | GPIO38 |
| MOSI | GPIO39 |
| MISO | GPIO40 |
| SCLK | GPIO41 |

---

## GPIO connections

| Function | GPIO | Direction | Notes |
|---|---|---|---|
| Bilge water sensor | GPIO2 | Input | Float switch to GND; internal pull-up enabled in firmware; dry = HIGH, wet = LOW |
| Bilge pump relay | GPIO3 | Output | Drive a relay module or logic-level MOSFET gate; pump draws from servo rail (6V) |
| Battery ADC | GPIO4 | — | **Not used** — calibrated for 3.7 V cell only; use INA219 for voltage monitoring |

---

## Power switch — hull-external on/off

A standard magnetic reed switch cannot be placed directly in the battery circuit — motor peak current (20–60A) would destroy it. Use one of the two approaches below.

### Option A — Commercial waterproof magnetic power switch (recommended)

Purpose-built units for RC boats combine a sealed reed switch with a high-current MOSFET circuit. The reed switch carries only the gate signal (milliamps); the MOSFET handles full battery current. Features to look for:

- **Current rating:** 30A continuous minimum; 60A+ preferred to handle motor stall
- **Anti-spark:** built-in inrush current limiting protects the ESC capacitors and the connector
- **Waterproof rating:** IP67 or better; mount the switch body inside the hull, flush against the hull wall so the external magnet can reach it

Wiring: insert the switch in the positive wire between the battery T-plug and the parallel tap point where the ESC and UBEC share the battery feed.

```
  Battery (+) ──→ [Magnetic power switch] ──→ ESC input (+)
                                          └──→ UBEC input (+)
  Battery (−) ──────────────────────────────→ GND rail (no switch on negative)
```

> Never switch the negative/ground wire. Always switch the positive line only.

### Option B — Reed switch switching UBEC only (simpler, lower safety margin)

If a commercial switch is unavailable, a 3A-rated reed switch in the UBEC supply line is feasible — the UBEC load stays within spec. With the UBEC off, the ESP32 is off, the PCA9685 outputs no PWM, and the ESC holds neutral indefinitely.

Tradeoffs vs Option A:
- The battery remains connected to the ESC at all times (quiescent drain ~10–20 mA)
- The motor circuit is live but inert — safe in practice, less clean in principle
- No anti-spark protection on the main battery connection

```
  Battery (+) ──→ ESC input (+)                          (always connected)
  Battery (+) ──→ [Reed switch, 3A rated] ──→ UBEC input (+)
```

---

## Charging port — in-hull LiPo charging

Charging a 3S LiPo requires two simultaneous connections to the charger:

| Connection | Connector type | Carries |
|---|---|---|
| Main charge lead | T-plug (XT60 compatible) or XT30 | Charge current (up to ~5A at 1C) |
| Balance lead | JST-XH 4-pin (3S standard) | Per-cell voltage sensing |

### Recommended approach — cable gland with pigtail

Mount a **waterproof cable gland** (PG7 or PG9 size) in the hull. Pass a short pigtail through it — a T-plug pigtail wired to the battery's main lead, and a JST-XH balance extension wired to the battery's balance tap. Both pigtails terminate outside the hull for charger connection.

```
  [Hull wall]
  Outside: T-plug female ──→ [cable gland] ──→ Inside: wired to battery main lead (+/−)
  Outside: JST-XH female ──→ [cable gland] ──→ Inside: wired to battery balance tap
```

When not charging, tuck the external pigtails into a small waterproof pouch or tape them against the hull. When charging, connect charger leads to the pigtails — no hull opening required.

### Alternative — waterproof panel-mount connectors

Mount two waterproof panel connectors in the hull (e.g. XT30 + JST-XH with O-ring seals). These provide a cleaner appearance but require finding connectors with adequate IP ratings and machining or 3D-printing a mounting plate.

### Safety rules for charging

- **Switch the boat off** (via the power switch) before connecting the charger. Do not charge while electronics are running — charge current and operating draw through the same wiring confuses the INA219 readings and can cause voltage spikes.
- **Never charge unattended** or inside the sealed hull. LiPo thermal runaway produces gas and heat that can split a hull.
- **Charge at 1C or less** through a cable gland — the wiring gauge (22–20 AWG) limits safe sustained current; 5A is the practical ceiling.

---

## System block diagram

```mermaid
graph TD
    LIPO["3S LiPo\n11.1 V T-Plug"]
    UBEC["10A UBEC\n6V regulated"]

    LIPO -->|T-Plug heavy gauge| ESC["Quicrun 1060\nBrushed ESC\n(motor only — SBEC not used)"]
    LIPO -->|parallel tap\n16-14 AWG| UBEC
    ESC -->|motor wires| MOTOR["Brushed motor"]

    UBEC -->|6V| INA_VIN["INA219 V+"]
    INA_VIN -->|0.1Ω shunt| INA_VOUT["INA219 V−\n= electronics rail 6V"]

    INA_VOUT --> PCA_V["PCA9685 V+\nservo power rail"]
    INA_VOUT --> BUCK["5V Buck\n3A converter"]

    PCA_V --> RUDDER["Rudder servo\nCh0"]
    PCA_V --> SAIL["Sail winch\nCh1"]

    ESC -->|signal wire\n3-pin white only| PCA_CH2["PCA9685 Ch2\nESC signal"]

    BUCK -->|5V via USB-C| ESP["ESP32-S3\nWaveshare AMOLED 1.64"]
    BUCK -->|5V| PCA_VCC["PCA9685 VCC\nlogic"]
    BUCK -->|5V| ELRS_RX["ELRS receiver"]

    ESP -->|3.3V| SENSORS["INA219 VCC\nBN-880 VCC"]

    ESP <-->|I2C\nGPIO47 SDA / GPIO48 SCL| I2C_BUS["I²C bus"]
    I2C_BUS --- PCA["PCA9685\n0x40"]
    I2C_BUS --- INA["INA219\n0x41"]
    I2C_BUS --- HMAG["BN-880 HMC5883L\n0x1E"]
    I2C_BUS --- TOUCH["FT3168 touch\n0x38 (onboard)"]
    I2C_BUS --- IMU["QMI8658 IMU\n0x6A/6B (onboard)"]

    ESP <-->|UART1 signal\nGPIO16/17 420kbaud| ELRS_RX
    ELRS_RX <-->|2.4 GHz RF| ELRS_TX["ELRS TX module\non Pi"]

    ESP <-->|UART2\nGPIO15/18 9600baud| BN880["BN-880 GPS"]
    BN880 <-->|I2C| I2C_BUS

    ESP -->|GPIO2 input| BILGE_S["Bilge sensor"]
    ESP -->|GPIO3 output| BILGE_P["Bilge pump relay"]

    PI["Raspberry Pi 5"] <-->|USB-Serial| ELRS_TX
    PI -->|Wi-Fi| BROWSER["Browser UI"]
```

---

## Prompt for a visual wiring diagram

Paste the following into Claude.ai (or any capable AI chat) to get a hand-drawn style visual diagram. You can also attach this file for full context.

---

```
I need a clear, easy-to-read visual wiring diagram for a remote-controlled sailboat electronics assembly.
Draw it in a breadboard/schematic style with labeled wires and component silhouettes. Use distinct colors:
  • Dark red / thick = main battery (3S LiPo, high current)
  • Orange = 6 V servo rail (from UBEC)
  • Yellow = 5 V logic rail (from buck converter)
  • Red thin = 3.3 V (from ESP32 regulator)
  • Black = GND
  • Blue = I²C (SDA/SCL)
  • Green = UART TX/RX
  • White/grey = PWM signal wires

Components:
  1. 3S LiPo battery, T-Plug connector (label: 11.1V)
  2. HOBBYWING Quicrun 1060 Brushed ESC (T-Plug input, two heavy motor output wires, 3-pin servo connector — signal and GND used; red BEC wire taped off and NOT connected)
  3. Brushed DC motor
  4. Waterproof 10A UBEC set to 6V output (T-Plug or bare wire input from battery, positive and negative output terminals)
  5. 3A 5V buck converter (input from UBEC 6V output, output 5V)
  6. Waveshare ESP32-S3-Touch-AMOLED-1.64 dev board (USB-C port for 5V power input)
  7. Teyleten Robot PCA9685 16-channel PWM servo driver breakout (VCC pin for 5V logic, V+ pin for 6V servo power)
  8. INA219 current sensor breakout (A0 jumper bridged; in series on UBEC output before the servo rail / buck converter split)
  9. BN-880 GPS module (UART pins + separate I2C compass pads)
  10. ELRS receiver module
  11. Bilge float switch sensor
  12. Relay module for bilge pump

Power connections (use thick lines for high-current paths):
  Battery T-Plug → ESC T-Plug (thick dark red)
  Battery → UBEC input (thick dark red, parallel with ESC)
  ESC motor wires → motor (thick dark red)

  ESC 3-pin connector:
    Signal (white wire) → PCA9685 Ch2 signal pin (grey/white, thin)
    GND (black) → GND rail
    +BEC red wire → TAPED OFF, not connected

  UBEC output+ → INA219 V+ (orange, 6V)
  INA219 V− → PCA9685 V+ servo rail (orange, 6V)
  INA219 V− → buck converter input+ (orange, branch wire)

  PCA9685 V+ rail (6V) → rudder servo power pin, sail winch servo power pin

  Buck converter output 5V → ESP32 USB-C VBUS (yellow, use USB-C pigtail)
  Buck converter output 5V → PCA9685 VCC pin
  Buck converter output 5V → ELRS receiver VCC
  Buck converter GND → GND rail

  ESP32 3.3V → INA219 VCC, BN-880 VCC (thin red)

I2C bus (SDA=GPIO47, SCL=GPIO48 on ESP32):
  Connect in parallel to: PCA9685 (0x40), INA219 (0x41, A0 bridged), BN-880 compass (0x1E)
  Add 4.7kΩ pull-ups: SDA → 3.3V, SCL → 3.3V

UART1 (GPIO16 RX, GPIO17 TX) → ELRS receiver TX, RX
UART2 (GPIO15 RX, GPIO18 TX) → BN-880 GPS TXD, RXD
BN-880 SDA/SCL compass pads → I2C bus

PCA9685 channel signal wires (50Hz PWM):
  Ch0 → rudder servo signal
  Ch1 → sail winch servo signal
  Ch2 → ESC white signal wire (already connected via ESC 3-pin)

GPIO:
  GPIO2 (input, pull-up) → float switch → GND
  GPIO3 (output) → relay IN → relay switches bilge pump from 6V servo rail

Important callouts:
  ⚠ ESC SBEC red wire must be taped off and NOT connected — UBEC owns the servo rail; connecting both BECs causes a voltage conflict
  ⚠ INA219 measures servo + logic current only — motor current bypasses it entirely through the ESC
  ⚠ PCA9685 VCC must be 5V from buck — do NOT connect 6V UBEC directly to VCC (max 5.5V)
  ⚠ ELRS receiver VCC must be 5V from buck — not 3.3V from ESP32
  ⚠ Do NOT connect 3S battery to ESP32 MX1.25 LiPo port (BAT_ADC calibrated for 3.7V only)

Target audience: electronics hobbyist wiring this for the first time. Make high-current wires visually thicker than signal wires.
```
