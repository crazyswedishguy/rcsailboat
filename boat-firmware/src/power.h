// power.h — INA228 total battery current and voltage monitor interface.
//
// Reads bus voltage (V) and current (A) from an INA228 over the shared I²C
// bus (GPIO47/48, address 0x41 — A0=VS, A1=GND). The INA228 sense leads
// connect across a 50A/1.5mΩ bus bar shunt wired in the main battery positive
// line before the ESC/UBEC split, so it measures total system current
// (motor + servos + logic) and true battery voltage.
//
// If the INA228 is not detected at init time, all functions return 0.0 safely.
// This allows the firmware to run and send telemetry (with zero readings) while
// the sensor is not yet wired up.
//
// Update the readings by calling power_update() every loop iteration.

#pragma once

// Initialise the INA228. Call once from setup(). Safe no-op if sensor absent.
void  power_init();

// Read the latest voltage and current, and accumulate mAh. Call every loop().
void  power_update();

// Battery voltage in volts (measured at the IN+ pin = battery positive).
float power_voltage_v();

// Instantaneous total current draw in amperes (positive = discharge).
float power_current_a();

// Total milliamp-hours consumed since boot. Resets to 0 on each power-cycle.
float power_mah_used();
