# CLAUDE.md

This file gives Claude Code persistent context about the project. Read this first on every session.

## Project summary

A remote-controlled sailboat. A Raspberry Pi 5 base station hosts a web UI that any browser-capable device (phone, tablet, laptop) can connect to. User inputs from the browser are forwarded by the Pi to the boat over a 2.4 GHz ExpressLRS (ELRS) radio link. The boat is driven by an ESP32-S3 that reads ELRS channels and drives servos + an ESC, and sends telemetry back over ELRS.

## Key architectural decisions (don't revisit without asking)

- **Single radio link**: ELRS for both control and telemetry. No Wi-Fi or LoRa on the boat side. Telemetry bandwidth is therefore limited (~tens of bytes per second) and the telemetry design must respect that.
- **Base station language**: Python. Web framework: **FastAPI** (async, integrates cleanly with serial I/O to the ELRS module).
- **Boat firmware**: C++ on PlatformIO, Arduino framework, targeting the Waveshare ESP32-S3 1.64" AMOLED dev board.
- **Servo/ESC control**: All PWM goes through a PCA9685 over I²C. The ESP32 does not generate PWM directly.
- **ELRS protocol on the boat**: CRSF over UART between the RP3 receiver and the ESP32-S3.
- **Web UI**: Plain HTML + vanilla JS initially. No build step, no framework. Kept simple until the rest of the system works.

## Repo layout

```
base-station/   # Python / FastAPI app on the Pi
boat-firmware/  # PlatformIO project for ESP32-S3
shared/         # protocol constants (channel map, telemetry schema)
docs/           # design docs — read these before changing behavior
```

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

### Docs
- Update `docs/` when behavior changes. Out-of-date docs are worse than no docs.
- Pin assignments live in `docs/pinmap.md` AND `boat-firmware/src/config.h`. Keep them in sync.

## What to ask before doing

Ask the user (don't assume) before:
- Changing the ELRS channel mapping in `docs/protocol.md`.
- Changing the telemetry packet schema.
- Adding a new radio link or network interface to the boat.
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
