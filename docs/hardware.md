# Hardware

## Bill of materials

### Base station

| Component | Purpose | Notes |
|---|---|---|
| Raspberry Pi 5 (8 GB) | Host the web UI, bridge to ELRS transmitter | Pimoroni NVMe base; run from USB-C PD supply (5V / 5A) |
| RadioMaster Ranger Micro 2.4 GHz ELRS | Transmitter module | Connected to Pi via USB-serial (CRSF or equivalent) |

### Sailboat

**Primary control / comms**

| Component | Purpose | Notes |
|---|---|---|
| Waveshare ESP32-S3 1.64" AMOLED dev board | Main controller | Has onboard IMU (accel + gyro) usable for heel angle |
| RadioMaster RP3 ELRS receiver | Receives ELRS, exposes CRSF over UART | 2.4 GHz, nano form factor |

**Drivers and actuators**

| Component | Purpose | Notes |
|---|---|---|
| PCA9685 16-ch PWM driver | Generates PWM for servos and ESC | I²C from ESP32-S3 |
| Hitec HS-785HB winch servo | Sail trim (sheet in/out) | Multi-turn winch servo; high-current under load |
| HobbyPark 7.5 kg waterproof servo | Rudder | Digital, metal gear |
| HobbyWing Quicrun 1060 ESC | Brushed motor speed control | 2–3S LiPo input, supports forward/brake/reverse |
| INJORA 550 brushed motor (21T) | Propulsion | Waterproof, for 1:10-scale crawlers — adequate thrust for RC sailboat auxiliary |

**Power / signal conditioning**

| Component | Purpose | Notes |
|---|---|---|
| SPARKHOBBY 10A UBEC (2–6S in, 6/7.4/8.4 V out) | Power servos | Set to 6 V unless a servo explicitly wants higher |
| Mini DC-DC buck (5–30 V → 5 V, 3 A) | Power ESP32-S3 | Feed from main battery |
| INA228 | Measure main battery current and voltage | I²C; address 0x41 (A0=VS, A1=GND) |
| Logic level shifter kit | 3.3 V ↔ 5 V where needed | ESP32-S3 is 3.3 V; most RC gear is 5 V tolerant on signal but verify |
| Ferrite clamp assortment | EMI suppression on motor / servo leads | Add liberally on motor leads |
| Glass fuse assortment + inline holders | Protection on main battery line | Size to motor stall current, not running current |

### Not yet specified (to be decided)

- **Main battery**: likely 3S LiPo, 2200–5000 mAh. Sized based on expected run time and motor draw.
- **Charger**: a balance charger (e.g., ISDT Q6, HOTA D6 Pro) plus a LiPo-safe storage bag.
- **Battery voltage alarm**: cheap piezo alarm that plugs into the balance lead is a good cheap insurance policy.
- **Main power switch**: needs to handle motor peak current.
- **Antenna extension for ELRS RX**: possibly, depending on hull material.

## Power budget (rough)

| Rail | Source | Consumers |
|---|---|---|
| Battery raw (~11.1 V) | 3S LiPo | ESC → motor; UBEC input; buck converter input |
| 6 V servo rail | UBEC output | Rudder servo, sail winch servo |
| 5 V logic rail | Buck converter output | ESP32-S3 dev board (via its 5V input), PCA9685 Vcc, INA228 Vcc |
| 3.3 V | ESP32-S3 onboard regulator | ELRS RX, I²C pull-ups, anything else 3.3 V |

> **Grounding note:** all grounds (battery, UBEC, buck, ESP32, ESC signal ground, PCA9685) must be tied together at a single star-ground point. Floating grounds cause servo jitter and flaky I²C.

## Wiring overview

See `pinmap.md` for exact GPIO assignments.

```
          ┌──────────────┐
          │  3S LiPo     │
          └──┬───┬───┬───┘
             │   │   │
             │   │   └───► Buck 5V ───► ESP32-S3 (5V in) ─► PCA9685 Vcc, INA228 Vcc
             │   │
             │   └───────► UBEC 6V ──────► PCA9685 V+ rail ─► Rudder servo, Winch servo
             │                                              (power, NOT signal)
             │
             └───────────► ESC ──────► Brushed motor
                             │
                             └──► PWM signal from PCA9685 channel
```

INA228 sits in series with the battery positive lead (high-side sensing).

## Known hardware gotchas

- **Winch servo current spikes** under sail load in wind. Budget for peaks well above the idle draw. The 10 A UBEC is likely fine, but verify with the INA228 during first sail.
- **ESP32-S3 AMOLED display** is lovely but is a waterproofing liability inside a hull. Plan either a sealed window or relocate to the base station later.
- **ESC arming**: brushed ESCs typically need to see neutral throttle at power-on before they'll respond. Firmware must send neutral (PWM ~1500 µs) first and hold it briefly.
- **ELRS RX antenna**: keep it away from the motor, ESC, and any large ground planes. Carbon hulls kill 2.4 GHz; fiberglass is fine.
- **I²C pull-ups**: the PCA9685 and INA228 breakout boards usually have their own. Don't stack pull-ups from multiple boards or the bus gets too stiff.
