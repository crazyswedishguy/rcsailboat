# Parts Checklist — RC Sailboat
_Generated 2026-05-09._

> **Tip:** Create `docs/inventory/on-hand.md` with a list of parts you already own — the skill will cross-reference it on future runs and mark items ✅ Have.

## Feature Coverage

| Feature | Status | Notes |
|---|---|---|
| Water detection / bilge monitoring | ✅ Covered | DYIables resistive sensor (GPIO2) |
| GPS positioning | ✅ Covered | BN-880 (u-blox M8N, UART2) |
| Compass / heading | ✅ Covered | HMC5883L bundled with BN-880 (I²C 0x1E) |
| Inertial measurement / heel angle | ✅ Covered | QMI8658 onboard Waveshare ESP32-S3 board |
| Motor control / propulsion | ✅ Covered | HobbyWing Quicrun 1060 ESC + INJORA 550 brushed motor (21T) |
| Servo control — sail winch | ✅ Covered | Hitec HS-785HB winch servo on PCA9685 ch 1 |
| Servo control — rudder | ✅ Covered | HobbyPark 7.5 kg waterproof servo on PCA9685 ch 0 |
| Radio control link | ✅ Covered | RadioMaster RP3 ELRS RX + Ranger Micro TX |
| Current / power monitoring | ✅ Covered | ⚠ hardware.md lists INA219; config.h firmware uses INA228 — confirm which to order |
| Data logging | ✅ Covered | Onboard TF card slot (Waveshare board — no extra hardware needed) |
| Battery voltage monitoring | ✅ Covered | BAT_ADC on GPIO4 (onboard resistor divider) + INA228 |
| WiFi web interface | ✅ Covered | ESP32-S3 built-in WiFi (no extra hardware needed) |
| Bilge pump control | ⚠ Deferred | Firmware GPIO defined (GPIO3, N-MOSFET gate); pump and MOSFET selection tabled |
| Wind sensing / anemometer | Out of scope | Excluded from this build phase |

## Parts

| Status | Component | Qty | Specification | Purpose | Search hint |
|---|---|---|---|---|---|
| 🛒 Buy | Raspberry Pi 5 | 1 | 8 GB RAM | Base station host for web UI and ELRS bridge | search Amazon: "Raspberry Pi 5 8GB" |
| 🛒 Buy | RadioMaster Ranger Micro ELRS TX module | 1 | 2.4 GHz ELRS, USB-serial to Pi | Base station transmitter | search: "RadioMaster Ranger Micro ELRS" |
| 🛒 Buy | Waveshare ESP32-S3 1.64" AMOLED dev board | 1 | SKU 31197; ESP32-S3R8; onboard QMI8658 IMU + TF card + AMOLED | Boat MCU | search: "Waveshare ESP32-S3 Touch AMOLED 1.64" |
| 🛒 Buy | RadioMaster RP3 ELRS receiver | 1 | 2.4 GHz, nano form factor, CRSF over UART | Boat ELRS receiver | search: "RadioMaster RP3 ELRS" |
| 🛒 Buy | PCA9685 16-ch PWM driver breakout | 1 | I²C 0x40, 50 Hz PWM | Drives rudder servo, sail winch, and motor ESC | search: "PCA9685 breakout board" |
| 🛒 Buy | Hitec HS-785HB winch servo | 1 | Multi-turn, high torque, 6V | Sail sheet trim | search: "Hitec HS-785HB" |
| 🛒 Buy | HobbyPark 7.5 kg waterproof servo | 1 | Digital, metal gear, waterproof | Rudder | search: "HobbyPark 7.5kg waterproof servo" |
| 🛒 Buy | HobbyWing Quicrun 1060 ESC | 1 | Brushed, 2–3S LiPo, forward/brake/reverse | Motor speed control | search: "HobbyWing Quicrun 1060" |
| 🛒 Buy | INJORA 550 brushed motor (21T) | 1 | Waterproof, 1:10-scale crawler motor | Auxiliary propulsion | search: "INJORA 550 motor 21T" |
| 🛒 Buy | SPARKHOBBY 10A UBEC | 1 | 2–6S input, 6/7.4/8.4V selectable output | Servo power rail | search: "SPARKHOBBY 10A UBEC" |
| 🛒 Buy | Mini DC-DC buck converter (5V 3A) | 1 | 5–30V input → 5V 3A output | Logic power for ESP32-S3, PCA9685, INA228 | search Amazon: "DC-DC buck 5V 3A mini" |
| 🛒 Buy | INA228 current/voltage sensor breakout | 1 | I²C, 20-bit resolution; set A0=VS A1=GND for 0x41 — ⚠ hardware.md lists INA219; decide before ordering | Battery current and voltage monitoring | search: "INA228 breakout board" |
| 🛒 Buy | Logic level shifter | 1 | Bidirectional, I²C compatible, 4-channel minimum | 3.3V ↔ 5V signal level translation | search Amazon: "bidirectional logic level shifter 4 channel" |
| 🛒 Buy | Ferrite clamp assortment | 1 | Snap-fit, Fair-Rite 31 material preferred | EMI suppression on motor and ESC leads | search Mouser: "ferrite snap clamp 31 material" |
| 🛒 Buy | Inline fuse holder + fuse assortment | 1 | ATC/ATO blade type; size fuse to motor stall current | Battery positive lead short-circuit protection | search Mouser: "ATC blade fuse holder inline" |
| 🛒 Buy | DYIables water/moisture sensor | 1 | Resistive probe, analog output; wires to GPIO2 (ADC1_CH1) | Bilge water detection | search Amazon: "DYIables water sensor" |
| 🛒 Buy | 3S LiPo battery | 1 | 11.1V nominal, 2200–5000 mAh, XT60 connector | Main power source for motor, servos, and electronics | search: "3S LiPo 3300mAh XT60" |
| 🛒 Buy | LiPo balance charger | 1 | 3S compatible; e.g. ISDT Q6 or HOTA D6 Pro | Safe per-cell LiPo charging | search Amazon: "ISDT Q6 LiPo charger" |
| 🛒 Buy | LiPo-safe charging bag | 1 | Sized for 3S battery pack | Safe LiPo charging and storage | search Amazon: "LiPo safe bag" |
| 🛒 Buy | Battery cell voltage alarm | 1 | Plugs into balance lead, piezo buzzer | Low-voltage warning during use | search Amazon: "LiPo voltage alarm balance lead" |
| 🛒 Buy | Main power switch | 1 | ≥30A rated; must handle motor peak current | System power on/off | search Amazon: "RC power switch 30A" |
| 🛒 Buy | XT60 connector pair | 1 pair | Male + female, gold-plated, rated for peak motor current | Battery main lead connection; polarised against reverse connection | search Amazon: "XT60 connector pair" |
| 🛒 Buy | 4.7kΩ pull-up resistors | 2 | 1/4W through-hole | I²C bus pull-ups — ⚠ verify PCA9685 and INA228 breakouts don't already include them before adding; never stack | search Mouser: "4.7k ohm resistor through-hole" |
| 🛒 Buy | Ceramic capacitors 100nF (motor noise) | 3 | 50V, ceramic | Motor terminal noise suppression: one across terminals, one each terminal to motor case | search Mouser: "100nF 50V ceramic capacitor" |
| 🛒 Buy | Bypass capacitors 100nF | 5 | 50V ceramic, one per logic IC VCC pin | Decouple supply pins on PCA9685, INA228, HMC5883L, BN-880 GPS, and ELRS RX | search Mouser: "100nF 50V ceramic capacitor" |
| 🛒 Buy | Bulk electrolytic capacitor (470µF) | 1 | 16V+, electrolytic | Stabilise ESC/motor supply rail against current draw transients | search Mouser: "470uF electrolytic capacitor" |
| 🛒 Buy | Servo extension leads | 2 (optional) | Standard 3-pin servo lead, length to suit routing | Extend PCA9685 connectors to sail winch and rudder mount points | search Amazon: "servo extension lead" |

