// servos.cpp — PCA9685 servo/ESC driver implementation.
//
// Uses the Adafruit PWMServoDriver library to talk to the PCA9685 over I²C.
// All three actuators share the same PCA9685 at address 0x40 (config.h).
//
// servos_init() is a no-op if diag_ok(DEV_PCA9685) is false (device absent or
// disabled).  When the PCA9685 is present it:
//   1. Sets PWM frequency to 50 Hz (standard hobby servo/ESC rate).
//   2. Drives all outputs to 1500 µs (neutral) and holds for 2 s so the ESC
//      completes its arming handshake before any throttle command is applied.
//
// servos_set() maps the normalised value to a pulse width in microseconds:
//   us = 1500 + value × 500
// This produces 1000 µs at –1.0, 1500 µs at 0.0, and 2000 µs at +1.0.
// The sail winch only ever receives 0.0..+1.0, so it maps to 1500–2000 µs.

#include "servos.h"
#include "config.h"
#include "diag.h"
#include <Adafruit_PWMServoDriver.h>
#include <Arduino.h>

// Servo pulse width limits in microseconds.
#define SERVO_US_MIN 1000   // full port / full reverse
#define SERVO_US_MID 1500   // neutral / centre
#define SERVO_US_MAX 2000   // full starboard / full forward

// Most hobby ESCs require ~1–2 s of stable neutral on power-up.
#define ESC_ARM_MS 2000

static Adafruit_PWMServoDriver s_pca(i2c_addr::PCA9685);
static bool  s_ok = false;
static float s_commanded[16] = {};

void servos_init() {
    s_ok = false;
    if (!diag_ok(DEV_PCA9685)) {
        Serial.println("servos: PCA9685 absent — disabled");
        return;
    }

    s_pca.begin();
    s_pca.setPWMFreq(50);  // 50 Hz = 20 ms frame, standard for hobby servos and ESCs

    // Hold all outputs at neutral during ESC arming.
    s_pca.writeMicroseconds(pwm_ch::RUDDER,     SERVO_US_MID);
    s_pca.writeMicroseconds(pwm_ch::SAIL_WINCH, SERVO_US_MID);
    s_pca.writeMicroseconds(pwm_ch::MOTOR_ESC,  SERVO_US_MID);
    delay(ESC_ARM_MS);   // blocks intentionally — ESC must hear neutral before anything else

    s_ok = true;
    Serial.println("servos: PCA9685 ready");
}

void servos_set(int pca_channel, float value) {
    if (pca_channel < 0 || pca_channel >= 16) return;

    float v = constrain(value, -1.0f, 1.0f);
    s_commanded[pca_channel] = v;

    if (!s_ok) return;

    uint16_t us = (uint16_t)(SERVO_US_MID + v * (float)(SERVO_US_MAX - SERVO_US_MID));
    s_pca.writeMicroseconds(pca_channel, us);
}

float servos_get_commanded(int pca_channel) {
    if (pca_channel < 0 || pca_channel >= 16) return 0.0f;
    return s_commanded[pca_channel];
}
