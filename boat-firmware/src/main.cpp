#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "display.h"
#include "elrs.h"
#include "failsafe.h"
#include "imu.h"
#include "power.h"
#include "servos.h"
#include "telemetry.h"

#ifdef GPS_ENABLED
#include "gps.h"
#endif

// ---------------------------------------------------------------------------
// Phase 2 sweep — replace this block with elrs channel reads in Phase 3.
// Rudder/ESC: bipolar triangular wave ±1.0 over a 4 s period.
// Sail: unipolar triangular wave 0..1 over a 2 s period.
// ESC sweep is limited to ±0.3 so the motor doesn't spin up on the bench.
// ---------------------------------------------------------------------------
static void sweep_update() {
    unsigned long t = millis();

    // Bipolar triangular wave: 0 → 1 → -1 → 0 each 4 s
    uint32_t phase = t % 4000;
    float bipolar;
    if (phase < 1000)      bipolar =  (float)phase / 1000.0f;
    else if (phase < 3000) bipolar =  1.0f - (float)(phase - 1000) / 1000.0f;
    else                   bipolar = -1.0f + (float)(phase - 3000) / 1000.0f;

    // Unipolar triangular wave: 0 → 1 → 0 each 2 s
    uint32_t sphase = t % 2000;
    float unipolar = (sphase < 1000)
        ? (float)sphase / 1000.0f
        : 1.0f - (float)(sphase - 1000) / 1000.0f;

    servos_set(CH_RUDDER_PWM, bipolar);
    servos_set(CH_SAIL_PWM,   unipolar);
    servos_set(CH_ESC_PWM,    bipolar * 0.3f);  // gentle — propeller off on bench
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("rcsailboat firmware — Phase 2 bench test");

    pinMode(PIN_STATUS_LED, OUTPUT);

    // External I2C: PCA9685 + INA219 (+ QMC5883L when COMPASS_ENABLED)
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    // Onboard I2C: QMI8658 IMU + CST816S touch
    Wire1.begin(PIN_ONBOARD_I2C_SDA, PIN_ONBOARD_I2C_SCL);

    display_init();
    elrs_init();
    servos_init();   // I2C scan + PCA9685 init + ESC arm (blocks 2 s)
    imu_init();
    power_init();
    telemetry_init();
    failsafe_init();

#ifdef GPS_ENABLED
    gps_init();
#endif
}

void loop() {
    // Status LED: 500 ms heartbeat
    static unsigned long s_led_ms = 0;
    static bool s_led_on = false;
    if (millis() - s_led_ms >= 500) {
        s_led_ms = millis();
        s_led_on = !s_led_on;
        digitalWrite(PIN_STATUS_LED, s_led_on ? HIGH : LOW);
    }

    sweep_update();   // Phase 2 — remove in Phase 3

    // Display refresh at 5 Hz
    static unsigned long s_disp_ms = 0;
    if (millis() - s_disp_ms >= 200) {
        s_disp_ms = millis();
        display_update();
    }

    elrs_update();
    imu_update();
    power_update();
    failsafe_update();
    telemetry_update();

#ifdef GPS_ENABLED
    gps_update();
#endif
}
