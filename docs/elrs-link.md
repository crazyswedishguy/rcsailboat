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

## Control modes

The system supports three mutually exclusive control modes:

| Mode | Controller | Path | Web page |
|---|---|---|---|
| **1** | Boat ESP32-S3 WiFi AP | Phone → Boat AP (192.168.4.1) | Embedded in boat firmware (`wifi_ctrl.cpp`) |
| **2** | XIAO ESP32-S3 WiFi AP | Phone → XIAO AP (192.168.5.1) → UART1 → Ranger Micro → RF → Boat | Shared with Mode 1 (`shared/web/control_page.h`) |
| **3** | Raspberry Pi React UI | Browser → Pi (ws://) → USB-CDC → XIAO (byte-pump) → UART1 → Ranger Micro → RF → Boat | `base-station/static/` |

The XIAO automatically switches between Mode 2 and Mode 3 based on whether
the Pi base-station is actively driving it (see "Mode 2 / Mode 3 switching"
below).

## Architecture

**Mode 3 (Pi active):**
```
Browser ─ws─> FastAPI (Pi) ─USB-CDC─> XIAO (bridge) ─UART1 (CRSF)─> Ranger Micro
                                                                           │ RF (ELRS 2.4GHz)
                                                                           ▼
                                                                        RP3 ──CRSF──> ESP32-S3 ──I²C──> PCA9685 → servos/ESC
```

**Mode 2 (Pi absent, XIAO standalone):**
```
Phone ─WiFi─> XIAO AP (192.168.5.1) ─UART1 (CRSF)─> Ranger Micro
                     ↑ CRSF telemetry decoded here    │ RF (ELRS 2.4GHz)
                                                       ▼
                                                    RP3 ──CRSF──> ESP32-S3 ──I²C──> PCA9685 → servos/ESC
```

Telemetry flows the same path in reverse in both modes.

In Mode 3 the XIAO only shuttles bytes — all CRSF frame building
(`_build_rc_frame`) and parsing (`_decode_*`, `_parse_frames`) stays in
`base-station/app/elrs_bridge.py`. In Mode 2 the XIAO generates CRSF itself.

The bridge firmware lives in `crsf-bridge/` (separate PlatformIO project,
sibling of `boat-firmware/`).

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
- `Serial` (native USB-CDC, `ARDUINO_USB_CDC_ON_BOOT=1`) ↔ Pi (Mode 3) or
  unused (Mode 2).
- `Serial1` (hardware UART1 on GPIO43/44) ↔ Ranger Micro, fixed at
  **400000 baud** — this is the rate that matters for CRSF timing; the
  Pi-side baud for USB-CDC is nominal and doesn't need to match.
- In **Mode 3 (bridge)**: non-blocking bidirectional byte pump. Strips
  heartbeat frames (type `0x7E`) so they never reach the Ranger Micro.
- In **Mode 2 (standalone AP)**: WiFi AP at `192.168.5.1`, HTTP server on
  port 80 serving the shared control page, decodes CRSF telemetry from
  `Serial1`, builds and sends RC frames at 50 Hz.
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

## Mode 2 / Mode 3 dynamic switching

The XIAO decides which mode it is in based on whether the Pi
base-station is actively driving it:

### Detection signal — Pi heartbeat frame

`base-station/app/elrs_bridge.py` emits a **CRSF heartbeat frame** every
500 ms, interleaved with normal RC frames in `_tx_loop()`. The frame is:

```
[0xC8]  sync byte
[0x02]  length = type(1) + CRC(1), no payload
[0x7E]  custom frame type — "Pi liveness"
[CRC8]  CRC8-DVB-S2 over [0x7E] only
```

Total: 4 bytes. The Ranger Micro ignores unknown frame types, so this frame
is harmless if accidentally forwarded — but the XIAO **strips** it (by
decoding frame boundaries) so it never reaches the module.

### Switching logic (in `crsf-bridge/src/main.cpp`)

| Condition | Action |
|---|---|
| Heartbeat seen on `Serial` (USB) | Switch to **Mode 3** (bridge), tear down AP/HTTP server, flush buffers, reset commanded state to neutral/disarmed |
| No heartbeat for **2 000 ms** | Switch to **Mode 2** (standalone AP), bring up `192.168.5.1` AP + HTTP server, flush buffers, reset commanded state |
| Boot (cold start) | Start in **Mode 2** — Pi may not be running at all |

**Safety on every transition:** commanded state (rudder/sail/throttle/pump)
is reset to 0/0/0/off and armed status cleared before the new control
source takes over. This prevents a stale browser session from resuming
control on the wrong link.

### Hot-plug sequence

| Action | Expected |
|---|---|
| XIAO boots, no Pi connected | Mode 2: SSID "Mistral-2" / "readyabout" visible within ~2 s |
| Pi starts `elrs_bridge` → first heartbeat arrives | Mode 2 → Mode 3 within ~500 ms (first heartbeat); AP disappears |
| Pi app stops / USB unplugged | Mode 3 → Mode 2 within **2 s** (heartbeat timeout); AP reappears |
| Pi starts again | Mode 2 → Mode 3 again within ~500 ms |

### Mode 2 AP credentials

| Field | Value |
|---|---|
| SSID | `Mistral-2` |
| Password | `readyabout` |
| IP | `192.168.5.1` |
| Control page | `http://192.168.5.1/` |
| Map page | `http://192.168.5.1/map` |

(The boat's own Mode 1 AP is `Mistral` / `readyabout` / `192.168.4.1` —
different subnet so both can coexist without routing conflicts.)

## ELRS link configuration

To support 24 Hz attitude telemetry (Phase 1 of the firmware upgrade), the
ELRS link must be set to **250 Hz packet rate** with a **1:4 telemetry
ratio** (≈ 62 telemetry frames/s — comfortably above the ~32 frames/s
needed for one ATTITUDE + one SAILBOAT/BATTERY/DEVICES per 40 ms slot).

Configure both modules via their respective web UIs or Lua scripts:

| Module | Access | Setting |
|---|---|---|
| Ranger Micro (TX) | `http://10.0.0.1/` while in WiFi mode (hold button) or via ELRS Lua on Tx | Packet Rate = 250 Hz; Telem Ratio = 1:4 |
| RadioMaster RP3 (RX) | Follows the TX automatically after binding | (no separate config needed) |

After changing packet rate, rebind the pair. Confirm ≥ 24 attitude updates/s
in the Pi log or serial monitor before declaring the link healthy.

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
