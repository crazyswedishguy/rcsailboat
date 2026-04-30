#include "bilge.h"
#include "config.h"
#include <Arduino.h>

#define WET_DEBOUNCE_MS 2000   // sensor must read wet for this long before we believe it

static bool          s_water    = false;
static bool          s_pump     = false;
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
            s_water = true;
            Serial.println("bilge: water detected");
        }
    }

    // Drive the pump pin (safe to call every loop — just a register write)
    digitalWrite(pins::BILGE_PUMP, s_pump ? HIGH : LOW);
}

bool bilge_water_detected() { return s_water; }
bool bilge_pump_active()    { return s_pump; }

void bilge_pump_set(bool on)
{
    s_pump = on;
    digitalWrite(pins::BILGE_PUMP, on ? HIGH : LOW);
    Serial.printf("bilge: pump %s\n", on ? "ON" : "OFF");
}
