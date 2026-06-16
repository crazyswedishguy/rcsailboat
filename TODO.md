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

Software complete. Hardware wiring still pending.

- [ ] Wire ELRS receiver to UART1 (GPIO16 RX, GPIO17 TX)
- [ ] Power on, confirm `elrs: UART1 @ 420000 baud` log line at boot
- [ ] Confirm channel values print to serial console at 10 Hz when transmitter paired
- [ ] Move sticks → confirm servos respond; disarm → confirm motor stops

**Acceptance**: pairing a transmitter (sticks, not the Pi yet), moving sticks drives servos; disarming stops motor.

---

## Phase 4 — Base station: ELRS transmit path + XIAO dual-mode

Software complete (all phases of the Mode 2/3 plan implemented in code).
Hardware wiring + flash still pending.

Sub-tasks:

**4a — Mode 3 (Pi → XIAO bridge → boat):**
- [ ] Build/flash `crsf-bridge/` to the XIAO ESP32-S3, loopback-test UART1 TX↔RX
- [ ] Wire XIAO UART1 → Ranger Micro JR-bay CRSF pin (series resistor on TX), power Ranger Micro from JR-bay 5V
- [ ] Connect XIAO to Pi via USB-C, confirm `ELRS_PORT` (default `/dev/ttyACM0`, or `/dev/elrs-tx` after `setup-udev.sh`)
- [ ] Start base station and confirm 50 Hz RC frames flow through — Ranger Micro LED leaves "slow orange"
- [ ] Move a browser slider → confirm the corresponding servo moves
- [ ] Pump button in Pi UI toggles bilge pump; capsized/bilge-wet indicators track telemetry

**4b — ELRS link config (manual, one-time):**
- [ ] Set Ranger Micro TX to 250 Hz packet rate / 1:4 telemetry ratio (see `docs/elrs-link.md`)
- [ ] Rebind RP3; confirm ≥ 24 attitude updates/s in Pi log

**4c — Mode 2 (XIAO standalone → boat):**
- [ ] Boot XIAO without Pi connected; confirm SSID `Mistral-2` visible
- [ ] Phone joins `Mistral-2`/`readyabout`; control page loads at `http://192.168.5.1/`
- [ ] Sliders move boat servos; pump button works; telemetry (attitude, battery, GPS) renders

**4d — Dynamic Mode 2 ↔ Mode 3 hot-plug:**
- [ ] Start Pi base station while XIAO in Mode 2 → XIAO switches to bridge within ~500 ms
- [ ] Stop Pi app → XIAO reverts to AP within 2 s
- [ ] Both directions transition without leaving stale throttle commands

**Acceptance**: all four sub-tasks pass. Browser slider → servo moves ≤ 100 ms in both modes.

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

- **Map waypoints** — tap or long-press on the Leaflet map to drop a waypoint pin; display as list; send to boat over CRSF/WebSocket
- **Motor autopilot to waypoint** — hold a compass/GPS bearing toward the next waypoint; PI loop drives rudder; requires HMC5883L (already on I²C) or GPS COG at speed ≥ 1 kn
- **Motor autopilot return-to-home** — same heading controller, target is `homePos`; triggered by RTH button or low-battery cutoff
- **Sail auto-trim** — given true wind direction (anemometer), compute optimal sail angle; suggest or apply automatically
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

### Protocol v2 + XIAO dual-mode firmware + Pi parity (Phases 1–6 of Mode-2 plan)

**Protocol v2:**
- CH_PUMP (ch 7, index 6) added for bilge pump control over RF.
- SAILBOAT frame (0x80) extended to 9-byte payload with a status bitfield (capsized/bilge-wet/pump-active/armed/failsafe).
- ATTITUDE decoupled from SAILBOAT timer: now runs at 24 Hz (42 ms) independently.
- `PROTOCOL_VERSION` bumped to 2 in `protocol.h` and `shared/protocol.py`.
- `docs/protocol.md` and `CLAUDE.md` updated; channel quick-reference added to CLAUDE.md.

**Shared web UI (Phase 3):**
- `HTML_PAGE[]` and `MAP_HTML[]` PROGMEM extracted from `wifi_ctrl.cpp` into `shared/web/control_page.h` + `shared/web/map_page.h`.
- Both `boat-firmware` and `crsf-bridge` include via `-I"${PROJECT_DIR}/../shared"` in `platformio.ini`.
- Boat firmware byte-identical post-refactor (confirmed by clean re-build).

**XIAO dual-mode firmware (Phases 4–5):**
- `crsf-bridge/src/main.cpp` fully rewritten: hand-ported CRSF codec (RC frame builder at 50 Hz, telemetry decoders), WiFi AP (`Mistral-2`/`readyabout`/`192.168.5.1`), HTTP server serving shared control page.
- Pi emits CRSF 0x7E heartbeat every 500 ms. XIAO detects it → Mode 3 (bridge); 2 s timeout → Mode 2 (AP). State reset to neutral/disarmed on every transition.
- Build clean: RAM 14.4%, Flash 31.0%.

**Pi parity (Phase 6):**
- `elrs_bridge.py`: CH_PUMP added to RC frame, 9-byte SAILBOAT decode, heartbeat emitter in `_tx_loop()`.
- `state.py`: `pump` field in `DesiredState`; `capsized/bilge_wet/pump_active/boat_armed/boat_failsafe` in `TelemetryState`.
- `main.py`: pump WebSocket handler; `status` sub-dict in telemetry payload.
- `index.html`, `ctrl.jsx`, `landscape.jsx`: pump toggle button, capsized indicator, bilge-wet dot.
- `docs/elrs-link.md`: Mode 2/3 switching section, heartbeat frame format, ELRS link config (250 Hz/1:4).

**Hardware test pending** — see Phase 4 above.
