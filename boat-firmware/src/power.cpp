// power.cpp — INA228 battery monitor implementation.
//
// The robtillaart INA228 library handles all register-level I²C communication.
// This module wraps it with mAh integration and a safe-fallback pattern when
// the sensor is absent (s_ok = false → all functions return 0.0).
//
// Shunt: 50A / 1.5mΩ bus bar shunt in the main battery positive line.
// getBusVoltage() returns voltage at IN+ = battery positive (9–12.6V).
// getCurrent() returns amps directly.
//
// mAh integration:
//   Δmah = current_A × 1000 × Δt_hours
//   Only integrated for positive current (discharge direction).

#include "power.h"
#include "config.h"
#include <Wire.h>
#include <INA228.h>

static INA228        s_ina(i2c_addr::INA228);   // address in constructor; uses global Wire
static bool          s_ok      = false;
static float         s_voltage = 0.0f;
static float         s_current = 0.0f;
static float         s_mah     = 0.0f;
static unsigned long s_last_ms = 0;

void power_init() {
    if (!s_ina.begin()) {
        Serial.println("power: INA228 not found at 0x41 — readings disabled");
        return;
    }
    // 60A max, 1.5mΩ shunt (50A/75mV bus bar). Motor stall may briefly exceed 50A.
    s_ina.setMaxCurrentShunt(60.0f, 0.0015f);
    s_ok      = true;
    s_last_ms = millis();
    Serial.println("power: INA228 ready");
}

void power_update() {
    if (!s_ok) return;

    unsigned long now = millis();
    s_voltage = s_ina.getBusVoltage();   // volts at IN+ = battery voltage
    s_current = s_ina.getCurrent();      // amps

    float dt_h = (now - s_last_ms) / 3600000.0f;
    if (s_current > 0.0f) s_mah += s_current * 1000.0f * dt_h;
    s_last_ms = now;
}

float power_voltage_v() { return s_voltage; }
float power_current_a() { return s_current; }
float power_mah_used()  { return s_mah; }
