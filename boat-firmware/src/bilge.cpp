// bilge.cpp — bilge water detection and pump relay driver.
//
// Reads a float switch (or bare-wire conductivity probe) on GPIO2 with an internal
// pull-up. Dry = GPIO HIGH, wet = GPIO LOW (switch closes to GND when float rises).
// Water presence is debounced over WET_DEBOUNCE_MS (2 s) to ignore spray and waves.
//
// The bilge pump is driven by an N-MOSFET gate on GPIO3. Setting the pin HIGH
// turns the MOSFET on, completing the pump motor circuit from the main battery.
// The pump can be controlled manually via bilge_pump_set() (HTTP /pump endpoint)
// or automatically by future logic that responds to bilge_water_detected().
//
// bilge_update() should run every loop() iteration — it is a fast GPIO read and
// a register write, with no blocking or delay.

#include "bilge.h"
#include "config.h"
#include <Arduino.h>

#define WET_DEBOUNCE_MS 2000   // sensor must read wet for this long before we believe it

static bool          s_water     = false;
static bool          s_verified  = false;   // true once sensor has ever triggered (confirms connection)
static bool          s_pump      = false;
static unsigned long s_wet_start = 0;

void bilge_init()
{
    pinMode(pins::BILGE_SENSOR, INPUT_PULLUP);
    pinMode(pins::BILGE_PUMP,   OUTPUT);
    digitalWrite(pins::BILGE_PUMP, LOW);
}

void bilge_update()
{
    bool raw_wet = (digitalRead(pins::BILGE_SENSOR) == LOW);

    if (!raw_wet) {
        s_wet_start = 0;
        s_water = false;
    } else {
        if (!s_wet_start) s_wet_start = millis();
        if ((millis() - s_wet_start) >= WET_DEBOUNCE_MS) {
            s_water    = true;
            s_verified = true;
            Serial.println("bilge: water detected");
        }
    }

    // Drive the pump pin (safe to call every loop — just a register write)
    digitalWrite(pins::BILGE_PUMP, s_pump ? HIGH : LOW);
}

bool bilge_water_detected()  { return s_water; }
bool bilge_sensor_verified() { return s_verified; }
bool bilge_pump_active()     { return s_pump; }

void bilge_pump_set(bool on)
{
    s_pump = on;
    digitalWrite(pins::BILGE_PUMP, on ? HIGH : LOW);
    Serial.printf("bilge: pump %s\n", on ? "ON" : "OFF");
}
