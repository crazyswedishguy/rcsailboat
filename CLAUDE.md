# CLAUDE.md

This file gives Claude Code persistent context about the project. Read this first on every session.

## Project summary

A remote-controlled sailboat with three control modes:

- **Mode 1** — Phone connects to the **boat ESP32-S3** WiFi AP (`Mistral` / `192.168.4.1`). The boat serves the embedded control page directly.
- **Mode 2** — Phone connects to the **XIAO ESP32-S3** WiFi AP (`Mistral-2` / `192.168.5.1`). The XIAO serves the same control page and transmits CRSF RC frames over SX1262 GFSK directly to the boat.
- **Mode 3** — Browser connects to the **Raspberry Pi** React UI. The Pi sends CRSF over USB-CDC → XIAO → GFSK → boat; telemetry returns on the same path in reverse.

The XIAO (`crsf-bridge/`) auto-switches between Mode 2 (standalone AP) and Mode 3 (GFSK bridge) based on whether the Pi's heartbeat frame (CRSF type `0x7E`) is seen on its USB serial.

## Key architectural decisions (don't revisit without asking)

- **Single radio link**: SX1262 GFSK at 915 MHz, 150 kbps / 75 kHz deviation / 312 kHz RX BW, 14 dBm, for both control and telemetry. Switched from LoRa (SF7/BW500) once ~500 m range proved to need none of LoRa's processing gain — see `docs/lora-bringup.md` for that history. The Radiomaster Ranger Micro TX + RP3 receiver failed to link before either radio and have been removed. Raw CRSF frames are the GFSK packet payload format.
- **Base station language**: Python. Web framework: **FastAPI** (async, integrates cleanly with serial I/O via XIAO USB-CDC).
- **Boat firmware**: C++ on PlatformIO, Arduino framework, targeting the Waveshare ESP32-S3 1.64" AMOLED dev board.
- **Servo/ESC control**: All PWM goes through a PCA9685 over I²C. The ESP32 does not generate PWM directly.
- **Radio on the boat**: RadioLib SX1262 over software SPI (GPIO5/6/7) — both hardware SPI buses are occupied by display and SD card.
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
  ├── protocol.md       # RC channel mapping, telemetry packet schema (PROTOCOL_VERSION=4)
  ├── lora-bringup.md   # SX1262 LoRa bring-up debugging notes — read before touching elrs.cpp SPI/DIO1 code
  ├── elrs-link.md      # (superseded — describes old Ranger Micro / RP3 setup)
  ├── datasheets/       # CO5300, FT3168, QMI8658, ESP32-S3 TRM
  └── Waveshare Demo/   # vendor reference Arduino code (READ-ONLY reference)
