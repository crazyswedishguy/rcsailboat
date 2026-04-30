#include <Arduino.h>
#include <Wire.h>

#include "bilge.h"
#include "config.h"
#include "display.h"
#include "elrs.h"
#include "failsafe.h"
#include "imu.h"
#include "power.h"
#include "protocol.h"
#include "sdlog.h"
#include "servos.h"
#include "telemetry.h"
#include "wifi_ctrl.h"

#ifdef GPS_ENABLED
#include "gps.h"
#endif
#ifdef COMPASS_ENABLED
#include "compass.h"
#endif

void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println("rcsailboat firmware starting");

    // Single I²C bus shared by PCA9685, INA219, FT3168 (touch), and QMI8658 (IMU).
    Wire.begin(pins::I2C_SDA, pins::I2C_SCL);

    display_init();     // SH8601 AMOLED via QSPI + LVGL (starts FreeRTOS render task)
    imu_init();         // QMI8658 6-axis IMU (shared I²C bus)
    bilge_init();       // float switch (GPIO2) + pump MOSFET (GPIO3)
    servos_init();      // PCA9685 init + ESC arm sequence (blocks ~2 s at neutral)
    power_init();       // INA219 current sensor (safe no-op if not wired)
    sdlog_init();       // TF card CSV logger (SPI3; safe no-op if no card)
    wifi_ctrl_init();   // WiFi AP + web control server (default control mode)
    elrs_init();        // CRSF hardware init (inactive until mode switched to ELRS)
    failsafe_init();    // failsafe state machine (stubbed — implemented in Phase 3)
    telemetry_init();   // CRSF telemetry emitter

#ifdef GPS_ENABLED
    gps_init();
#endif
#ifdef COMPASS_ENABLED
    compass_init();
#endif

    Serial.println("rcsailboat firmware ready");
}

void loop()
{
    // Handle WiFi HTTP requests and apply any queued mode switches.
    wifi_ctrl_update();

    // ── Mode-aware servo control ──────────────────────────────────────────────
    if (wifi_ctrl_mode() == CtrlMode::ELRS) {
        // ELRS mode: parse CRSF, run failsafe, apply channels directly.
        elrs_update();
        failsafe_update();
        servos_set(pwm_ch::RUDDER,     elrs_get_channel(CH_RUDDER));
        servos_set(pwm_ch::SAIL_WINCH, elrs_get_channel(CH_SAIL));
        servos_set(pwm_ch::MOTOR_ESC,  elrs_get_channel(CH_THROTTLE));
    } else {
        // WiFi mode: apply web UI commands; neutral if not armed or timed out.
        if (!wifi_ctrl_armed() || wifi_ctrl_timed_out()) {
            servos_set(pwm_ch::RUDDER,     0.0f);
            servos_set(pwm_ch::SAIL_WINCH, 0.0f);
            servos_set(pwm_ch::MOTOR_ESC,  0.0f);
        } else {
            servos_set(pwm_ch::RUDDER,     wifi_ctrl_rudder());
            servos_set(pwm_ch::SAIL_WINCH, wifi_ctrl_sail());
            servos_set(pwm_ch::MOTOR_ESC,  wifi_ctrl_throttle());
        }
    }

    // Read INA219 power data.
    power_update();

    // Update IMU orientation estimate and capsize detection.
    imu_update();

    // Poll bilge sensor and drive pump relay.
    bilge_update();

    // Poll FT3168 touch controller and cache result for LVGL (must run in main task).
    static unsigned long s_touch_ms = 0;
    if (millis() - s_touch_ms >= 20) {
        s_touch_ms = millis();
        display_poll_touch();
    }

    // Write one log row per second to the TF card.
    static unsigned long s_log_ms = 0;
    if (millis() - s_log_ms >= 1000) {
        s_log_ms = millis();
        sdlog_update();
    }

    // Emit CRSF telemetry frames on schedule (used in ELRS mode; no-op when WiFi).
    telemetry_update();

    // Refresh the LVGL telemetry display at 5 Hz.
    static unsigned long s_disp_ms = 0;
    if (millis() - s_disp_ms >= 200) {
        s_disp_ms = millis();
        display_update();
    }

#ifdef GPS_ENABLED
    gps_update();
#endif

#ifdef COMPASS_ENABLED
    static unsigned long s_compass_ms = 0;
    if (millis() - s_compass_ms >= 100) {  // 10 Hz
        s_compass_ms = millis();
        compass_update();
    }
#endif
}
