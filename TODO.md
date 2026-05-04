# TODO — Phased build plan

Work top-to-bottom. Each phase ends with a concrete bench test. Don't start phase N+1 until phase N's acceptance test passes.

Claude Code: when a phase is done, move it to "Completed" at the bottom and write a 2–3 line note on what actually got built vs. what was planned.

---

## Phase 2 — Boat firmware: I²C + PCA9685 + servos (no radio yet)

Software is written (`servos.cpp` + `power.cpp`). Pending bench test — nothing is physically wired yet.

- [ ] Wire PCA9685 to I²C bus (GPIO47/48); wire INA219 at 0x41
- [ ] Power on, verify I²C scan prints `0x40` (PCA9685) and `0x41` (INA219) at boot
- [ ] Confirm rudder, sail winch, and ESC all receive PWM on channels 0/1/2
- [ ] Verify ESC arms at neutral (1500 µs for 2 s) without spinning

**Acceptance**: with the boat on the bench (propeller off — safety), all three actuators sweep on power-up.

---

## Phase 3 — Boat firmware: ELRS receive path

`elrs.cpp` is a stub — CRSF parsing not yet implemented.

- [ ] Wire ELRS receiver to UART1 (GPIO16 RX, GPIO17 TX)
- [ ] Implement `elrs_init()`: `Serial1.begin(420000, SERIAL_8N1, CRSF_RX, CRSF_TX)`
- [ ] Implement `elrs_update()`: parse incoming CRSF frames, extract RC channels
- [ ] Parse `LINK_STATISTICS` (0x14) to update RSSI and link quality
- [ ] Confirm channel values print to serial console at 10 Hz
- [ ] Map channels per `docs/protocol.md` — arm channel must be high before throttle works

**Acceptance**: pairing a transmitter (sticks, not the Pi yet), moving sticks drives servos; disarming stops motor.

---

## Phase 4 — Base station: ELRS transmit path

Software is complete (`base-station/app/elrs_bridge.py`). Pending hardware connection.

- [ ] Connect ELRS TX module to Pi via USB-Serial
- [ ] Confirm `ELRS_PORT` env var points to the right device (check `dmesg` or `ls /dev/ttyUSB*`)
- [ ] Start base station and confirm 50 Hz RC frames flow to TX module (check ELRS Lua script on transmitter)
- [ ] Move a browser slider → confirm the corresponding servo moves

**Acceptance**: browser slider → servo moves within ~100 ms.

---

## Phase 5 — Boat firmware: failsafe

`failsafe.cpp` is a stub. Must be implemented before any water testing. See `docs/failsafe.md`.

- [ ] Implement link-loss detection: 500 ms without a valid CRSF frame → link lost
- [ ] On link loss: rudder → center, sail → center, throttle → 0 immediately
- [ ] ESC re-arms only after link is re-established AND arm channel goes high again
- [ ] Throttle slew-rate limiter (prevent instant full-throttle commands)
- [ ] Enable hardware watchdog
- [ ] Run the full failsafe test procedure from `docs/failsafe.md`

**Acceptance**: full failsafe test procedure passes. **No water testing until this is done.**

---

## Phase 6e — Link quality telemetry

- [ ] Parse `LINK_STATISTICS` (0x14) frames in `elrs_update()` (can be done alongside Phase 3)
- [ ] Expose live RSSI and link quality via `elrs_rssi()` / `elrs_link_quality()`
- [ ] Verify values appear in the SD card log and in the base-station WebSocket feed

---

## Phase 7 — First water test

Only begin after Phase 5 failsafe acceptance test passes.

- [ ] Waterproofing audit of every component and cable entry point
- [ ] LiPo voltage check + cell balance before launch
- [ ] Range check on dry land (walk 50 m away; confirm control + telemetry)
- [ ] Short tethered float test in a bucket / bath
- [ ] Actual pond test in calm conditions, with a retrieval plan ready

**Acceptance**: the boat sails, returns, and the battery and electronics are dry afterward.

---

## Ideas / someday / maybe

- Wind sensor (direction + speed) — would need a custom CRSF telemetry frame
- Water temperature sensor
- Auto-trim: given wind direction, suggest optimal sail position
- Migrate browser UI from vanilla JS to something richer once the rest of the system is stable
- **MAVLink**: replace the custom CRSF telemetry framing with MAVLink 2 once the system is stable.
  MAVLink has standard message definitions for GPS, attitude, battery, RC channels, and system status,
  plus ground-control-station (GCS) compatibility (Mission Planner, QGroundControl). The main trade-off
  is heavier per-message overhead vs. the compact hand-rolled CRSF frames used today — acceptable once
  telemetry bandwidth is no longer the primary constraint.