```

## Boat hardware

**Board:** Waveshare ESP32-S3-Touch-AMOLED-1.64

- **MCU:** ESP32-S3R8 — Xtensa LX7 dual-core @ 240 MHz, 512 KB SRAM, 8 MB PSRAM, 8 MB Flash (GigaDevice GD25Q64, 0xC8/0x4017 — board docs say 16 MB, chip confirmed 8 MB via esptool flash-id)
- **Display:** 1.64" AMOLED 280×456, **CO5300** driver over **QSPI**. Uncommon controller — TFT_eSPI does NOT support it. Use Waveshare's driver from `docs/waveshare-demo/`.
- **Touch:** **FT3168** capacitive, I²C
- **IMU:** **QMI8658** 6-axis (accel + gyro), I²C. On board but **not currently used** — see "What to ask before doing."
- **Storage:** TF card slot (SPI; 4-bit SDIO is wired but not accessible due to pin sharing)
- **Power:** USB-C + 3.7 V LiPo via MX1.25, charge IC ETA6098

The board's I²C bus (GPIO47 SDA / GPIO48 SCL) is shared by the touch controller, the IMU, and the header pins on P2. The **PCA9685 servo driver lives on this same bus** (default address 0x40 — no conflict with onboard devices).

### GPIO constraints when picking pins for new peripherals
- **GPIO0** is a strapping pin (BOOT button). Don't drive during boot.
- **GPIO42** is reserved internally as `AMOLED_EN` — not broken out, not usable for anything.
- **GPIO45, GPIO46** are strapping pins. GPIO46 is already taken by IMU INT1.
- **GPIO19/20** are USB D-/D+. Only repurpose if native USB isn't needed.
- **GPIO43/44** are U0TXD/U0RXD (USB-Serial console). Only repurpose if giving up the console.
- There are **no free GPIOs left** on this board — every pin is already allocated. Any new peripheral requires either freeing an existing assignment or dropping a non-essential signal (as was done for SX1262 DIO1, which is now polled over SPI instead of wired to a GPIO).

## Pin map

Canonical sources: **`docs/pinmap.md`** and **`boat-firmware/src/config.h`**. Read those for full assignments. Key facts for quick reference:

- **I²C bus**: GPIO47 SDA / GPIO48 SCL — shared by FT3168 (0x38), PCA9685 (0x40), INA228 (0x41), HMC5883L (0x1E), QMI8658 (0x6A/0x6B)
- **SX1262 GFSK (boat)**: software SPI — CLK=GPIO5, MOSI=GPIO6, MISO=GPIO7, CS=GPIO8, RESET=GPIO1, BUSY=GPIO45, RXEN=GPIO16, TXEN=GPIO17. DIO1 is unwired (no free GPIO — GPIO42 is reserved internally as AMOLED_EN); IRQ status is polled over SPI instead.
- **GPS UART2**: GPIO15 RX / GPIO18 TX — 9 600 baud
- **Bilge sensor**: GPIO2 (ADC1_CH1, analogRead) / **Bilge pump MOSFET gate**: GPIO3
- **TF card SPI**: GPIO38 CS / GPIO39 MOSI / GPIO40 MISO / GPIO41 SCLK (internal PCB traces)
- **Battery ADC**: GPIO4 (onboard resistor divider — calibrate before trusting)
- **Free GPIOs**: none — all free GPIOs are now allocated to SX1262

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
- **Boat firmware** (`boat-firmware/`): radio uses RadioLib SX1262 (`jgromes/RadioLib @ ^7.0.0`) with a software SPI wrapper class in `elrs.cpp`. CRSF frame type constants now live in `protocol.h` — do not re-introduce AlfredoCRSF.
- **XIAO bridge** (`crsf-bridge/`): the CRSF frame encoder is hand-written (same as before); the radio transport is RadioLib SX1262 on hardware SPI. See `crsf-bridge/src/main.cpp`.
- Use Adafruit_PWMServoDriver for the PCA9685.
- One module per concern: `elrs.{h,cpp}`, `servos.{h,cpp}`, `telemetry.{h,cpp}`, `failsafe.{h,cpp}`, `main.cpp`.
- Avoid blocking calls in `loop()`. No `delay()` longer than a few ms.
- All GPIO pin numbers live in `config.h` — nowhere else.
- The shared control page HTML lives in `shared/web/control_page.h` and `shared/web/map_page.h` (PROGMEM). Both `boat-firmware` and `crsf-bridge` include via `-I"${PROJECT_DIR}/../shared"` in `platformio.ini`.
- For chip-level work on CO5300 / FT3168 / QMI8658: start from `docs/Waveshare Demo/`, don't write drivers from scratch. Register details for these chips are sparse online and easy to get wrong.

### RC channel quick-reference (PROTOCOL_VERSION = 3)

| Index (0-based) | Name | Range | Notes |
|---|---|---|---|
| 0 | CH_RUDDER | −1 … +1 | centered; −1 = full port |
| 1 | CH_SAIL | 0 … +1 | unipolar; 0 = fully eased |
| 2 | CH_THROTTLE | 0 … +1 | unipolar; 0 = neutral/stop |
| 3 | CH_ARM | 0 or 1 | ≥ 0.5 = armed |
| 4 | CH_MODE | 0 or 1 | > 0.5 held ≥300ms = remote request for GFSK control mode (mirrors the touchscreen "Enable GFSK" button); < 0.5 held ≥3s releases to WiFi mode (asymmetric debounce — leaving tears down the AP). See `docs/protocol.md` "Remote mode switching". |
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
- [x] ESP32-S3 dev board powered (esp32-s3-full firmware flashed 2026-06-28; GPS on UART2, HMC5883L compass on I²C 0x1E; GPS confirmed sending NMEA data — needs outdoor sky view for fix)
- [x] SX1262 GFSK module wired to ESP32-S3 (software SPI, GPIO5/6/7/8 + control pins) — radio link confirmed working on the workbench 2026-07-24 (RC control + telemetry round-trip, ~46-47 Hz)
- [x] SX1262 GFSK module wired to XIAO ESP32-S3 (hardware SPI, D8/D9/D10 + control pins) — radio link confirmed working on the workbench 2026-07-24
- [ ] PCA9685 wired to ESP32-S3 (I²C) — absent at boot, servo driver disabled
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