## Consumables

| Status | Item | Qty | Specification | Purpose | Search hint |
|---|---|---|---|---|---|
| 🛒 Buy | Solder | 1 reel | 60/40 or lead-free, 0.8mm rosin core | Soldering wires and through-hole components | search Amazon: "electronics solder 0.8mm rosin core" |
| 🛒 Buy | Flux pen | 1 | No-clean rosin flux | Improve solder flow; prevent cold joints | search Amazon: "flux pen no-clean electronics" |
| 🛒 Buy | Heat shrink tubing assortment | 1 kit | Mixed sizes, 2:1 shrink ratio, black | Insulate and strain-relieve solder joints | search Amazon: "heat shrink tubing assortment" |
| 🛒 Buy | Isopropyl alcohol (IPA) | 1 bottle | 90%+ concentration | Clean flux residue after soldering | search Amazon: "isopropyl alcohol 99 percent" |
| 🛒 Buy | Helping hands / PCB holder | 1 | — | Hold boards and wires steady during soldering | search Amazon: "helping hands soldering" |
| 🛒 Buy | Wire stripper | 1 | Rated for 14–26 AWG | Strip wire insulation cleanly without nicking conductors | search Amazon: "wire stripper 14 26 AWG" |
| 🛒 Buy | Crimping tool | 1 | Rated for XT60, JST, and Dupont connectors | Crimp terminals onto wire ends | search Amazon: "connector crimping tool" |
| 🛒 Buy | Conformal coat spray | 1 can | Acrylic or silicone, electronics-safe | Waterproof PCB joints and exposed traces | search Amazon: "conformal coat spray electronics" |
| 🛒 Buy | Silicone sealant | 1 tube | Electronics-safe, waterproof | Seal cable entry points and connector interfaces | search Amazon: "silicone sealant electronics waterproof" |
| 🛒 Buy | UV-resistant zip ties | 1 pack | Mixed sizes, black UV-stabilised nylon | Cable management; securing components against vibration | search Amazon: "zip ties UV resistant black" |
| 🛒 Buy | Nylon standoffs and screws | 1 assortment | M3, mixed heights | Mount PCBs without shorting traces to chassis | search Amazon: "M3 nylon standoff assortment" |
| 🛒 Buy | Electrical tape | 1 roll | Rated for voltage in use | Temporary insulation and wire bundling | — |
