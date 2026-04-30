#pragma once

// Bilge water detection and pump control.
//
// Sensor: float switch (or bare-wire probe) wired between BILGE_SENSOR (GPIO2)
//         and GND with an internal pull-up.  Dry = HIGH, wet = LOW.
// Pump:   N-MOSFET gate on BILGE_PUMP (GPIO3); set HIGH to run the pump.
//
// Water presence is debounced to 2 s to ignore momentary spray.
// The pump can be manually controlled via bilge_pump_set() or will activate
// automatically if AUTO_PUMP is later added.

void bilge_init();
void bilge_update();           // call every loop() — debounces sensor
bool bilge_water_detected();   // true if sensor wet for > 2 s
bool bilge_pump_active();
void bilge_pump_set(bool on);
