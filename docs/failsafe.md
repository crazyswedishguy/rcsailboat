# Failsafe and safety behavior

This document defines what the boat does when something goes wrong. It is a **specification**, not a suggestion — firmware must match it, and any change must be reviewed.

## Threats we plan for

1. **Loss of radio link** (boat out of range, Pi crashed, TX module unplugged, antenna fault).
2. **Arming-switch off** (operator explicitly says "stop moving the motor").
3. **Power brownout** (battery sag under load).
4. **Boot / startup** (ESC must not spin motor before firmware is ready).
5. **Protocol version mismatch** (Pi and boat disagree on what channels mean).
6. **Sensor failure** (INA219 reports implausible values, IMU goes silent).
7. **Overcurrent** (propeller jammed, short circuit).

## Safe state

The **safe state** is:

| Actuator | Commanded value | Rationale |
|---|---|---|
| Throttle | 0.0 (neutral, motor stopped) | No propulsion |
| Rudder | +1.0 (full starboard) | Boat turns in slow circles; stays in roughly the same area rather than sailing off downwind |
| Sail | −1.0 (fully eased, let out) | De-powers the sail; boat luffs and decelerates |
| Status LED | rapid blink | Visible indicator from shore |

> **Physical verification required:** `failsafe_pos::SAIL = -1.0` assumes the sail winch servo at minimum position = sail fully eased. `failsafe_pos::RUDDER = +1.0` assumes +1.0 = starboard. Confirm both against your physical rig before water testing. Values are defined in `config.h` (failsafe_pos namespace).

A boat in safe state is a boat you can retrieve.

## State machine

```
         power on
             │
             ▼
      ┌───────────┐       protocol OK &
      │  BOOT     │────► link acquired ────► ┌──────────┐
      └───────────┘                          │ DISARMED │
                                             └─────┬────┘
                                                   │ arm channel = 1.0
                                                   ▼
                                             ┌──────────┐
                                             │  ARMED   │ ◄──── normal operation
                                             └─────┬────┘
                                                   │
              ┌────────────────────────────────────┼────────────────────────┐
              │ link lost > 500 ms                 │ arm channel = 0.0      │
              ▼                                    ▼                        │
      ┌──────────────┐                      ┌──────────┐                    │
      │  FAILSAFE    │                      │ DISARMED │◄───────────────────┘
      └──────┬───────┘                      └─────┬────┘
             │ link restored &                    │
             │ channels sane                      │ arm channel = 1.0
             ▼                                    ▼
         (back to DISARMED — must re-arm)     (back to ARMED)
```

Key rules:

- **BOOT → DISARMED** requires both: a valid CRSF frame received AND protocol version match AND I²C devices enumerated. Until then, PCA9685 outputs are held at safe values.
- **DISARMED → ARMED** requires the arm channel to be 1.0. Arming only while throttle is within ±0.05 of neutral. If the operator is holding throttle when they arm, we don't lurch.
- **ARMED → FAILSAFE** on any of: no fresh CRSF frame for 500 ms, CRSF reports `link_lost`, or the PCA9685 I²C bus errors repeatedly.
- **FAILSAFE → DISARMED** only after the link is healthy *and* the operator has returned the arm channel to 0.0 and back to 1.0. This is deliberate: returning to port should be a conscious act, not automatic.

## Control-mode switching (WiFi ↔ ELRS)

The boat's control *input source* (its own WiFi AP, or the ELRS receiver) is
a separate concern from the FAILSAFE state machine above, but interacts with
it. `CtrlMode` defaults to `WIFI` at boot and can change three ways:

1. **Touchscreen** — operator taps "Enable ELRS" / "Enable WiFi" on the boat.
2. **Remote request** (`CH_MODE`, channel 5, PROTOCOL_VERSION ≥ 3) — a live,
   CRC-valid CRSF stream asserting `CH_MODE > 0.5` for ≥300 ms is treated as
   an equally deliberate signal as a touchscreen tap, and switches the boat
   into `CtrlMode::ELRS` immediately — **no arbitration** with an active WiFi
   session. Releasing `CH_MODE` for ≥3 s switches back to `WIFI` — that exit
   debounce is deliberately much longer than the 300 ms entry debounce, since
   it triggers an AP teardown/restart and must not flap on a brief polling gap.
3. **Sustained ELRS link loss while in `CtrlMode::ELRS`** — after **12 s**
   with no valid CRSF frames (independent of, and much slower than, the
   500 ms FAILSAFE servo-neutral response below), the boat reverts to
   `CtrlMode::WIFI` automatically, so a bystander can always recover the boat
   from its own AP without needing to touch the screen.

Switching `CtrlMode` either direction tears down/starts the boat's own WiFi
AP and zeroes any in-flight WiFi-mode command (`wifi_ctrl.cpp`'s
`stop_ap()`/`start_ap()`), exactly as the manual touchscreen toggle already
did — the remote path reuses the same `wifi_ctrl_set_mode()` entry point, it
just adds an automatic trigger. The existing FAILSAFE state machine (BOOT →
DISARMED → ARMED → FAILSAFE) is unaffected and continues to apply safe servo
positions within 500 ms of any link loss while `CtrlMode::ELRS` is active,
regardless of how that mode was entered.

## Motor-specific safety

- ESC is held at neutral PWM (1500 µs) for **at least 2 seconds** at boot so the Quicrun 1060 can arm cleanly.
- Throttle deadband: inputs in `[-0.05, +0.05]` are treated as neutral. Prevents idle creep.
- Throttle slew limit: maximum rate of change is ±2.0 per second. A full-throttle-forward to full-reverse takes ≥2 s. Protects the drivetrain and avoids current spikes.
- Overcurrent: if INA219 reports battery current above a configured threshold (TBD — measure during first sea trial) for more than 1 second, force throttle to 0.0 and enter FAILSAFE. Operator must re-arm.

## Battery safety

- Log voltage and current continuously.
- Below 3.4 V/cell (10.2 V on 3S): surface a warning in telemetry.
- Below 3.2 V/cell (9.6 V on 3S): force throttle to 0.0. Rudder and sail still work so the boat can be sailed home.
- These are guidelines, not hard protections — the ultimate protection is the operator checking battery voltage before launch.

## Watchdog

ESP32-S3 hardware watchdog must be enabled with a 1-second timeout. Main loop kicks it every iteration. If the loop hangs, the chip reboots into BOOT state (which is the safe state).

## What FAILSAFE is NOT

- It is **not** "cut all outputs to zero PWM." Servos with no PWM can drift or buzz; ESCs may misread an absent signal. We explicitly *command* neutral, not silence.
- It is **not** "try to sail back autonomously." We have no autopilot. The boat becomes passive and waits for retrieval.
- It is **not** "reboot and hope." We enter a defined state and stay there.

## Testing the failsafe

The failsafe must be tested on the bench before every water test:

1. Power the boat up on the bench with all actuators attached.
2. Arm normally, confirm servos respond.
3. Power off the transmitter.
4. Within 500 ms: motor goes to neutral, sail lets out to minimum, rudder goes hard starboard.
5. Status LED enters rapid-blink pattern.
6. Power the transmitter back on; confirm boat stays in FAILSAFE until operator re-arms deliberately.

If any of these steps fail, do not put the boat in water.
