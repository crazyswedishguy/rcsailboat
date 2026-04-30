// failsafe.cpp — link-loss failsafe implementation (Phase 5 stub).
//
// Both functions are currently no-ops. The full implementation belongs in
// Phase 5, after the CRSF receive path (elrs.cpp Phase 3) is working.
//
// When implementing, this module should:
//   1. Track the timestamp of the last valid CRSF frame (from elrs_update()).
//   2. If (millis() - last_frame_ms) > 500: call servos_set() to neutral for
//      rudder/sail, and servos_set(pwm_ch::MOTOR_ESC, 0.0f) for throttle.
//   3. Set an internal "disarmed by failsafe" flag.
//   4. Clear the flag only when elrs_link_ok() returns true AND the arm
//      channel (CH_ARM) rises above 0.5.
//   5. Apply a slew-rate limiter to throttle commands to prevent sudden
//      acceleration on link restore.
//   6. Enable the ESP32 hardware watchdog (esp_task_wdt_init + esp_task_wdt_add).
//
// See docs/failsafe.md for the complete state machine and test procedure.

#include "failsafe.h"

void failsafe_init()   {}   // Phase 5: init watchdog, reset state
void failsafe_update() {}   // Phase 5: check link age, apply safe outputs
