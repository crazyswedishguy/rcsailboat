# RC Sailboat

A remote-controlled sailboat controlled from any mobile device (phone, tablet, laptop) via a Raspberry Pi base station. The base station speaks to the boat over a 2.4 GHz ExpressLRS (ELRS) radio link.

## System at a glance

```
 ┌────────────────┐       Wi-Fi       ┌────────────────┐    2.4 GHz ELRS    ┌──────────────┐
 │ User's device  │ ◄───────────────► │  Raspberry Pi  │ ◄────────────────► │   Sailboat   │
 │ (browser UI)   │   (Pi-hosted AP)  │  base station  │  (control + tlm)   │  (ESP32-S3)  │
 └────────────────┘                   └────────────────┘                    └──────────────┘
```

- **User device**: any browser. No app install. Connects to a Wi-Fi network the Pi broadcasts.
- **Base station**: Raspberry Pi 5. Hosts the web UI, bridges user inputs to the ELRS transmitter, and displays telemetry coming back from the boat.
- **Boat**: ESP32-S3 reads ELRS channel values, drives rudder servo / sail winch servo / motor ESC via a PCA9685 servo driver, and streams telemetry back over ELRS.

## Repository layout

```
rc-sailboat/
├── README.md              # this file
├── CLAUDE.md              # persistent context for Claude Code
├── TODO.md                # phased build plan / task tracking
├── .gitignore
├── docs/
│   ├── hardware.md        # bill of materials + wiring
│   ├── architecture.md    # how the pieces fit together
│   ├── protocol.md        # ELRS channel mapping + telemetry format
│   ├── pinmap.md          # ESP32-S3 GPIO assignments
│   └── failsafe.md        # safety behavior spec
├── base-station/          # Python (FastAPI) app running on the Pi
├── boat-firmware/         # PlatformIO project for the ESP32-S3
└── shared/                # protocol constants shared between the two
```

## Status

Early planning / scaffolding. See [TODO.md](./TODO.md) for the phased build plan.

## Quick start (once implemented)

Placeholder — will be filled in as each phase is built. See TODO.md for current phase.

## License

Personal hobby project. No license selected yet.
