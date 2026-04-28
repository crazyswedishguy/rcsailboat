#include "power.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_INA219.h>

static Adafruit_INA219 s_ina(i2c_addr::INA219);
static bool            s_ok      = false;
static float           s_voltage = 0.0f;
static float           s_current = 0.0f;
static float           s_mah     = 0.0f;
static unsigned long   s_last_ms = 0;

void power_init() {
    if (!s_ina.begin(&Wire)) {
        // INA219 not found — values remain zero; telemetry will still send zeros.
        return;
    }
    s_ok      = true;
    s_last_ms = millis();
}

void power_update() {
    if (!s_ok) return;
    unsigned long now = millis();
    s_voltage = s_ina.getBusVoltage_V();
    s_current = s_ina.getCurrent_mA() / 1000.0f;
    float dt_h = (now - s_last_ms) / 3600000.0f;
    if (s_current > 0.0f) s_mah += s_current * 1000.0f * dt_h;
    s_last_ms = now;
}

float power_voltage_v() { return s_voltage; }
float power_current_a() { return s_current; }
float power_mah_used()  { return s_mah; }
