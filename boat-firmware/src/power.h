// power.h — INA219 battery current and voltage monitor interface.
//
// Reads bus voltage (V) and shunt current (A) from an INA219 over the shared
// I²C bus (GPIO47/48, address 0x41 — address strapped to avoid collision with
// the PCA9685 at 0x40). Accumulates milliamp-hours consumed since boot.
//
// The INA219 shunt resistor goes in series with the main battery positive lead
// so it measures total system current (servos, ESC, MCU, everything).
//
// If the INA219 is not detected at init time, all functions return 0.0 safely.
// This allows the firmware to run and send telemetry (with zero readings) while
// the sensor is not yet wired up.
//
// Update the readings by calling power_update() every loop iteration.
// The INA219 hardware samples at its own rate (~500 Hz); we read the latest
// result each call, so calling more often than ~2 kHz is wasteful but harmless.

#pragma once

// Initialise the INA219. Call once from setup(). Safe no-op if sensor absent.
void  power_init();

// Read the latest voltage, current, and accumulate mAh. Call every loop().
void  power_update();

// Battery bus voltage in volts (measured on the load side of the shunt).
float power_voltage_v();

// Instantaneous current draw in amperes (positive = discharge).
float power_current_a();

// Total milliamp-hours consumed since boot. Resets to 0 on each power-cycle.
float power_mah_used();
