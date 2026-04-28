# CLAUDE.md

This file gives Claude Code persistent context about the project. Read this first on every session.

## Project summary

A remote-controlled sailboat. A Raspberry Pi 5 base station hosts a web UI that any browser-capable device (phone, tablet, laptop) can connect to. User inputs from the browser are forwarded by the Pi to the boat over a 2.4 GHz ExpressLRS (ELRS) radio link. The boat is driven by an ESP32-S3 that reads ELRS channels and drives servos + an ESC, and sends telemetry back over ELRS.

## Key architectural decisions (don't revisit without asking)

- **Single radio link**: ELRS for both control and telemetry. No Wi-Fi or LoRa on the boat side. Telemetry bandwidth is therefore limited (~tens of bytes per second) and the telemetry design must respect that.
- **Base station language**: Python. Web framework: **FastAPI** (async, integrates cleanly with serial I/O to the ELRS module).
- **Boat firmware**: C++ on PlatformIO, Arduino framework, targeting the Waveshare ESP32-S3 1.64" AMOLED dev board.
- **Servo/ESC control**: All PWM goes through a PCA9685 over I²C. The ESP32 does not generate PWM directly.
- **ELRS protocol on the boat**: CRSF over UART between the ELRS receiver and the ESP32-S3.
- **Web UI**: Plain HTML + vanilla JS initially. No build step, no framework. Kept simple until the rest of the system works.

## Repo layout

```
base-station/      # Python / FastAPI app on the Pi
boat-firmware/     # PlatformIO project for ESP32-S3
shared/            # protocol constants (channel map, telemetry schema)
docs/
  ├── pinmap.md         # canonical pin assignments — keep in sync with config.h
  ├── failsafe.md       # failsafe behavior — read before changing motor/throttle code
  ├── protocol.md       # ELRS channel mapping, telemetry packet schema
  ├── datasheets/       # CO5300, FT3168, QMI8658, ESP32-S3 TRM
  └── Waveshare Demo/   # vendor reference Arduino code (READ-ONLY reference)
```

## Boat hardware

**Board:** Waveshare ESP32-S3-Touch-AMOLED-1.64

- **MCU:** ESP32-S3R8 — Xtensa LX7 dual-core @ 240 MHz, 512 KB SRAM, 8 MB PSRAM, 16 MB Flash
- **Display:** 1.64" AMOLED 280×456, **CO5300** driver over **QSPI**. Uncommon controller — TFT_eSPI does NOT support it. Use Waveshare's driver from `docs/waveshare-demo/`.
- **Touch:** **FT3168** capacitive, I²C
- **IMU:** **QMI8658** 6-axis (accel + gyro), I²C. On board but **not currently used** — see "What to ask before doing."
- **Storage:** TF card slot (SPI; 4-bit SDIO is wired but not accessible due to pin sharing)
- **Power:** USB-C + 3.7 V LiPo via MX1.25, charge IC ETA6098

The board's I²C bus (GPIO47 SDA / GPIO48 SCL) is shared by the touch controller, the IMU, and the header pins on P2. The **PCA9685 servo driver lives on this same bus** (default address 0x40 — no conflict with onboard devices).

### GPIO constraints when picking pins for new peripherals
- **GPIO0** is a strapping pin (BOOT button). Don't drive during boot.
- **GPIO45, GPIO46** are strapping pins. GPIO46 is already taken by IMU INT1.
- **GPIO19/20** are USB D-/D+. Only repurpose if native USB isn't needed.
- **GPIO43/44** are U0TXD/U0RXD (USB-Serial console). Only repurpose if giving up the console.

## Pin map

Canonical sources are `docs/pinmap.md` and `boat-firmware/src/config.h`. This section is Claude's quick reference — **if a pin changes here, update both other sources too.**

### Onboard peripherals (verified against schematic)

```
AMOLED display (CO5300, QSPI)
  CS    = GPIO9    (OLED_CS)
  CLK   = GPIO10   (OLED_CLK)
  D0    = GPIO11   (OLED_SIO0)
  D1    = GPIO12   (OLED_SI1)
  D2    = GPIO13   (OLED_SI2)
  D3    = GPIO14   (OLED_SI3)
  RESET = GPIO21   (OLED_RESET)
  TE    = not used — Waveshare demo does not wire or use TE

Touch controller (FT3168, I²C)
  SDA   = GPIO47   (shared bus)
  SCL   = GPIO48   (shared bus)
  INT   = not used — demo polls I²C directly, no INT pin wired to MCU
  RESET = not used — no separate reset; shares power-on reset with rest of board
  Address = 0x38   (confirmed from Waveshare demo lcd_config.h)

IMU (QMI8658, I²C)
  SDA   = GPIO47   (shared bus)
  SCL   = GPIO48   (shared bus)
  INT1  = GPIO46
  INT2  = not connected
  Address = 0x6A or 0x6B depending on SDO/SA0 strap — verify with i2cdetect on first build.

TF card (SPI mode)
  CS    = GPIO38
  MOSI  = GPIO39
  MISO  = GPIO40
  SCLK  = GPIO41
  Note: 4-bit SDIO is NOT available on this board (pin sharing via 0Ω resistors).

Battery / system
  Battery ADC = GPIO4    (BAT_ADC, via resistor divider — requires calibration)
  BOOT button = GPIO0    (strapping pin; do not drive at boot)
  RST button  = CHIP_PU  (hardware reset, not a GPIO)
  USB-Serial  = GPIO43 (TX) / GPIO44 (RX)
```

