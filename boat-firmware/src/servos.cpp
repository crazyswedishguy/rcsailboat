// servos.cpp — PCA9685 servo/ESC driver implementation.
//
// Uses the Adafruit PWMServoDriver library to talk to the PCA9685 over I²C.
// All three actuators share the same PCA9685 at address 0x40 (config.h).
//
// Startup sequence (servos_init):
//   1. Scan the I²C bus and log all found addresses.
//   2. If PCA9685 is not found, set s_ok = false and return — all subsequent
//      servos_set() calls become no-ops, which is safe for bench testing.
//   3. Set PWM frequency to 50 Hz (standard hobby servo/ESC rate).
//   4. Drive all outputs to 1500 µs (neutral) and hold for 2 s so the ESC
//      completes its arming handshake before any throttle command is applied.
//
// servos_set() maps the normalised value to a pulse width in microseconds:
//   us = 1500 + value × 500
// This produces 1000 µs at –1.0, 1500 µs at 0.0, and 2000 µs at +1.0.
// The sail winch only ever receives 0.0..+1.0, so it maps to 1500–2000 µs.

#include "servos.h"
#include "config.h"
#include <Adafruit_PWMServoDriver.h>
#include <Arduino.h>
#include <Wire.h>

// Servo pulse width limits in microseconds.
// Matches the SERVO_PULSE_* constants in config.h (duplicated here as local #defines
// because writeMicroseconds() takes raw ints, not the constexpr values).
#define SERVO_US_MIN 1000   // full port / full reverse
#define SERVO_US_MID 1500   // neutral / centre
#define SERVO_US_MAX 2000   // full starboard / full forward

// How long to hold neutral before the ESC accepts throttle commands.
// Most hobby ESCs require ~1–2 s of stable neutral on power-up.
#define ESC_ARM_MS 2000

static Adafruit_PWMServoDriver s_pca(i2c_addr::PCA9685);
static bool  s_ok = false;          // false until PCA9685 is confirmed present
static float s_commanded[16] = {};  // last value sent to each channel (for telemetry readback)

// Scan the I²C bus and log every responding address. Runs at boot so we can
// verify that PCA9685 (0x40), INA219 (0x41), and HMC5883L (0x1E) are all present.
static void i2c_scan() {
    Serial.print("I2C scan (ext bus): ");
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("0x%02X ", addr);
            found++;
        }
    }
    if (!found) Serial.print("nothing found");
    Serial.println();
}

void servos_init() {
    i2c_scan();

    // Check that the PCA9685 is present before calling begin() — begin() on a
    // missing device hangs the I²C bus on some hardware revisions.
    Wire.beginTransmission(i2c_addr::PCA9685);
    if (Wire.endTransmission() != 0) {
        Serial.println("PCA9685 not found — servo output disabled");
        return;
    }

    s_pca.begin();
    s_pca.setPWMFreq(50);  // 50 Hz = 20 ms frame, standard for hobby servos and ESCs

    // Hold all outputs at neutral during ESC arming. The rudder and sail winch
    // will also park at mid-travel, which is correct for both.
    s_pca.writeMicroseconds(pwm_ch::RUDDER,     SERVO_US_MID);
    s_pca.writeMicroseconds(pwm_ch::SAIL_WINCH, SERVO_US_MID);
    s_pca.writeMicroseconds(pwm_ch::MOTOR_ESC,  SERVO_US_MID);
    delay(ESC_ARM_MS);   // blocks intentionally — ESC must hear neutral before anything else

    s_ok = true;
    Serial.println("PCA9685 ready");
}

void servos_set(int pca_channel, float value) {
    if (pca_channel < 0 || pca_channel >= 16) return;

    // Clamp to ±1.0 and cache for telemetry readback.
    float v = constrain(value, -1.0f, 1.0f);
    s_commanded[pca_channel] = v;

    if (!s_ok) return;  // PCA9685 not found — skip hardware write

    // Linear map: –1.0 → 1000 µs, 0.0 → 1500 µs, +1.0 → 2000 µs
    uint16_t us = (uint16_t)(SERVO_US_MID + v * (float)(SERVO_US_MAX - SERVO_US_MID));
    s_pca.writeMicroseconds(pca_channel, us);
}

float servos_get_commanded(int pca_channel) {
    if (pca_channel < 0 || pca_channel >= 16) return 0.0f;
    return s_commanded[pca_channel];
}
