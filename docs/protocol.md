# Protocol

This document defines the wire-level contract between the base station and the boat. **Both sides must agree on this.** When in doubt, this document wins; code must be changed to match, not vice versa.

## Control link: ELRS channels (Pi → boat)

ELRS supports up to 16 channels per packet (via CRSF). We use the first few; the rest are reserved.

| Channel | Purpose | Range | Notes |
|--------:|---------|-------|-------|
| 1 | Rudder | -1.0 (port) to +1.0 (starboard) | Neutral = 0.0 |
| 2 | Sail trim | 0.0 (sheeted out) to +1.0 (sheeted in) | Unipolar |
| 3 | Throttle | -1.0 (reverse) to +1.0 (forward) | Neutral = 0.0; the ESC must see neutral at arm |
| 4 | Arm / disarm | 0.0 = disarmed, 1.0 = armed | Required to be 1.0 for motor to respond |
| 5 | Mode | >0.5 = request ELRS control mode, <0.5 = release to WiFi mode | See "Remote mode switching" below |
| 6 | System restart | 0.0 = idle, 1.0 = restart requested | Hold at max for ≥ 2 s **while arm channel is low (disarmed)** → `ESP.restart()`. Disarmed gate is mandatory — never restart while motor could be running. Implement in Phase 3 alongside the CRSF channel handler. |
| 7 | Bilge pump | 0.0 = off, 1.0 = on | Manual override of the bilge pump from any control mode. The boat's automatic bilge logic still runs; this channel forces the pump on. Not gated by arm. |
| 8–16 | Reserved | — | Don't use without updating this doc |

### On-the-wire encoding

CRSF channel values are 11-bit integers, nominally 172 .. 1811 with 992 = center.
Both sides use this mapping between the normalized values above and the 11-bit integer:

```
  raw_11bit = round(992 + normalized * (1811 - 992 + 172 - 992) / 2 ... )
```

In practice, use the standard CRSF helpers from whichever library is chosen; don't hand-roll this. The contract is: **the Pi sends a normalized float; the boat reads a normalized float. Library internals are implementation detail.**

### Update rate

- Pi transmits channels at **50 Hz**.
- Boat reads channels as they arrive (CRSF is event-driven).
- If the boat sees no fresh channel frame for **500 ms**, it declares link loss (see `failsafe.md`).

### Remote mode switching (channel 5 / `CH_MODE`)

The boat's control input source — its own WiFi AP (`CtrlMode::WIFI`, default at
boot) or the ELRS receiver (`CtrlMode::ELRS`) — was originally toggled only by
a physical button on the boat's touchscreen. Since Mode 2/3 are meant to be
used *without* physical access to the boat, the boat firmware also honors a
remote request on `CH_MODE`:

- A live, CRC-valid CRSF stream with `CH_MODE > 0.5`, held for **≥300 ms**
  (debounce), switches the boat into `CtrlMode::ELRS` — the same effect as
  pressing "Enable ELRS" on the touchscreen (AP torn down, WiFi session
  zeroed). The XIAO (`crsf-bridge/`) asserts this whenever someone actually
  has the Mode 2 page open and polling `/control` within the last **2.5 s**
  (`MODE_REQUEST_TIMEOUT_MS`, looser than the 500 ms servo-safety timeout) —
  not merely because the XIAO and Ranger Micro happen to be powered on with
  no one connected. This is independent of arm state: switching into ELRS
  mode to check telemetry before arming is intentionally allowed. No separate
  "go ELRS" UI button is needed — opening the page is itself the signal.
- Releasing `CH_MODE` below 0.5 for **≥3 s** switches back to `CtrlMode::WIFI`.
  This debounce is deliberately asymmetric (300 ms to enter, 3 s to leave):
  entering should feel responsive, but leaving tears down and restarts the
  boat's own WiFi AP, so a brief polling gap (mobile network/JS jitter) must
  not flap the AP up and down — this was observed on the bench (rapid
  WiFi-stack errors) before the timeouts were loosened.
- There is **no arbitration** with an active WiFi session: a live ELRS stream
  is treated as just as strong a signal of present, deliberate operator intent
  as a touchscreen tap, so it always wins immediately.
- If the boat is in `CtrlMode::ELRS` and the ELRS link itself is lost for
  **>12 s** (independent of, and slower than, the 500 ms servo-failsafe
  response), the boat automatically reverts to `CtrlMode::WIFI` so a bystander
  can recover it from the boat's own AP without touching the screen.
- `base-station/app/elrs_bridge.py` (Mode 3 / Pi) does not yet set `CH_MODE` —
  it currently sends channel 5 centered, so Mode 3 still requires a manual
  touchscreen switch into ELRS mode until that's wired up.

## Telemetry link: boat → Pi

