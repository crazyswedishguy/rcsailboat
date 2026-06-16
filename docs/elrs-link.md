# ELRS link: Pi → Ranger Micro

## Why this exists

The straightforward approach — wire the Ranger Micro TX module directly to
the Pi 5's GPIO UART (GPIO14/15, the RP1 southbridge UART) — does not work.
The Ranger Micro's LED never leaves "slow orange / No Handset Connection",
meaning it never accepts CRSF frames from the Pi, regardless of baud,
address byte, or frame content. This was confirmed not to be a protocol or
wiring problem: the Pi 5's RP1 UART has open kernel bugs
(`raspberrypi/linux` #6365, #6374) that break `select()`/`poll()`/`in_waiting`
behavior, and an independent report
([thedevanaproject.com](https://thedevanaproject.com/2025/01/27/how-to-send-rc-commands-with-a-raspberry-pi-and-elrs-transmitter-module/))
hit the identical failure with the identical TX module on a Pi 5, resolved
only by switching to a Pi 4.

Instead of changing the host computer, we put a microcontroller in the
middle: a **Seeed XIAO ESP32-S3** acting as a transparent USB↔UART bridge.
An MCU hardware UART has no trouble hitting CRSF's timing — it's what ELRS
hardware does internally — and this is the community-proven pattern (see
`ESP32-UART-Bridge` on GitHub).

## Architecture

```
Browser ─ws─> FastAPI (Pi) ─USB-CDC─> XIAO ESP32-S3 ─UART1 (CRSF)─> Ranger Micro
                                                                          │ RF (ELRS 2.4GHz)
                                                                          ▼
                                                                       RadioMaster RP3 ──CRSF/UART1──> ESP32-S3 ──I²C──> PCA9685 → servos/ESC
```

Telemetry flows the same path in reverse: boat ESP32-S3 → RP3 → RF →
Ranger Micro → XIAO bridge → Pi.

The XIAO only shuttles bytes — all CRSF frame building (`_build_rc_frame`)
and parsing (`_decode_*`, `_parse_frames`) stays in
`base-station/app/elrs_bridge.py`, unchanged from before. The bridge
firmware lives in `crsf-bridge/` (separate PlatformIO project, sibling of
`boat-firmware/`).

## Wiring

**XIAO ESP32-S3 → Ranger Micro JR-bay** (CRSF is a single bidirectional
half-duplex pin):

| XIAO pin | Signal | Connects to |
|---|---|---|
| D6 (GPIO43) | UART1 TX | 1 kΩ (or 470 Ω) series resistor → Ranger Micro JR-bay CRSF pin |
| D7 (GPIO44) | UART1 RX | Ranger Micro JR-bay CRSF pin (direct) |
| GND | — | Ranger Micro JR-bay GND |

- The XIAO is natively 3.3 V logic — matches the Ranger Micro CRSF pin, no
  level shifter needed.
- The XIAO connects to the Pi over USB-C, which both powers it and carries
  USB-CDC data (enumerates as `/dev/ttyACM0`).
- The XIAO's UART1 RX will see its own UART1 TX echoed back on the shared
  half-duplex wire; this is expected and harmless — the Pi-side parser
  ignores frame type `0x16` (RC_CHANNELS_PACKED) on receive.

**Ranger Micro power:** power it from the JR-bay 5V pin (not USB-C) so it
boots in operational/radio-module mode rather than USB-config mode.

**Binding:** both the Ranger Micro and the boat's RP3 receiver must share
the same UID. Bind phrase `darkandstormy` → UID `[68, 39, 235, 76, 236, 172]`
(confirmed via both modules' web UIs). `uart-inverted = false` on the TX.

**Boat side (unchanged):** RP3 → ESP32-S3 UART1, GPIO16 RX / GPIO17 TX, CRSF
@ 420000 baud — see `docs/pinmap.md`.

## Bridge firmware (`crsf-bridge/`)

- PlatformIO project, `board = seeed_xiao_esp32s3`, `framework = arduino`.
- `Serial` (native USB-CDC, `ARDUINO_USB_CDC_ON_BOOT=1`) ↔ Pi.
- `Serial1` (hardware UART1 on GPIO43/44) ↔ Ranger Micro, fixed at
  **400000 baud** in firmware — this is the rate that matters for CRSF
  timing; the Pi-side baud for the USB-CDC port is nominal and doesn't need
  to match.
- `loop()` is a non-blocking bidirectional byte pump, no framing logic.
- Build/flash via the `esp-pio-handling` skill: `cd crsf-bridge && pio run
  && pio run -t upload`.

## Pi-side configuration

In `/etc/rcsailboat.env`:

```
ELRS_PORT=/dev/elrs-tx   # stable symlink from setup-udev.sh, backed by /dev/ttyACM0
ELRS_BAUD=400000
```

If the Ranger Micro's LED won't leave "slow orange", change the UART rate in
the **bridge firmware** (`crsf-bridge/src/main.cpp`), not here — the Pi-side
baud is nominal for the USB-CDC link and doesn't reach the RF side.

## Verification (do these in order; stop and debug at the first failure)

0. **Bridge loopback:** flash `crsf-bridge`, jumper UART1 TX↔RX on the XIAO,
   open `/dev/ttyACM0` from a Python REPL, write bytes, confirm they echo
   back. Proves the USB-CDC↔UART1 path works before any RF hardware is involved.
1. **Wire & power:** connect XIAO UART1 to the Ranger Micro JR-bay (resistor
   on TX), power the Ranger Micro from JR-bay 5V, USB-C unplugged from it.
2. **Pi → Ranger Micro accepts CRSF:** start the base station against
   `/dev/ttyACM0` (or `/dev/elrs-tx`), watch the Ranger Micro LED. Success =
   it leaves slow-orange "No Handset" (→ searching / fading-blue).
3. **RF link TX↔RP3:** power-cycle the RP3, confirm it binds (LED solid).
4. **RP3 → ESP32 channels:** `pio device monitor` on the boat, move browser
   sliders, confirm the ESP32's `elrs: rud=… sail=… thr=… LQ=… RSSI=…` debug
   line tracks the inputs.
5. **ESP32 → servos:** confirm motion on the PCA9685-driven servos/ESC
   (respect failsafe — throttle stays neutral until armed).
6. **Telemetry return:** confirm `BATTERY_SENSOR`/`LINK_STATISTICS`/etc.
   decode on the Pi (`telem.bridge_connected` true, RSSI/LQ populated in the
   web UI).

## Fallback

If the XIAO bridge still can't get the Ranger Micro's LED out of orange,
that points at the module's mode/power or binding rather than transport —
recheck JR-bay power vs. USB-C mode, UID match on both modules, and try the
bridge firmware at 420000 baud instead of 400000. Moving the base station to
a Pi 4 is the last-resort fallback; not pursued unless the above all check out.
