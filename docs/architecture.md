# Architecture

## High-level data flow

```
┌─────────────────────────┐
│ User's browser          │
│  - touch / slider UI    │
│  - telemetry dashboard  │
└─────────┬───────────────┘
          │ HTTP + WebSocket
          │ (over Pi-hosted Wi-Fi AP)
          ▼
┌─────────────────────────────────────────────────────┐
│ Raspberry Pi 5  (base station)                      │
│                                                     │
│  ┌──────────────┐   ┌──────────────────────────┐    │
│  │ FastAPI app  │◄─►│ ELRS bridge (async task) │    │
│  │  - serves UI │   │  - talks CRSF over UART  │    │
│  │  - WS I/O    │   │  - packs channels out    │    │
│  └──────────────┘   │  - unpacks telemetry in  │    │
│                     └───────────┬──────────────┘    │
│                                 │                   │
│              ┌──────────────────┴────────────┐      │
│              │ RadioMaster Ranger Micro      │      │
│              │ (ELRS TX, USB-serial to Pi)   │      │
│              └──────────────────┬────────────┘      │
└─────────────────────────────────┼───────────────────┘
                                  │
                         2.4 GHz ELRS
                                  │
┌─────────────────────────────────┼───────────────────┐
│ Sailboat  (ESP32-S3)            │                   │
│                                 ▼                   │
│            ┌──────────────────────────┐             │
│            │ RadioMaster RP3 ELRS RX  │             │
│            │  CRSF over UART          │             │
│            └───────────┬──────────────┘             │
│                        │                            │
│                   ┌────▼────┐                       │
│                   │ ESP32-S3│                       │
│                   │  main   │                       │
│                   └──┬───┬──┘                       │
│            I²C       │   │     Telemetry out        │
│         ┌────────────┘   └───────────┐              │
│         ▼                            ▼              │
│  ┌──────────────┐             (back over CRSF       │
│  │   PCA9685    │              to ELRS RX → TX)     │
│  └──┬───┬───┬───┘                                   │
│     │   │   │                                       │
│   rudder │   ESC ──► motor                          │
│        winch                                        │
│                                                     │
│  Also on I²C bus: INA219 (current/voltage sense),   │
│                    onboard IMU (heel angle)         │
└─────────────────────────────────────────────────────┘
```

## Components

### Browser UI

- Plain HTML + vanilla JS, served by the Pi.
- Connects to the Pi over Wi-Fi (the Pi broadcasts its own AP — no router required at the sailing site).
- Sends control input to the Pi via WebSocket (low latency, continuous updates).
- Receives telemetry from the Pi via the same WebSocket.
- Designed to work on phones first. Controls: a sail trim slider, a rudder slider (or touch pad), a throttle slider, big red STOP button.

### Base station (Raspberry Pi 5, Python + FastAPI)

Two concurrent async tasks:

1. **Web task** — serves static files, handles WebSocket connections from browsers, maintains the current "desired state" (target sail / rudder / throttle positions).
2. **ELRS bridge task** — at a fixed rate (e.g., 50 Hz), reads the current desired state, packs it into a CRSF channel frame, and writes it to the transmitter's serial port. Also reads incoming CRSF frames (which include telemetry from the boat) and pushes decoded telemetry to the web task.

Only the Pi decides what goes into each ELRS channel. The browser sends high-level intent (e.g., `{rudder: 0.3, sail: 0.7, throttle: 0.0}`), not channel numbers.

### Boat firmware (ESP32-S3, C++ / PlatformIO)

Structured as cooperative modules invoked from `loop()`:

- **`elrs`** — reads the CRSF UART, exposes current channel values and link status.
- **`servos`** — knows how to drive each actuator via the PCA9685 given a normalized -1.0 .. 1.0 command.
- **`telemetry`** — reads the INA219 + IMU, packs a compact telemetry payload, and hands it to the CRSF layer for transmission back.
- **`failsafe`** — continuously checks ELRS link status. On loss of link, forces safe outputs (see `failsafe.md`).
- **`main`** — orchestrates the above at a steady loop rate.

No threads. No RTOS tasks (initially). A single fast loop is simpler to reason about and adequate for this workload.

## Latency budget

Target end-to-end (user input → servo movement): **< 100 ms**.

Rough allocation:
- Browser → Pi WebSocket: ~10 ms on local Wi-Fi.
- Pi desired-state → next ELRS packet: up to 20 ms at 50 Hz.
- ELRS air time: ~5–10 ms typical.
- Boat CRSF parse + PCA9685 I²C write: ~5 ms.
- Servo mechanical response: the dominant term, typically 50–100 ms.

Telemetry direction is less latency-sensitive — 2–5 Hz updates are fine.

## Why this architecture

- **Single radio link (ELRS only)**: reduces moving parts. Trade-off is tight telemetry bandwidth — schema must be compact (see `protocol.md`).
- **PCA9685 instead of ESP32 native PWM**: isolates timing-critical PWM generation from the MCU, gives clean stable pulses even if the firmware loop momentarily stalls.
- **Pi as the controller, not the boat**: lets us build a rich UI on a full OS. The boat only has to be a reliable servo mover.
- **Browser UI (not a native app)**: works on anything. No app store, no install, no iOS/Android split.
