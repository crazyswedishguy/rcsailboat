// telemetry.h — CRSF telemetry frame emitter.
//
// Builds and transmits CRSF telemetry frames over the ELRS link on a fixed schedule.
// Frames are written via elrs_send_frame(), which in turn writes to Serial1 (the ELRS
// TX module). The ELRS module relays them over RF to the base station.
//
// Emission schedule:
//   BATTERY_SENSOR (0x08) — 2 Hz   — voltage, current, mAh used, remaining %
//   ATTITUDE       (0x1E) — 5 Hz   — pitch, roll, yaw (in radians × 10 000)
//   SAILBOAT       (0x80) — 5 Hz   — commanded rudder/sail/ESC + ESP32 temp (custom frame)
//   GPS            (0x02) — 1 Hz   — lat/lng/speed/heading/alt/sats (GPS_ENABLED only)
//   DEVICE_STATUS  (0x81) — 0.2 Hz — device health bitmap (10 devices × 2-bit level)
//
// All multi-byte fields are big-endian, matching the CRSF specification.
// See docs/protocol.md for the full frame layout of each type.
//
// telemetry_update() is designed to be called every loop() iteration — it tracks
// its own timers internally and skips emission when not due.

#pragma once

// Initialise telemetry state. Call once from setup(). Currently a no-op.
void telemetry_init();

// Check timers and emit any frames that are due. Call every loop() iteration.
void telemetry_update();

#ifdef GPS_ENABLED
// Build and emit a CRSF GPS frame (0x02) from the current GPS fix.
// Called internally by telemetry_update() at 1 Hz; exposed here for testing.
void telemetry_send_gps();
#endif
