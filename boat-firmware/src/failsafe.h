// failsafe.h — BOOT / DISARMED / ARMED / FAILSAFE state machine.
//
// See docs/failsafe.md for the full specification and test procedure.
// Safe servo positions are defined in config.h (failsafe_pos namespace).

#pragma once

// Initialise failsafe state. Call once from setup() after elrs_init().
void failsafe_init();

// Update the state machine. Call every loop() iteration from the ELRS branch.
// Reads elrs_link_ok() and elrs_get_channel() internally.
void failsafe_update();

// True in BOOT, DISARMED, and FAILSAFE states — main.cpp applies safe positions.
bool failsafe_active();

// True only in ARMED state. Convenience inverse of failsafe_active().
bool failsafe_armed();
