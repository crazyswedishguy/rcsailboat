# CLAUDE.md

This file gives Claude Code persistent context about the project. Read this first on every session.

## Project summary

A remote-controlled sailboat with three control modes:

- **Mode 1** — Phone connects to the **boat ESP32-S3** WiFi AP (`Mistral` / `192.168.4.1`). The boat serves the embedded control page directly.
- **Mode 2** — Phone connects to the **XIAO ESP32-S3** WiFi AP (`Mistral-2` / `192.168.5.1`). The XIAO serves the same control page and generates CRSF frames autonomously over its UART1 → Ranger Micro → RF → boat.
- **Mode 3** — Browser connects to the **Raspberry Pi** React UI. The Pi builds CRSF over USB-CDC → XIAO (byte-pump) → Ranger Micro → RF → boat.

The XIAO (`crsf-bridge/`) auto-switches between Mode 2 (standalone AP) and Mode 3 (byte-pump bridge) based on whether the Pi's heartbeat frame (CRSF type `0x7E`) is seen on its USB serial — see `docs/elrs-link.md`.

## Key architectural decisions (don't revisit without asking)

- **Single radio link**: ELRS for both control and telemetry. No Wi-Fi or LoRa on the boat side. Telemetry bandwidth is therefore limited (~tens of bytes per second) and the telemetry design must respect that.
- **Base station language**: Python. Web framework: **FastAPI** (async, integrates cleanly with serial I/O to the ELRS module).
- **Boat firmware**: C++ on PlatformIO, Arduino framework, targeting the Waveshare ESP32-S3 1.64" AMOLED dev board.
- **Servo/ESC control**: All PWM goes through a PCA9685 over I²C. The ESP32 does not generate PWM directly.
- **ELRS protocol on the boat**: CRSF over UART between the ELRS receiver and the ESP32-S3.
- **Web UI**: Plain HTML + vanilla JS initially. No build step, no framework. Kept simple until the rest of the system works.

## Repo layout

