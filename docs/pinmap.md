# Pin map

Canonical pin assignment reference for the boat firmware.

**This file is mirrored in two other places** — when you change a pin, update all three:
1. This file (`docs/pinmap.md`).
2. `boat-firmware/src/config.h` — the source of truth for the compiler.
3. `CLAUDE.md` — the quick-reference section that Claude Code reads at session start.

---

## Board

**Waveshare ESP32-S3-Touch-AMOLED-1.64** (SKU 31197)

- ESP32-S3R8 (8 MB PSRAM, 16 MB Flash, dual-core LX7 @ 240 MHz)
- 1.64" AMOLED 280×456, CO5300 driver, QSPI interface
- FT3168 capacitive touch, I²C
- QMI8658 6-axis IMU, I²C
- TF card slot (SPI)
- USB-C + LiPo charging (ETA6098), MX1.25 battery connector

## Onboard peripherals

These pins are wired on the PCB and cannot be reassigned.

### AMOLED display (CO5300, QSPI)

| Signal | GPIO | Schematic net | Notes |
|--------|------|---------------|-------|
| CS     | 9    | `OLED_CS`     | |
| CLK    | 10   | `OLED_CLK`    | |
| D0     | 11   | `OLED_SIO0`   | QSPI data line 0 |
| D1     | 12   | `OLED_SI1`    | QSPI data line 1 |
| D2     | 13   | `OLED_SI2`    | QSPI data line 2 |
| D3     | 14   | `OLED_SI3`    | QSPI data line 3 |
| RESET  | 21   | `OLED_RESET`  | active low |
| TE     | n/a  | —             | not used — Waveshare demo does not wire TE |

### Touch controller (FT3168, I²C)

| Signal | GPIO | Schematic net | Notes |
|--------|------|---------------|-------|
| SDA    | 47   | `TP_SDA`      | shared bus |
| SCL    | 48   | `TP_SCL`      | shared bus |
| INT    | n/a  | `TP_INT`      | not used — demo polls I²C directly, no INT line to MCU |
| RESET  | n/a  | `TP_RESET`    | not used — no separate reset pin |

### IMU (QMI8658, I²C)

| Signal | GPIO | Schematic net | Notes |
|--------|------|---------------|-------|
| SDA    | 47   | `IMU_SDA`     | shared bus |
| SCL    | 48   | `IMU_SCL`     | shared bus |
| INT1   | 46   | `IMU_INT1`    | strapping pin — fine as input |
| INT2   | n/c  | —             | not connected |

I²C address is `0x6A` or `0x6B` depending on the SDO/SA0 strap. **Verify with `i2cdetect` on first build** before hardcoding.

### TF card (SPI mode)

| Signal | GPIO | Schematic net |
|--------|------|---------------|
| CS     | 38   | `SD_CS`       |
| MOSI   | 39   | `SD_MOSI`     |
| MISO   | 40   | `SD_MISO`     |
| SCLK   | 41   | `SD_SCLK`     |

4-bit SDIO mode is **not** available on this board — the SDIO data pins are tied to the SPI nets through 0Ω resistors, so all four SPI pins must function for SD access.

### Battery and system

| Signal       | GPIO   | Notes |
|--------------|--------|-------|
| Battery ADC  | 4      | `BAT_ADC` — read through resistor divider, calibrate before trusting voltage |
| BOOT button  | 0      | strapping pin — do not drive at boot |
| RST button   | CHIP_PU| hardware reset, not a GPIO |
| USB-Serial TX| 43     | `U0TXD` — console output |
| USB-Serial RX| 44     | `U0RXD` — console input |

---

## External peripherals (header-mounted)

These are connected through the P1/P2 headers, so the GPIO assignments are choices, not constraints.

### GPS module (BN-880, NMEA over UART, 9 600 baud)

| Signal       | GPIO | Notes |
|--------------|------|-------|
| MCU RX ← GPS TX | 15 | UART2; on header P1, free |
| MCU TX → GPS RX | 18 | UART2; on header P1, free; optional (module works receive-only) |

