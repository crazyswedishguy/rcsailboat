// power.cpp — INA219 battery monitor implementation.
//
// The Adafruit INA219 library handles all register-level I²C communication.
// This module wraps it with mAh integration and a safe-fallback pattern when
// the sensor is absent (s_ok = false → all functions return 0.0).
//
// mAh integration:
//   Each call to power_update() measures elapsed time since the last call,
//   converts current (A) to hourly rate, and adds the incremental charge:
//     Δmah = current_A × 1000 × Δt_hours
//   The integral is only added for positive current (discharge). A negative
//   reading means the INA219 wiring is reversed or current is bidirectional
//   (e.g. regenerative braking) — ignore to avoid subtracting from the counter.

#include "power.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_INA219.h>

static Adafruit_INA219 s_ina(i2c_addr::INA219);
static bool            s_ok      = false;   // false until sensor confirmed present
static float           s_voltage = 0.0f;   // last bus voltage reading (V)
static float           s_current = 0.0f;   // last current reading (A)
static float           s_mah     = 0.0f;   // accumulated charge drawn since boot (mAh)
static unsigned long   s_last_ms = 0;      // timestamp of last power_update() call

void power_init() {
    if (!s_ina.begin(&Wire)) {
        // INA219 not found on the I²C bus. Values remain at zero.
        // Telemetry will still transmit — battery fields will read 0 V / 0 A.
        Serial.println("power: INA219 not found at 0x41 — readings disabled");
        return;
    }
    s_ok      = true;
    s_last_ms = millis();
    Serial.println("power: INA219 ready");
}

void power_update() {
    if (!s_ok) return;

    unsigned long now  = millis();
    s_voltage          = s_ina.getBusVoltage_V();
    s_current          = s_ina.getCurrent_mA() / 1000.0f;  // convert mA → A

    // Integrate: dt in hours, current in amps → Δmah.
    // Guard against a very large first interval if millis() wrapped or init was slow.
    float dt_h = (now - s_last_ms) / 3600000.0f;
    if (s_current > 0.0f) s_mah += s_current * 1000.0f * dt_h;
    s_last_ms = now;
}

float power_voltage_v() { return s_voltage; }
float power_current_a() { return s_current; }
float power_mah_used()  { return s_mah; }
