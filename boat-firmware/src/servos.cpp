#include "servos.h"
#include "config.h"
#include <Adafruit_PWMServoDriver.h>
#include <Arduino.h>
#include <Wire.h>

// Standard hobby-servo pulse range in microseconds.
#define SERVO_US_MIN 1000
#define SERVO_US_MID 1500
#define SERVO_US_MAX 2000

// ESC must see neutral for this long before it will accept throttle commands.
#define ESC_ARM_MS 2000

static Adafruit_PWMServoDriver s_pca(i2c_addr::PCA9685);
static bool  s_ok = false;
static float s_commanded[16] = {};

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

    Wire.beginTransmission(i2c_addr::PCA9685);
    if (Wire.endTransmission() != 0) {
        Serial.println("PCA9685 not found — servo output disabled");
        return;
    }

    s_pca.begin();
    s_pca.setPWMFreq(50);  // standard servo / ESC frequency

    // Hold ESC at neutral while the ESC arms; servos park at mid-travel.
    s_pca.writeMicroseconds(pwm_ch::RUDDER,     SERVO_US_MID);
    s_pca.writeMicroseconds(pwm_ch::SAIL_WINCH, SERVO_US_MID);
    s_pca.writeMicroseconds(pwm_ch::MOTOR_ESC,  SERVO_US_MID);
    delay(ESC_ARM_MS);

    s_ok = true;
    Serial.println("PCA9685 ready");
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
