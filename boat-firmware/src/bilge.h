#pragma once

// Bilge water detection and pump control.
//
// Sensor: DYIables bare resistive water sensor.
//         Wire VCC → 3.3V, GND → GND, SIG → BILGE_SENSOR (GPIO2, ADC1_CH1).
//         No pull-up. Dry ≈ 0 ADC counts; wet rises toward 4095.
// Pump:   N-MOSFET gate on BILGE_PUMP (GPIO3); set HIGH to run the pump.
//
// Water presence is debounced to 2 s to ignore momentary spray.
// BILGE_WET_THRESHOLD (default 500/4095) is the detection threshold — adjust
// in bilge.cpp if the sensor produces false triggers or is slow to respond.

void bilge_init();
void bilge_update();           // call every loop() — debounces sensor
bool bilge_water_detected();    // true if sensor wet for > 2 s
bool bilge_sensor_verified();   // true once sensor has triggered at least once this session
bool bilge_pump_active();
void bilge_pump_set(bool on);