### External peripherals (header-mounted, to be wired)

```
ELRS receiver (CRSF over UART, 420000 baud, 8N1)
  RX (boat ← rx) = TODO   (recommend GPIO16 — free on header P1)
  TX (boat → rx) = TODO   (recommend GPIO17 — free on header P1)
  Must use a hardware UART (UART1 or UART2). Do NOT bit-bang at 420k.
  Don't use UART0 — that's the USB-Serial console.

PCA9685 servo driver (I²C, default address 0x40)
  SDA = GPIO47, SCL = GPIO48 (P2 header — shared with touch + IMU)
  /OE = tie to GND, or assign a free GPIO if servo cutoff is wanted on failsafe
  Channel assignments live in config.h:
    Ch 0 → rudder servo
    Ch 1 → sail winch servo
    Ch 2 → motor ESC

INA219 current sensor (I²C)
  Default address is also 0x40 — CONFLICTS with PCA9685.
  Set INA219 address straps so it lands at 0x41 or higher before wiring up.
```

### Free GPIOs available on headers P1 / P2

Not assigned by the board, safe to use for new I/O:
**GPIO1, GPIO2, GPIO3, GPIO5, GPIO6, GPIO7, GPIO8, GPIO15, GPIO16, GPIO17, GPIO18, GPIO42, GPIO45**

(GPIO45 is a strapping pin — fine as input or post-boot output, but don't pull it during reset.)

## Coding conventions

### Python (base station)
- Target Python 3.11+.
- Use type hints on all function signatures.
- Use `ruff` for linting/formatting (line length 100).
- Use `asyncio` for concurrency; don't spawn threads unless there's a reason.
- Serial I/O via `pyserial-asyncio`.
- Dependencies pinned in `requirements.txt`; use a venv.
- Logging via `logging` module, not `print`, except in tiny scripts.

### C++ (boat firmware)
- PlatformIO, Arduino framework for ESP32-S3.
- Use the AlfredoCRSF or CRSFforArduino library for ELRS CRSF parsing — do **not** write a CRSF parser from scratch unless asked.
- Use Adafruit_PWMServoDriver for the PCA9685.
- One module per concern: `elrs.{h,cpp}`, `servos.{h,cpp}`, `telemetry.{h,cpp}`, `failsafe.{h,cpp}`, `main.cpp`.
- Avoid blocking calls in `loop()`. No `delay()` longer than a few ms.
- All GPIO pin numbers live in `config.h` — nowhere else.
- For chip-level work on CO5300 / FT3168 / QMI8658: start from `docs/Waveshare Demo/`, don't write drivers from scratch. Register details for these chips are sparse online and easy to get wrong.

### Build / flash / monitor
- `pio run` — build
- `pio run -t upload` — flash over USB
- `pio device monitor` — open serial monitor
- After making changes, run `pio run` and report compile errors before declaring the change done.

### Docs
- Update `docs/` when behavior changes. Out-of-date docs are worse than no docs.
- Pin assignments live in `docs/pinmap.md` AND `boat-firmware/src/config.h`. Keep them in sync (and with the pin map section above).

## What to ask before doing

Ask the user (don't assume) before:
- Changing the ELRS channel mapping in `docs/protocol.md`.
- Changing the telemetry packet schema.
- Adding a new radio link or network interface to the boat.
- Enabling the onboard **display**, **touch**, or **IMU** in the firmware (available hardware, but not currently in scope).
- Installing a new Python or C++ dependency.
- Widening the failsafe behavior (e.g., making throttle non-zero on signal loss).

## Work in slices, not monoliths

Prefer small, individually-testable increments over large code drops. A good slice:
1. Compiles / runs on its own.
2. Can be bench-tested with the hardware currently wired up.
3. Has a clear acceptance test the user can run.

Check `TODO.md` for the current phase. Don't jump ahead — later phases often assume hardware that isn't wired yet.

## Hardware currently on the bench

Update this section as hardware gets wired up. Claude Code should treat anything not listed here as "not yet connected — stub it."

- [ ] Raspberry Pi 5 powered and on network
- [ ] ELRS transmitter module connected to Pi
- [ ] ESP32-S3 dev board powered
- [ ] PCA9685 wired to ESP32-S3 (I²C)
- [ ] ELRS receiver wired to ESP32-S3 (UART, CRSF)
- [ ] Rudder servo on PCA9685
- [ ] Sail winch servo on PCA9685
- [ ] Motor ESC on PCA9685
- [ ] INA219 current sensor wired

## Safety first

This thing has a spinning motor, a battery that can catch fire, and runs outdoors on water. Before suggesting anything that changes power, motor, or failsafe behavior, re-read `docs/failsafe.md` and flag the change explicitly.

## Useful references

- ELRS docs: https://www.expresslrs.org/
- CRSF protocol: https://github.com/crsf-wg/crsf/wiki
- PCA9685 datasheet: https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf
- Waveshare ESP32-S3 AMOLED board: https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.64
- ESP32-S3 Technical Reference Manual: https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf
- QMI8658 datasheet: in `docs/datasheets/`
- FT3168 datasheet: in `docs/datasheets/`
- CO5300 datasheet: in `docs/datasheets/`
