// failsafe.h — link-loss failsafe state machine.
//
// Monitors the ELRS link and transitions to a safe state when the RC link
// is lost (elrs_link_ok() returns false for any reason). Safe outputs are
// defined in config.h (failsafe_pos namespace):
//   - throttle → 0.0   motor off immediately
//   - sail     → −1.0  fully let out — sail luffs, boat decelerates
//   - rudder   → +1.0  full starboard — boat turns in slow circles
//
// failsafe_update() is called only from the ELRS branch in main.cpp.
// The equivalent WiFi-mode timeout is handled by wifi_ctrl_timed_out() in
// main.cpp, which applies the same failsafe_pos values.
//
// IMPORTANT: main.cpp must check failsafe_active() every loop and apply
// failsafe_pos values when true. The flag is set/cleared here; positions
// are commanded there.

#pragma once

// Initialise failsafe state. Call once from setup() after elrs_init().
void failsafe_init();

// Update failsafe state machine. Call every loop() iteration from the
// ELRS control branch. Reads elrs_link_ok() and logs transitions.
void failsafe_update();

// Returns true while the ELRS link is lost and failsafe is active.
// main.cpp uses this to select between normal channel output and safe positions.
bool failsafe_active();