```
base-station/      # Python / FastAPI app on the Pi (Mode 3 host)
boat-firmware/     # PlatformIO project for ESP32-S3 (Mode 1 host, boat controller)
crsf-bridge/       # PlatformIO project for XIAO ESP32-S3 (Mode 2 standalone AP + Mode 3 byte-pump)
shared/
  ├── protocol.py           # CRSF channel/frame constants (Python)
  └── web/
      ├── control_page.h    # HTML_PAGE[] PROGMEM — served by boat (Mode 1) and XIAO (Mode 2)
      └── map_page.h        # MAP_HTML[] PROGMEM — same sharing
docs/
  ├── pinmap.md         # canonical pin assignments — keep in sync with config.h
  ├── failsafe.md       # failsafe behavior — read before changing motor/throttle code
  ├── protocol.md       # ELRS channel mapping, telemetry packet schema (PROTOCOL_VERSION=2)
  ├── elrs-link.md      # bridge wiring, Mode 2/3 switching, heartbeat, link config
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

Canonical sources: **`docs/pinmap.md`** and **`boat-firmware/src/config.h`**. Read those for full assignments. Key facts for quick reference:

- **I²C bus**: GPIO47 SDA / GPIO48 SCL — shared by FT3168 (0x38), PCA9685 (0x40), INA228 (0x41), HMC5883L (0x1E), QMI8658 (0x6A/0x6B)
- **ELRS UART1**: GPIO16 RX / GPIO17 TX — 420 000 baud, 8N1; must use hardware UART, not UART0
- **GPS UART2**: GPIO15 RX / GPIO18 TX — 9 600 baud
- **Bilge sensor**: GPIO2 (ADC1_CH1, analogRead) / **Bilge pump MOSFET gate**: GPIO3
- **TF card SPI**: GPIO38 CS / GPIO39 MOSI / GPIO40 MISO / GPIO41 SCLK (internal PCB traces)
- **Battery ADC**: GPIO4 (onboard resistor divider — calibrate before trusting)
- **Free GPIOs**: GPIO1, GPIO5, GPIO6, GPIO7, GPIO8, GPIO42, GPIO45

## Coding conventions

### Python (base station)
- Target Python 3.11+.
- Use type hints on all function signatures.
- Use `ruff` for linting/formatting (line length 100).
- Use `asyncio` for concurrency; don't spawn threads unless there's a reason.
- Serial I/O via `pyserial-asyncio`.
- Dependencies pinned in `requirements.txt`; use a venv.
- Logging via `logging` module, not `print`, except in tiny scripts.

### C++ (boat firmware and XIAO bridge)
- PlatformIO, Arduino framework for ESP32-S3.
- **Boat firmware** (`boat-firmware/`): use AlfredoCRSF or CRSFforArduino library for ELRS CRSF parsing — do **not** write a CRSF parser from scratch unless asked.
- **XIAO bridge** (`crsf-bridge/`): the CRSF codec is hand-written (ported from the validated Python implementation) because AlfredoCRSF models the flight-controller role (decode RC, send telemetry), which is the inverse of what the XIAO needs as a handset. See `crsf-bridge/src/main.cpp`.
- Use Adafruit_PWMServoDriver for the PCA9685.
- One module per concern: `elrs.{h,cpp}`, `servos.{h,cpp}`, `telemetry.{h,cpp}`, `failsafe.{h,cpp}`, `main.cpp`.
- Avoid blocking calls in `loop()`. No `delay()` longer than a few ms.
- All GPIO pin numbers live in `config.h` — nowhere else.
- The shared control page HTML lives in `shared/web/control_page.h` and `shared/web/map_page.h` (PROGMEM). Both `boat-firmware` and `crsf-bridge` include via `-I"${PROJECT_DIR}/../shared"` in `platformio.ini`.
- For chip-level work on CO5300 / FT3168 / QMI8658: start from `docs/Waveshare Demo/`, don't write drivers from scratch. Register details for these chips are sparse online and easy to get wrong.

### ELRS channel quick-reference (PROTOCOL_VERSION = 2)

| Index (0-based) | Name | Range | Notes |
|---|---|---|---|
| 0 | CH_RUDDER | −1 … +1 | centered; −1 = full port |
| 1 | CH_SAIL | 0 … +1 | unipolar; 0 = fully eased |
| 2 | CH_THROTTLE | 0 … +1 | unipolar; 0 = neutral/stop |
| 3 | CH_ARM | 0 or 1 | ≥ 0.5 = armed |
| 4 | CH_MODE | — | reserved |
| 5 | CH_RESTART | 0 or 1 | ≥ 0.5 = restart signal |
| 6 | CH_PUMP | 0 or 1 | ≥ 0.5 = bilge pump on |
| 7–15 | — | — | reserved |

Canonical definitions: `boat-firmware/src/protocol.h` (C++), `shared/protocol.py` (Python).

### Build / flash / monitor
- `pio run` — build
- `pio run -t upload` — flash over USB
- `pio device monitor` — open serial monitor
- After making changes, run `pio run` and report compile errors before declaring the change done.

### Docs
- Update `docs/` when behavior changes. Out-of-date docs are worse than no docs.
- Pin assignments live in `docs/pinmap.md` AND `boat-firmware/src/config.h`. Keep them in sync.

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

- [x] Raspberry Pi 5 powered and on network (192.168.4.32)
- [ ] ELRS transmitter module connected to Pi
- [x] ESP32-S3 dev board powered (esp32-s3-full firmware flashed 2026-06-16; GPS on UART2, HMC5883L compass on I²C 0x1E)
- [ ] PCA9685 wired to ESP32-S3 (I²C) — absent at boot, servo driver disabled
- [ ] ELRS receiver wired to ESP32-S3 (UART, CRSF)
- [ ] Rudder servo on PCA9685
- [ ] Sail winch servo on PCA9685
- [ ] Motor ESC on PCA9685
- [ ] INA228 current sensor wired — absent at boot, power monitor disabled

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