ELRS telemetry bandwidth is very limited. Budget: roughly a few hundred bytes per second, not a streaming channel. Plan accordingly.

### Strategy

Use CRSF's standard telemetry frame types where they fit (battery, GPS if ever added, attitude), rather than inventing custom frames. This is what ground stations like the RadioMaster transmitter natively understand.

### Frames used

| CRSF frame | Fields carried | Source | Rate |
|---|---|---|---|
| `BATTERY_SENSOR` (0x08) | voltage (dV), current (dA), capacity used (mAh), remaining % | INA219 + estimator | 2 Hz |
| `ATTITUDE` (0x1E) | pitch, roll, yaw (0.0001 rad) | Onboard IMU (roll = heel) | **24 Hz** |
| `LINK_STATISTICS` (0x14) | RSSI, LQ, SNR, etc. | Auto-populated by ELRS stack | automatic |
| `FLIGHT_MODE` (0x21) | short text: "MANUAL", "FAILSAFE", "DISARMED" | Firmware state | on change |
| `GPS` (0x02) | lat (°×1e7 int32), lng (°×1e7 int32), groundspeed (km/h×10 uint16), heading (°×100 uint16), altitude (m+1000 uint16), satellites (uint8) | GPS module on UART2 — **optional**, requires `-DGPS_ENABLED` build flag | 1 Hz when fix acquired |
| `SAILBOAT` (0x80, custom) | rudder/sail/ESC commanded (×10000 int16), MCU temp (°C×10 int16), **status byte** | Firmware state | 5 Hz |
| `DEVICES` (0x81, custom) | 10 × 2-bit device health levels (LSB-first) | `diag` module | 0.2 Hz |

### Custom SAILBOAT frame (0x80) — 9-byte payload (big-endian)

| Offset | Field | Encoding |
|---|---|---|
| 0–1 | rudder commanded | int16, ×10000 (−1.0..+1.0) |
| 2–3 | sail commanded | int16, ×10000 (0.0..+1.0) |
| 4–5 | ESC commanded | int16, ×10000 (−1.0..+1.0) |
| 6–7 | MCU temperature | int16, °C ×10 |
| 8 | **status** | bitfield (see below) |

Status byte bits (LSB = bit 0):

| Bit | Flag | Meaning |
|---|---|---|
| 0 | capsized | IMU reports the hull inverted |
| 1 | bilge_wet | bilge float switch triggered (water present) |
| 2 | pump_active | bilge pump is currently running |
| 3 | armed | arm channel high / motor live |
| 4 | failsafe | firmware in FAILSAFE state (link lost) |
| 5–7 | reserved | 0 |

This replaces the previous 8-byte SAILBOAT payload (the trailing status byte is new in PROTOCOL_VERSION 2). Remote control modes (Pi, XIAO) use these flags to render the same bilge / capsize / pump indicators the boat's own page shows.

### ELRS link configuration

Both the TX module (RadioMaster Ranger Micro) and the RX (RadioMaster RP3) must be set to:

- **Packet rate: 250 Hz**
- **Telemetry ratio: 1:4**

This yields ≈62 telemetry frames/s — enough for 24 Hz ATTITUDE + 2 Hz battery + 1 Hz GPS + 5 Hz SAILBOAT + occasional link-stats/devices (≈32 frames/s) with comfortable margin. Control uplink (the Pi/XIAO send RC at 50 Hz) is heavily oversampled by the remaining ~187 Hz of slots, so giving telemetry 1/4 of the budget costs nothing in practice. 250 Hz (over 500 Hz) keeps better receiver sensitivity / range for open-water use. This is a radio setting (set via the ELRS Lua/web config), not firmware — re-apply it after any module re-flash.

### Fields deliberately **not** sent (for bandwidth reasons)

- Commanded servo positions — the Pi already knows what it sent; no point echoing.
- Water temperature, wind speed — out of scope initially. If added later, use a custom CRSF frame ID and add it here first.

### Packet cadence

Total telemetry budget: one frame per 50–100 ms window. Round-robin through the above rather than sending everything every tick.

## Versioning

When any field in this document changes:

1. Update this document first.
2. Bump `PROTOCOL_VERSION` in `shared/protocol.py` and `boat-firmware/src/protocol.h`.
3. Both sides refuse to operate if versions don't match (log loudly, enter safe state on the boat).

```
PROTOCOL_VERSION = 3
```

### Version history

- **v3** — channel 5 (`CH_MODE`) repurposed from "reserved" to a remote ELRS-mode-switch request (see "Remote mode switching" above).
- **v2** — added channel 7 (bilge pump); added the SAILBOAT status byte (capsized/bilge_wet/pump_active/armed/failsafe); raised ATTITUDE to 24 Hz; specified the 250 Hz / 1:4 ELRS link config.
- **v1** — initial channel map + standard CRSF telemetry frames.
