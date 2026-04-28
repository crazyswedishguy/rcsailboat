#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "display.h"
#include "elrs.h"
#include "failsafe.h"
#include "power.h"
#include "protocol.h"
#include "servos.h"
#include "telemetry.h"

#ifdef GPS_ENABLED
#include "gps.h"
#endif

void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println("rcsailboat firmware starting");

    // Single I²C bus shared by PCA9685, INA219, FT3168 (touch), and QMI8658 (IMU).
    Wire.begin(pins::I2C_SDA, pins::I2C_SCL);

    display_init();     // SH8601 AMOLED via QSPI + LVGL (starts FreeRTOS render task)
    servos_init();      // PCA9685 init + ESC arm sequence (blocks ~2 s at neutral)
    power_init();       // INA219 current sensor (safe no-op if not wired)
    elrs_init();        // CRSF over UART1 (stubbed — implement in Phase 3)
    failsafe_init();    // failsafe state machine (stubbed)
    telemetry_init();   // CRSF telemetry emitter

#ifdef GPS_ENABLED
    gps_init();
#endif

    Serial.println("rcsailboat firmware ready");
}

void loop()
{
    // Parse incoming CRSF frames and update link statistics.
    elrs_update();

    // Check link timeout; apply failsafe positions if link is lost.
    failsafe_update();

    // Map ELRS channels to PCA9685 outputs.
    // elrs_get_channel() returns 0.0 while ELRS is stubbed → servos hold neutral.
    servos_set(pwm_ch::RUDDER,     elrs_get_channel(CH_RUDDER));
    servos_set(pwm_ch::SAIL_WINCH, elrs_get_channel(CH_SAIL));
    servos_set(pwm_ch::MOTOR_ESC,  elrs_get_channel(CH_THROTTLE));

    // Read INA219 power data.
    power_update();

    // Emit CRSF telemetry frames on schedule.
    telemetry_update();

    // Refresh the LVGL telemetry labels at 5 Hz.
    static unsigned long s_disp_ms = 0;
    if (millis() - s_disp_ms >= 200) {
        s_disp_ms = millis();
        display_update();
    }

#ifdef GPS_ENABLED
    gps_update();
#endif
}
