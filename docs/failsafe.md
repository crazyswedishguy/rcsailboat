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
| Rudder | 0.0 (centered) | Boat drifts straight; avoids violent turns |
| Sail | 0.0 (sheeted fully out) | De-powers the sail; boat luffs and slows down |
| Status LED | rapid blink | Visible indicator from shore |

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
4. Within 500 ms: rudder centers, sail sheets out, motor goes to neutral.
5. Status LED enters rapid-blink pattern.
6. Power the transmitter back on; confirm boat stays in FAILSAFE until operator re-arms deliberately.

If any of these steps fail, do not put the boat in water.
