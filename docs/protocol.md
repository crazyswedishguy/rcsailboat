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
| 5 | Mode | discrete: 0.0 = manual, 0.5 = (reserved), 1.0 = (reserved) | For future use |
| 6–16 | Reserved | — | Don't use without updating this doc |

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

## Telemetry link: boat → Pi

ELRS telemetry bandwidth is very limited. Budget: roughly a few hundred bytes per second, not a streaming channel. Plan accordingly.

### Strategy

Use CRSF's standard telemetry frame types where they fit (battery, GPS if ever added, attitude), rather than inventing custom frames. This is what ground stations like the RadioMaster transmitter natively understand.

### Frames used

| CRSF frame | Fields carried | Source | Rate |
|---|---|---|---|
| `BATTERY_SENSOR` (0x08) | voltage (dV), current (dA), capacity used (mAh), remaining % | INA219 + estimator | 2 Hz |
| `ATTITUDE` (0x1E) | pitch, roll, yaw (0.0001 rad) | Onboard IMU (roll = heel) | 5 Hz |
| `LINK_STATISTICS` (0x14) | RSSI, LQ, SNR, etc. | Auto-populated by ELRS stack | automatic |
| `FLIGHT_MODE` (0x21) | short text: "MANUAL", "FAILSAFE", "DISARMED" | Firmware state | on change |
| `GPS` (0x02) | lat (°×1e7 int32), lng (°×1e7 int32), groundspeed (km/h×10 uint16), heading (°×100 uint16), altitude (m+1000 uint16), satellites (uint8) | GPS module on UART2 — **optional**, requires `-DGPS_ENABLED` build flag | 1 Hz when fix acquired |

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
PROTOCOL_VERSION = 1
```