The BN-880 also carries an **HMC5883L compass** on I²C (address `0x1E`). Wire SDA/SCL to the shared I²C bus on header P2 alongside the PCA9685 and INA219.

---

### ELRS receiver (CRSF over UART, 420 000 baud, 8N1)

| Signal       | GPIO | Notes |
|--------------|------|-------|
| MCU RX ← Rx TX | 16 | recommended; on header P1, free |
| MCU TX → Rx RX | 17 | recommended; on header P1, free |

Constraints:
- Must use a hardware UART (UART1 or UART2 on the ESP32-S3). Bit-banged 420k won't be reliable.
- Don't use UART0 — that's the USB-Serial console.

### PCA9685 servo driver (I²C, default address `0x40`)

| Signal | GPIO | Notes |
|--------|------|-------|
| SDA    | 47   | shared with onboard touch + IMU on header P2 |
| SCL    | 48   | shared with onboard touch + IMU on header P2 |
| /OE    | TODO | tie to GND for always-enabled, or assign a free GPIO if servo cutoff on failsafe is wanted |

Channel assignments:

| PCA9685 channel | Function           |
|-----------------|--------------------|
| 0               | Rudder servo       |
| 1               | Sail winch servo   |
| 2               | Motor ESC          |
| 3–15            | unused             |

### INA228 current/voltage sensor (I²C)

| Signal | GPIO | Notes |
|--------|------|-------|
| SDA    | 47   | shared bus |
| SCL    | 48   | shared bus |

⚠ The INA228's default address is `0x40`, which **conflicts with the PCA9685**. Set A0 = VS and A1 = GND to land at `0x41`. The firmware assumes `0x41`.

---

## I²C bus summary

A single I²C bus on GPIO47/48 carries everything. Confirmed devices and addresses:

| Address   | Device    | Notes |
|-----------|-----------|-------|
| 0x1E      | HMC5883L  | compass on BN-880 GPS module (external) |
| 0x38      | FT3168    | touch controller (onboard) — confirmed from Waveshare demo |
| 0x40      | PCA9685   | servo driver (external) |
| 0x41      | INA228    | current/voltage sensor (external; A0=VS, A1=GND) |
| 0x6A/0x6B | QMI8658   | IMU (onboard) — verify which |

Run `i2cdetect`-equivalent at first power-up and check this list matches.

---

## Free GPIOs

Broken out on header P1 or P2 with no onboard function:

`GPIO1, GPIO5, GPIO6, GPIO7, GPIO8, GPIO42, GPIO45`

*(GPIO2/3 = bilge sensor/pump; GPIO15/18 = GPS UART2; GPIO16/17 = ELRS UART1 — all taken)*

**Caveats:**
- GPIO45 is a strapping pin — fine as input or as output after boot, but don't pull it during reset.
- GPIO42 is unlabeled in the schematic's GPIO table — verify it's actually broken out before planning to use it.

## Reserved / strapping pins (avoid unless you know what you're doing)

| GPIO | Reason |
|------|--------|
| 0    | BOOT strapping pin (also onboard BOOT button) |
| 19   | USB D- |
| 20   | USB D+ |
| 43   | U0TXD (USB-Serial console) |
| 44   | U0RXD (USB-Serial console) |
| 45   | strapping pin (VDD_SPI voltage select) |
| 46   | strapping pin + IMU INT1 |

---

## Open questions / unverified pins

These are the items still marked `TODO` above. Resolve them by reading the Waveshare Arduino demo source in `docs/waveshare-demo/` and updating this file, `config.h`, and `CLAUDE.md` together.

- [x] Touch INT GPIO — not used; demo polls I²C directly
- [x] Touch RESET GPIO — not used; no separate reset pin
- [x] Display TE GPIO — not used; Waveshare demo does not wire TE
- [ ] IMU I²C address (0x6A vs 0x6B) — confirm with i2cdetect
- [ ] PCA9685 /OE pin assignment (or tie to GND)
- [ ] Confirm GPIO42 is broken out to a header
