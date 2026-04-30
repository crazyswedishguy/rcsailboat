# RC Sailboat

A remote-controlled sailboat controlled from any mobile device (phone, tablet, laptop) via a Raspberry Pi base station. The Pi bridges browser inputs to the boat over a 2.4 GHz ExpressLRS (ELRS) radio link. The boat runs an ESP32-S3 that drives servos, an ESC, and a bilge pump, and streams live telemetry back.

## System overview

```
 ┌────────────────┐       Wi-Fi       ┌──────────────────────────┐    2.4 GHz ELRS    ┌──────────────────────┐
 │  User's device │ ◄───────────────► │   Raspberry Pi 5         │ ◄────────────────► │  Sailboat (ESP32-S3) │
 │  (browser UI)  │   (Pi-hosted AP)  │   FastAPI + ELRS bridge  │  (CRSF over UART)  │  servos, ESC, sensors│
 └────────────────┘                   └──────────────────────────┘                    └──────────────────────┘
```

- **User device**: any browser — no app install. Connects to a Wi-Fi network the Pi broadcasts.
- **Base station**: Raspberry Pi 5. Hosts the web UI, bridges user inputs to the ELRS transmitter at 50 Hz, decodes CRSF telemetry from the boat, and pushes it to the browser over WebSocket.
- **Boat**: Waveshare ESP32-S3 AMOLED dev board. Reads ELRS channel values, drives rudder / sail winch / motor ESC via a PCA9685, monitors the bilge, logs to SD card, and streams telemetry over ELRS.

## Repository layout

```
rcsailboat/
├── README.md                     # this file
├── CLAUDE.md                     # persistent context for Claude Code AI assistant
├── TODO.md                       # phased build plan and completion tracking
├── .gitignore
│
├── docs/
│   ├── pinmap.md                 # ESP32-S3 GPIO assignments (canonical — keep in sync with config.h)
│   ├── wiring.md                 # physical wiring guide with connection tables + Mermaid diagram
│   ├── protocol.md               # ELRS channel mapping + CRSF telemetry frame schema
│   ├── failsafe.md               # safety behavior specification
│   └── datasheets/               # CO5300, FT3168, QMI8658, ESP32-S3 TRM
│
├── boat-firmware/                # PlatformIO project — ESP32-S3 (C++/Arduino)
│   ├── platformio.ini            # build environments (base / gps / compass / full)
│   ├── include/                  # lv_conf.h (LVGL configuration)
│   └── src/
│       ├── main.cpp              # Arduino setup() / loop() — orchestrates all modules
│       ├── config.h              # ALL GPIO pins, I²C addresses, baud rates (single source of truth)
│       ├── protocol.h            # CRSF channel indices and telemetry frame type constants
│       ├── elrs.{h,cpp}          # ELRS receiver — CRSF parse + telemetry TX (Phase 3 stub)
│       ├── servos.{h,cpp}        # PCA9685 servo/ESC driver
│       ├── power.{h,cpp}         # INA219 current/voltage monitor
│       ├── imu.{h,cpp}           # QMI8658 IMU — roll/pitch/capsize detection
│       ├── compass.{h,cpp}       # HMC5883L compass (COMPASS_ENABLED builds only)
│       ├── gps.{h,cpp}           # TinyGPS++ NMEA parser (GPS_ENABLED builds only)
│       ├── telemetry.{h,cpp}     # CRSF telemetry frame builder + emitter
│       ├── bilge.{h,cpp}         # bilge water sensor + pump relay
│       ├── sdlog.{h,cpp}         # TF card CSV data logger
│       ├── wifi_ctrl.{h,cpp}     # WiFi Direct AP + embedded web UI (fallback control mode)
│       ├── display.{h,cpp}       # LVGL UI on AMOLED display (swipeable 6-screen tileview)
│       └── failsafe.{h,cpp}      # link-loss failsafe state machine (Phase 5 stub)
│
├── base-station/                 # Python FastAPI app — Raspberry Pi
│   ├── requirements.txt
│   └── app/
│       ├── main.py               # FastAPI app, WebSocket hub, HTTP endpoints
│       ├── elrs_bridge.py        # bidirectional CRSF over serial (50 Hz TX, async RX decode)
│       └── state.py              # shared dataclasses: DesiredState, GpsPosition, TelemetryState
│
├── shared/                       # protocol constants mirrored between Python and C++
│   └── protocol.py
│
└── tools/
    └── tile_downloader/          # OSM tile pre-downloader for the base-station map UI
        └── tile_downloader.py    # GUI mode (tkinter) or headless CLI mode for Pi
```

## Boat firmware

### Build environments

Four PlatformIO environments select which optional hardware is compiled in:

| Environment | `GPS_ENABLED` | `COMPASS_ENABLED` | Use when |
|---|---|---|---|
| `esp32-s3` | — | — | Base build; no GPS or compass hardware |
| `esp32-s3-gps` | yes | — | BN-880 GPS module wired to UART2 |
| `esp32-s3-compass` | — | yes | BN-880 compass (HMC5883L) wired to I²C |
| `esp32-s3-full` | yes | yes | Both GPS and compass wired up |

### Build, flash, and monitor

