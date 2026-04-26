#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "elrs.h"
#include "failsafe.h"
#include "imu.h"
#include "power.h"
#include "servos.h"
#include "telemetry.h"

#ifdef GPS_ENABLED
#include "gps.h"
#endif

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("rcsailboat firmware v0 — Phase 0 scaffold");

    // External I2C: PCA9685 + INA219 (+ QMC5883L when COMPASS_ENABLED)
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    // Onboard I2C: QMI8658 IMU + CST816S touch
    Wire1.begin(PIN_ONBOARD_I2C_SDA, PIN_ONBOARD_I2C_SCL);

    elrs_init();
    servos_init();
    imu_init();
    power_init();
    telemetry_init();
    failsafe_init();

#ifdef GPS_ENABLED
    gps_init();
#endif
}

void loop() {
    elrs_update();
    imu_update();
    power_update();
    failsafe_update();
    telemetry_update();

#ifdef GPS_ENABLED
    gps_update();
#endif
}
