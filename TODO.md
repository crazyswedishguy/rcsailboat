# TODO — Phased build plan

Work top-to-bottom. Each phase ends with a concrete bench test. Don't start phase N+1 until phase N's acceptance test passes.

Claude Code: when a phase is done, move it to "Completed" at the bottom and write a 2–3 line note on what actually got built vs. what was planned.

---

## Phase 2 — Boat firmware: I²C + PCA9685 + servos (no radio yet)

- [ ] Bring up I²C and scan the bus on boot (log found addresses)
- [ ] Initialize PCA9685 at 50 Hz
- [ ] Drive the rudder servo through its full range with a hard-coded sweep
- [ ] Drive the sail winch servo through its full range
- [ ] Drive the ESC through a safe arming sequence (hold neutral 2 s, then a gentle forward pulse)
- [ ] Status LED heartbeat

**Acceptance**: with the boat on the bench (propeller off — safety), all three actuators sweep on power-up.

---

## Phase 3 — Boat firmware: ELRS receive path

- [ ] Wire the RP3 receiver to the ESP32-S3 UART
- [ ] Integrate a CRSF parser library
- [ ] Print channel values at 10 Hz to serial console
- [ ] Map channels 1–4 to the actuators per `docs/protocol.md` (replacing the hard-coded sweep)
- [ ] Implement arm-channel gating: motor does nothing unless channel 4 ≥ 0.5

**Acceptance**: pairing a transmitter directly (sticks, not the Pi yet), moving the sticks drives the servos and ESC; disarming stops the motor immediately.

---

## Phase 4 — Base station: ELRS transmit path

- [ ] Detect the ELRS TX module's USB-serial device
- [ ] Async task packs the desired state into a CRSF channel frame at 50 Hz and writes it to the TX
- [ ] Connect the pieces: browser slider → Pi desired state → CRSF out → boat servo moves

**Acceptance**: moving a slider on a phone browser moves the corresponding servo on the bench within ~100 ms.

---

## Phase 5 — Boat firmware: failsafe

- [ ] Implement the state machine from `docs/failsafe.md`
- [ ] 500 ms link-loss detection
- [ ] Safe-state output on link loss
- [ ] Throttle slew-rate limiter
- [ ] Hardware watchdog enabled
- [ ] Bench test the failsafe per `docs/failsafe.md` § "Testing the failsafe"

**Acceptance**: the full failsafe test procedure passes. No water until this works.

---

## Phase 6 — Telemetry (remaining)

### 6c — Heading (QMC5883L magnetometer) [requires additional hardware]
- [ ] Wire QMC5883L to external I²C bus (IO6/IO7, addr 0x0D) — update `docs/pinmap.md`
- [ ] Add `compass.h/cpp` under `-DCOMPASS_ENABLED` build flag (mirrors GPS pattern)
- [ ] Include live heading in ATTITUDE frame yaw field once compass is wired

### 6e — Link quality
- [ ] Parse `LINK_STATISTICS` (0x14) frames received from ELRS in `elrs.cpp`
- [ ] Expose `elrs_rssi()` and `elrs_link_quality()` accessors

### 6g — Base station: decode and display
- [ ] Pi decodes incoming CRSF telemetry frames from ELRS serial port
- [ ] Push decoded values over WebSocket to browser
- [ ] Browser dashboard: voltage, current, mAh, heel, pitch, heading, GPS position on map,
      GPS speed, RSSI/link quality, servo commanded positions, ESP32 temperature

**Acceptance**: browser dashboard shows live values that respond to reality (tilt boat → heel changes; run motor → current rises; walk away → RSSI drops; move a slider → commanded position updates).

---

## Phase 7 — First water test

- [ ] Waterproofing audit of every component and cable entry
- [ ] LiPo voltage check + balance before launch
- [ ] Range check on dry land first (walk 50 m away, confirm control)
- [ ] Short tethered float test in a bucket / bath
- [ ] Actual pond test in calm conditions, with a retrieval plan

**Acceptance**: the boat sails, returns to shore, and the battery and electronics are dry afterward.

---

## Ideas / someday / maybe (not in the current plan)

- GPS module + position display on a map in the browser UI
- Wind sensor (direction + speed) — would need a custom CRSF telemetry frame
- Water temperature sensor
- Auto-trim: given wind direction, suggest sail position
- Data logging to SD or to the Pi for post-sail analysis
- Migrate browser UI from vanilla JS to something richer if/when it earns the complexity

---

## Completed

### Phase 0 — Project scaffolding
Scaffold created as planned: PlatformIO project for ESP32-S3, FastAPI base-station project with venv,
`shared/protocol.py` and `boat-firmware/src/protocol.h` with matching protocol constants and channel
definitions, `config.h` with GPIO pin map, ruff config. `git pre-commit` hooks not added (skipped by choice).

### Phase 1 — Base station: web UI skeleton
FastAPI app serving static HTML with rudder/sail/throttle sliders, arm toggle, and STOP button.
WebSocket endpoint updates an in-memory `DesiredState` object and logs changes. Range clamping and
NaN guard in place. GPS position broadcast infrastructure (`GpsPosition`, `broadcast()`) added
alongside the base Phase 1 items. Wi-Fi AP setup deferred to a separate setup script (not in repo).

### Phase 6a — Power monitoring (INA219)
`power.h/cpp` implemented: reads voltage + current from INA219 over external I²C (IO6/IO7, addr 0x41),
tracks mAh consumed since boot. Emits CRSF `BATTERY_SENSOR` (0x08) at 2 Hz via `telemetry.cpp`.

### Phase 6b — IMU / attitude (QMI8658)
`imu.h/cpp` implemented: reads QMI8658 accelerometer on onboard I²C (IO47/IO48, addr 0x6B) at 62.5 Hz,
computes heel (roll) and pitch from gravity projection using atan2. Emits CRSF `ATTITUDE` (0x1E) at
5 Hz via `telemetry.cpp`; yaw is hardcoded 0 pending compass hardware (Phase 6c).

### Phase 6d — GPS telemetry
`gps.h/cpp` stub and `telemetry_send_gps()` implemented in `telemetry.cpp`. Emits CRSF GPS frame
(0x02) at 1 Hz when `GPS_ENABLED` build flag is set and a fix is available.

### Phase 6f — Commanded servo positions + ESP32 temperature
`servos_get_commanded()` accessor implemented. Custom CRSF sailboat frame (0x80) emitted at 5 Hz
from `telemetry.cpp`: rudder, sail, ESC commanded values (×10000) plus ESP32 internal temperature.
CRSF frame builder (`crsf_build`, CRC-8/DVB-S2) implemented and shared by all telemetry emitters.
