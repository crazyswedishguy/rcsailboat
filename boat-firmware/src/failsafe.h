// failsafe.h — link-loss failsafe state machine.
//
// Monitors the ELRS link and drives the boat to a safe state when the RC
// link is lost. The full specification is in docs/failsafe.md. Summary:
//
//   • 500 ms without a valid CRSF frame → link considered lost
//   • On link loss: rudder → centre, sail → centre, throttle → 0 immediately
//   • Throttle is not re-enabled until the link is restored AND the arm
//     channel rises above 0.5 again (prevents surprise restart after dropout)
//   • Throttle slew-rate limiter prevents instant full-power commands
//   • Hardware watchdog reset if loop() stalls
//
// Phase 5 stub: both functions are no-ops until the CRSF RX path in elrs.cpp
// is implemented (Phase 3). Do NOT test on water until Phase 5 is complete
// and the acceptance test in docs/failsafe.md passes.

#pragma once

// Initialise failsafe state. Call once from setup() after elrs_init().
void failsafe_init();

// Update failsafe state machine. Call every loop() iteration.
// Reads elrs_link_ok() and applies safe outputs if link is lost.
void failsafe_update();