---

## Completed

### Phase 0 — Project scaffolding
Scaffold created: PlatformIO project for ESP32-S3, FastAPI base-station with venv,
`shared/protocol.py` and `boat-firmware/src/protocol.h` with matching protocol constants,
`config.h` with GPIO pin map, ruff config.

### Phase 1 — Base station: web UI skeleton
FastAPI app with rudder/sail/throttle sliders, arm toggle, STOP button.
WebSocket endpoint updating `DesiredState`. Range clamping and NaN guard in place.

### Phase 2 (software) — PCA9685 servo driver
`servos.h/cpp` written: PCA9685 initialised at 50 Hz, ESC arm sequence (2 s neutral),
`servos_set()` maps –1.0..+1.0 to 1000–2000 µs. I²C scan at boot logs all found addresses.
Hardware bench test still pending (nothing wired yet).

### Phase 6a — Power monitoring (INA219)
`power.h/cpp` written: reads voltage + current from INA219 (I²C 0x41), accumulates mAh.
Emits CRSF `BATTERY_SENSOR` (0x08) at 2 Hz via `telemetry.cpp`. Safe no-op if sensor absent.

### Phase 6b — IMU / attitude (QMI8658)
`imu.h/cpp` written: reads QMI8658 accelerometer (I²C 0x6B at 62.5 Hz), computes roll and
pitch via atan2. Capsize flag triggers at roll > 110° sustained > 2 s. Emits CRSF `ATTITUDE`
(0x1E) at 5 Hz; yaw is live heading when COMPASS_ENABLED, else 0.

### Phase 6c — Compass (HMC5883L)
`compass.h/cpp` written using Adafruit HMC5883 Unified library. Heading 0–360° via atan2,
no declination correction. Compiled in `esp32-s3-compass` / `esp32-s3-full` environments.
Hardware (BN-880 I²C wiring) still pending.

### Phase 6d — GPS telemetry (TinyGPS++)
`gps.h/cpp` written: HardwareSerial on UART2 at 9600 baud, TinyGPS++ parses NMEA.
`gps_has_fix()` requires valid location data < 2 s old. Emits CRSF GPS frame (0x02) at 1 Hz
when `GPS_ENABLED` and fix is available. BN-880 wiring still pending.

### Phase 6f — Commanded servo positions + ESP32 temperature
`servos_get_commanded()` added. Custom CRSF sailboat frame (0x80) emits at 5 Hz:
rudder/sail/ESC (×10000) + ESP32 internal temperature from `temperatureRead()`.

### Phase 6g — Base station: CRSF bridge + telemetry UI
`elrs_bridge.py` fully implemented: async serial RX/TX, CRSF frame builder for RC channels
(50 Hz), decoders for battery/attitude/GPS/sailboat frames, auto-reconnect. `main.py` wires
it into FastAPI with WebSocket broadcast. `index.html` updated with live telemetry strip
(battery, GPS, attitude, boat values) using CSS status colouring.

### Phase 6h — SD card data logger
`sdlog.h/cpp` written: opens a new `LOG####.CSV` on every boot, logs all telemetry at 1 Hz.
Columns: timestamp, GPS, voltage/current/mAh, roll/pitch, RSSI/LQ, servo positions, temp, bilge.
Safe no-op if no card present (uses HSPI / SPI3 independent of display).

### Phase 6i — Bilge monitoring
`bilge.h/cpp` written: float switch on GPIO2 (internal pull-up, active LOW), debounced 2 s.
N-MOSFET gate on GPIO3 drives bilge pump. `bilge_water_detected()` and `bilge_pump_set()`
exposed. WiFi UI shows bilge status and has a manual pump toggle button.

### Phase 6j — WiFi Direct + on-board web server
`wifi_ctrl.h/cpp` written: ESP32 broadcasts its own AP (`darkandstormy`/`readyabout`).
Embedded HTML control page (sliders, arm, pump) served from PROGMEM. GPS track map on `/map`.
Tile server on `/tiles/{z}/{x}/{y}.png` from SD card. Mode switch (WiFi ↔ ELRS) via display touch.

### Phase 6k — AMOLED display (CO5300 + LVGL)
`display.h/cpp` written: 6-screen swipeable tileview (LVGL 8.3) on CO5300 AMOLED 280×456
via QSPI. Screens: status, attitude, GPS, bilge/power, SD log, compass. FT3168 touch drives
LVGL pointer events. Swipe gesture fix applied: gesture events registered on edge tiles, not tileview.
