// main.cpp — Arduino entry point for the RC sailboat firmware.
//
// Runs on a Waveshare ESP32-S3-Touch-AMOLED-1.64 (ESP32-S3R8, dual-core LX7).
// The main Arduino loop() executes on core 1. The LVGL render task (started by
// display_init()) runs on core 0 so UI rendering never blocks servo updates.
//
// Control modes:
//   WiFi Direct (default) — ESP32 broadcasts its own AP; browser sends HTTP commands.
//   ELRS — CRSF channels received from the ELRS receiver on UART1.
//   Mode is toggled via the touchscreen display. See wifi_ctrl.h for the API.
//
// Build environments (platformio.ini):
//   esp32-s3        — base build (no GPS, no compass)
//   esp32-s3-gps    — adds TinyGPS++ parser on UART2 (-DGPS_ENABLED)
//   esp32-s3-compass — adds HMC5883L heading via I²C (-DCOMPASS_ENABLED)
//   esp32-s3-full   — GPS + compass
//
// Timing notes:
//   - No delay() calls in loop() — all rate-limiting uses millis() timestamps.
//   - Touch polled at 50 Hz; display updated at 5 Hz; SD logged at 1 Hz.
//   - GPS update is called every iteration (byte-by-byte, non-blocking).
//   - Compass sampled at 10 Hz (HMC5883L outputs at up to 75 Hz; 10 Hz is enough).

#include <Arduino.h>
#include <Wire.h>

#include "bilge.h"
#include "config.h"
#include "diag.h"
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

    // Single I²C bus shared by FT3168 (touch), QMI8658 (IMU), PCA9685, and INA228.
    // Must use Wire (i2c-ng) — pioarduino 3.x pre-installs the i2c-ng driver before
    // setup() runs. Calling i2c_driver_install() (legacy API) after that triggers a
    // conflict abort.
    Wire.begin(pins::I2C_SDA, pins::I2C_SCL);
    Wire.setClock(I2C_FREQ_HZ);

    // Probe all I²C devices before any module init.  diag_init() ACK-tests each
    // address, marks absent devices as disabled, and recovers Wire after every
    // NACK so the next probe starts clean.  Module inits then gate on diag_ok()
    // and skip hardware access for absent devices — no more cascading 259 errors.
    diag_register_reinit(DEV_QMI8658, imu_init);
    diag_register_reinit(DEV_PCA9685, servos_init);
    diag_register_reinit(DEV_INA228,  power_init);
    diag_init();

    // display_init() is intentionally deferred to after wifi_ctrl_init().
    // The FT3168 touch controller enters Standby after ~10 s of no I²C traffic
    // and can only be woken by a touch event. diag_init() probes FT3168 at ~313 ms;
    // WiFi AP init takes ~8 s. By calling display_init() immediately after WiFi,
    // FT3168 is still within its sleep window and the init write succeeds. After
    // that, display_poll_touch() at 50 Hz keeps it awake indefinitely.
    imu_init();         // QMI8658 6-axis IMU (shared I²C bus)
    bilge_init();       // float switch (GPIO2) + pump MOSFET (GPIO3)
    servos_init();      // PCA9685 init + ESC arm sequence (blocks ~2 s at neutral)
    power_init();       // INA228 current/voltage sensor
    sdlog_init();       // TF card CSV logger (SPI3; safe no-op if no card)
    wifi_ctrl_init();   // WiFi AP + web control server (default control mode)
    elrs_init();        // CRSF UART1 + AlfredoCRSF (Phase 3)
    failsafe_init();    // BOOT/DISARMED/ARMED/FAILSAFE state machine (Phase 3)
    telemetry_init();   // CRSF telemetry emitter
    display_init();     // SH8601 AMOLED via QSPI + LVGL (starts FreeRTOS render task)

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
    // Dispatch any pending repair request from the display Repair button.
    // Must run here on core 1 — Wire is not thread-safe.
    {
        DevId req = diag_pending_reprobe();
        if (req < DEV_COUNT) {
            diag_clear_reprobe();
            diag_reprobe(req);
        }
    }

    // Handle WiFi HTTP requests and apply any queued mode switches.
    wifi_ctrl_update();

    // ── Mode-aware servo control ──────────────────────────────────────────────
    if (wifi_ctrl_mode() == CtrlMode::ELRS) {
        // ELRS mode: parse CRSF, run failsafe, apply channels or safe positions.
        elrs_update();
        failsafe_update();
        if (failsafe_active()) {
            servos_set(pwm_ch::RUDDER,     failsafe_pos::RUDDER);
            servos_set(pwm_ch::SAIL_WINCH, failsafe_pos::SAIL);
            servos_set(pwm_ch::MOTOR_ESC,  failsafe_pos::THROTTLE);
        } else {
            servos_set(pwm_ch::RUDDER,     elrs_get_channel(CH_RUDDER));
            servos_set(pwm_ch::SAIL_WINCH, elrs_get_channel(CH_SAIL));
            servos_set(pwm_ch::MOTOR_ESC,  elrs_get_channel(CH_THROTTLE));

            // Bilge pump from CH_PUMP (manual override; not arm-gated). Only
            // touch it when the commanded state changes, to avoid log spam.
            // On failsafe (handled above) the pump is deliberately left in its
            // last state — a flooding hull may need it to keep running.
            bool pump_cmd = elrs_get_channel(CH_PUMP) > 0.5f;
            if (pump_cmd != bilge_pump_active()) {
                bilge_pump_set(pump_cmd);
            }
        }
    } else {
        // WiFi mode: apply web UI commands; failsafe positions if not armed or timed out.
        if (!wifi_ctrl_armed() || wifi_ctrl_timed_out()) {
            servos_set(pwm_ch::RUDDER,     failsafe_pos::RUDDER);
            servos_set(pwm_ch::SAIL_WINCH, failsafe_pos::SAIL);
            servos_set(pwm_ch::MOTOR_ESC,  failsafe_pos::THROTTLE);
        } else {
            servos_set(pwm_ch::RUDDER,     wifi_ctrl_rudder());
            servos_set(pwm_ch::SAIL_WINCH, wifi_ctrl_sail());
            servos_set(pwm_ch::MOTOR_ESC,  wifi_ctrl_throttle());
        }
    }

    // Read INA228 power data at 10 Hz.
    static unsigned long s_power_ms = 0;
    if (millis() - s_power_ms >= 100) {
        s_power_ms = millis();
        power_update();
    }

    // Update IMU orientation estimate and capsize detection at 50 Hz.
    static unsigned long s_imu_ms = 0;
    if (millis() - s_imu_ms >= 20) {
        s_imu_ms = millis();
        imu_update();
    }

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