```sh
# Build (all environments)
pio run

# Build a specific environment
pio run -e esp32-s3-full

# Flash over USB
pio run -t upload -e esp32-s3-full

# Serial monitor (115200 baud)
pio device monitor
```

> On Windows: prefix `pio` commands with `PYTHONIOENCODING=utf-8` to avoid encoding errors in the progress bar.

### What the firmware does

1. **Setup**: initialises I²C, display (LVGL on FreeRTOS task), IMU, bilge sensor/pump, PCA9685 servos, INA219 power monitor, SD card logger, WiFi AP, ELRS stub, and optionally GPS + compass.
2. **Loop**: in WiFi Direct mode — serves HTTP, applies web UI slider values to servos; in ELRS mode — parses CRSF channels, applies to servos. Runs bilge debounce, reads INA219, updates LVGL display at 5 Hz, logs to SD card at 1 Hz, and emits CRSF telemetry on schedule.
3. **Display**: 6-screen swipeable tileview on the 1.64" AMOLED. Shows mode, link, voltage, GPS, attitude, bilge status, and compass heading.
4. **Telemetry**: CRSF frames streamed back over ELRS: Battery (2 Hz), Attitude (5 Hz), Sailboat custom (5 Hz), GPS (1 Hz when fix available).

### Two control modes

| Mode | Description |
|---|---|
| **WiFi Direct** (default) | ESP32 broadcasts its own Wi-Fi AP (`darkandstormy`). Connect any phone/tablet and open `192.168.4.1`. No Pi or ELRS required. |
| **ELRS** | Receives RC channels from the ELRS transmitter on the Pi (or a hand transmitter). Used for range and reliability on water. |

Mode is switched via the display touchscreen UI on the boat.

## Base station

### Requirements

- Python 3.11+
- `pip install -r base-station/requirements.txt` (in a venv)
- ELRS TX module connected via USB-Serial (default `/dev/ttyUSB0`)

### Run

```sh
cd base-station
uvicorn app.main:app --host 0.0.0.0 --port 8000
```

Then open `http://<pi-ip>:8000` on any browser on the same Wi-Fi network.

### Configuration

| Environment variable | Default | Description |
|---|---|---|
| `ELRS_PORT` | `/dev/ttyUSB0` | Serial device for the ELRS TX module |
| `ELRS_BAUD` | `420000` | Baud rate (must match ELRS module setting) |

### What the base station does

- Sends CRSF RC Channels Packed frames at 50 Hz to the ELRS TX module.
- Parses incoming CRSF telemetry frames (battery, attitude, GPS, sailboat custom) and broadcasts them to all connected browser clients over WebSocket.
- Provides a REST endpoint (`GET /telemetry`) for polling clients.
- Serves the web UI (`base-station/static/index.html`) with a Leaflet map, telemetry strip, and control sliders.
- Auto-reconnects to the ELRS module after a serial error or disconnect.

## Offline map tiles

The base-station map uses OpenStreetMap tiles served from the Pi itself (no internet needed on the water). Pre-download tiles with:

```sh
# GUI mode (on a desktop machine)
python tools/tile_downloader/tile_downloader.py

# Headless CLI mode (on the Pi)
python tools/tile_downloader/tile_downloader.py \
    --north 51.5 --south 51.4 --east -0.1 --west -0.2 \
    --zmin 12 --zmax 16 \
    --outdir base-station/static/tiles
```

## Hardware

### Boat

| Component | Part | Notes |
|---|---|---|
| MCU | Waveshare ESP32-S3-Touch-AMOLED-1.64 | ESP32-S3R8, 8 MB PSRAM, 16 MB Flash, 1.64" CO5300 AMOLED |
| Servo driver | PCA9685 | I²C 0x40; drives rudder, sail winch, motor ESC |
| Power monitor | INA219 | I²C 0x41 (A0 jumper bridged); shunt in main battery lead |
| GPS + compass | BN-880 | HMC5883L compass (I²C 0x1E); GPS NMEA on UART2 |
| ELRS receiver | e.g. EP1, EP2, SuperD | CRSF on UART1 (GPIO16/17) |
| SD card | Standard TF/microSD | Logs CSV data; also serves offline map tiles |

See [docs/wiring.md](docs/wiring.md) for full wiring tables and a system diagram.
See [docs/pinmap.md](docs/pinmap.md) for all GPIO assignments.

### Base station

| Component | Notes |
|---|---|
| Raspberry Pi 5 | Runs the FastAPI app |
| ELRS TX module | Connected via USB-Serial; any ELRS-compatible module works |

## Current status

Software is complete through Phase 6 (telemetry). All firmware modules compile across all four environments. **No hardware has been physically wired yet** — that is the next step.

Pending before water:
- [ ] Wire PCA9685, ELRS receiver, INA219, GPS/compass to ESP32-S3
- [ ] Flash and bench-test each firmware module
- [ ] Complete Phase 3 ELRS RX parsing (`elrs.cpp`)
- [ ] Complete Phase 5 failsafe state machine (`failsafe.cpp`)
- [ ] Connect ELRS TX module to Pi and run the full bridge

See [TODO.md](TODO.md) for the full phased plan.

## License

Personal hobby project. No license selected.
