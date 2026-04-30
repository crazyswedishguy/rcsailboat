// servos.h — PCA9685 PWM servo/ESC driver interface.
//
// All PWM output goes through a PCA9685 16-channel I²C PWM controller
// (I²C address 0x40) at 50 Hz. The ESP32 generates no PWM directly.
// This keeps all servo wiring at the PCA9685 breakout and avoids GPIO conflicts.
//
// Channel assignments (defined in config.h):
//   pwm_ch::RUDDER     (0) — rudder servo,    –1.0 (port) .. +1.0 (starboard)
//   pwm_ch::SAIL_WINCH (1) — sail winch servo, 0.0 (fully out) .. +1.0 (fully in)
//   pwm_ch::MOTOR_ESC  (2) — motor ESC,       –1.0 (reverse) .. +1.0 (forward)
//
// Pulse range: 1000–2000 µs (calibrate per-servo before relying on limits).
// servos_init() runs the ESC arm sequence — holds neutral for 2 s — before returning.
// If the PCA9685 is not found on the I²C bus, output is silently disabled (safe no-op).

#pragma once

// Initialise the PCA9685 and run the ESC arm sequence. Call once from setup().
// Blocks for ~2 s while the ESC arms at neutral pulse width.
void  servos_init();

// Set the output for pca_channel to value (–1.0..+1.0).
// Values outside the range are clamped. The last commanded value is cached
// for telemetry readback via servos_get_commanded().
void  servos_set(int pca_channel, float value);

// Return the last value passed to servos_set() for this channel.
// Used by telemetry.cpp to include commanded positions in the sailboat frame.
float servos_get_commanded(int pca_channel);
