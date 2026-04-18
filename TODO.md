# TODO — Phased build plan

Work top-to-bottom. Each phase ends with a concrete bench test. Don't start phase N+1 until phase N's acceptance test passes.

Claude Code: when a phase is done, move it to "Completed" at the bottom and write a 2–3 line note on what actually got built vs. what was planned.

---

## Phase 0 — Project scaffolding

- [ ] Create `base-station/` Python project (venv, `requirements.txt`, `pyproject.toml`, `ruff` config)
- [ ] Create `boat-firmware/` PlatformIO project for ESP32-S3 (Waveshare board)
- [ ] Create `shared/protocol.py` with `PROTOCOL_VERSION`, channel constants
- [ ] Create `boat-firmware/src/protocol.h` with matching `PROTOCOL_VERSION` and channel constants
- [ ] Create `boat-firmware/src/config.h` with GPIO pin definitions (matching `docs/pinmap.md`)
- [ ] Set up git pre-commit: `ruff` for Python, `clang-format` for C++

**Acceptance**: `boat-firmware` builds an empty `setup()`/`loop()` and flashes to the ESP32-S3. The base station has a venv with FastAPI installed and can serve "hello world" from `/`.

---

## Phase 1 — Base station: web UI skeleton

- [ ] FastAPI app serving a static HTML page with three sliders (rudder, sail, throttle), an arm toggle, and a STOP button
- [ ] WebSocket endpoint receives control state and logs it
- [ ] In-memory "desired state" object updated from the WebSocket
- [ ] Basic input validation (range-clamp, NaN guard)
- [ ] Configure Pi as Wi-Fi AP with hostapd + dnsmasq (documented as a separate setup script)

**Acceptance**: connect a phone to the Pi's Wi-Fi, open the page in a browser, move the sliders, and see the values update in the Pi's server logs.

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

## Phase 6 — Telemetry

- [ ] Read INA219 voltage + current
- [ ] Read onboard IMU for roll (heel) angle
- [ ] Emit CRSF `BATTERY_SENSOR` at 2 Hz
- [ ] Emit CRSF `ATTITUDE` at 5 Hz
- [ ] Pi decodes telemetry frames and pushes them over WebSocket to the browser
- [ ] Browser displays: battery voltage, battery current, heel angle, link quality

**Acceptance**: the browser dashboard shows live values that respond to reality (tilt the boat → heel changes; run the motor → current rises).

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

(nothing yet)
