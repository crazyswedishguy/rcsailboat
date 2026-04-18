# Pin map — ESP32-S3 (Waveshare 1.64" AMOLED dev board)

**This is the canonical pin assignment.** The firmware's `config.h` must match this table exactly. When pins change, update both simultaneously.

## ESP32-S3 pin assignments

| GPIO | Direction | Function | Connects to | Notes |
|-----:|-----------|----------|-------------|-------|
| TBD  | OUT | I²C SDA | PCA9685 SDA, INA219 SDA | Shared bus; pull-ups on PCA9685 breakout |
| TBD  | OUT | I²C SCL | PCA9685 SCL, INA219 SCL | Shared bus |
| TBD  | IN  | UART RX (CRSF from ELRS) | ELRS RP3 TX pin | 420000 baud, CRSF |
| TBD  | OUT | UART TX (CRSF to ELRS) | ELRS RP3 RX pin | 420000 baud, CRSF |
| TBD  | IN  | ESP32-S3 IMU INT (optional) | Onboard IMU interrupt | If used |
| TBD  | OUT | Status LED | Board LED or external | Heartbeat / failsafe indicator |

> **TBD**: the Waveshare board has several pins consumed by the AMOLED display, touch controller, and onboard IMU. Fill these in after consulting the board's pinout diagram. Reserve these before committing to any wiring.

## Pins known to be consumed by the board itself

Do **not** repurpose these without reading the Waveshare schematic:

- AMOLED display QSPI lines (CS, CLK, D0–D3)
- AMOLED backlight / reset
- Capacitive touch I²C (may share the main I²C bus — verify; if so, addresses must not collide with PCA9685 @ 0x40 or INA219 @ 0x40/0x41/0x44/0x45)
- Onboard IMU (accelerometer + gyro)
- USB D+ / D- (GPIO19/20 on standard ESP32-S3)
- Boot strapping pins (GPIO0, GPIO45, GPIO46) — avoid using as outputs

## I²C addresses on the shared bus

| Device | Address | Notes |
|---|---|---|
| PCA9685 | 0x40 (default) | Can be changed via address jumpers if there's a collision |
| INA219 | 0x40 / 0x41 / 0x44 / 0x45 | **Warning: default INA219 address is 0x40 — same as PCA9685.** Re-jumper one of them. |
| Onboard touch controller | TBD — check board docs | |
| Onboard IMU | TBD — check board docs | |

> **Address collision note**: resolve the 0x40 conflict between PCA9685 and INA219 *before* powering the bus. Easiest fix: solder A0 on the INA219 to move it to 0x41.

## PCA9685 channel assignments

| PCA9685 channel | Purpose | PWM neutral (µs) | PWM range (µs) | Notes |
|---:|---|---:|---|---|
| 0 | Rudder servo | 1500 | 1000–2000 | Reverse direction in firmware if mounted flipped |
| 1 | Sail winch servo | 1500 | ~500–2500 | Winch servos have extended travel — verify with HS-785HB datasheet |
| 2 | Motor ESC | 1500 | 1000–2000 | ESC must see 1500 µs at power-on to arm |
| 3–15 | Reserved | — | — | |

PCA9685 PWM frequency: **50 Hz** (standard servo/ESC rate).
