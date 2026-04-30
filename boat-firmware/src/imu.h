// imu.h — QMI8658 6-axis IMU interface (roll, pitch, capsize detection).
//
// The QMI8658 is the onboard IMU on the Waveshare AMOLED board (I²C 0x6B,
// shared bus GPIO47/48). Only the accelerometer is used — the gyroscope is
// not read. Attitude is computed purely from gravity projection via atan2,
// which is sufficient for the slow angular rates of a sailing boat but would
// drift on faster platforms.
//
// Outputs:
//   Roll  — heel angle in degrees. Positive = starboard heel (leaning right).
//   Pitch — pitch angle in degrees. Positive = bow up.
//
// Capsize detection:
//   If |roll| > 110° for more than 2 s continuously, imu_is_capsized() returns
//   true. The threshold uses a 2 s debounce to ignore momentary wave action.
//   The flag self-clears when the boat rights itself.
//
// If the QMI8658 WHOAMI register does not return 0x05, the module silently
// disables itself (s_ok = false) and all functions return 0 / false.

#pragma once

// Initialise the QMI8658 (detect, configure accel at ±8g / 62.5 Hz). Call from setup().
void  imu_init();

// Read the latest accelerometer sample and update roll, pitch, capsize state.
// Call every loop() iteration — the QMI8658 samples at 62.5 Hz internally.
void  imu_update();

// Heel angle in degrees. Positive = starboard heel.
float imu_roll_deg();

// Pitch angle in degrees. Positive = bow up.
float imu_pitch_deg();

// True if |roll| has exceeded 110° for more than 2 s.
bool  imu_is_capsized();
