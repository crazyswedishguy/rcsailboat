#include "imu.h"
#include "config.h"
#include <Arduino.h>
#include <Wire.h>
#include <math.h>

// QMI8658 register map (minimal subset for accelerometer-based attitude)
#define QMI8658_REG_WHOAMI 0x00  // should read 0x05
#define QMI8658_REG_CTRL1  0x02
#define QMI8658_REG_CTRL2  0x03
#define QMI8658_REG_CTRL7  0x08
#define QMI8658_REG_AX_L   0x35  // first of 6 accel bytes (AX_L..AZ_H, little-endian)

#define QMI8658_CTRL1_AUTOINC  0x40
#define QMI8658_CTRL2_8G_62HZ  0x25   // ±8g, 62.5 Hz ODR
#define QMI8658_CTRL7_ACCEL_EN 0x01
#define QMI8658_ACCEL_SCALE    (8.0f / 32768.0f)  // g per LSB at ±8g

#define CAPSIZE_ANGLE_DEG   110.0f
#define CAPSIZE_DEBOUNCE_MS 2000

static bool          s_ok            = false;
static float         s_roll          = 0.0f;
static float         s_pitch         = 0.0f;
static bool          s_capsized      = false;
static unsigned long s_capsize_start = 0;

static bool qmi_write(uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(i2c_addr::QMI8658);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static bool qmi_read(uint8_t reg, uint8_t *buf, uint8_t len)
{
    Wire.beginTransmission(i2c_addr::QMI8658);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom((int)i2c_addr::QMI8658, (int)len);
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

// Not called from main — IMU is not in scope. See CLAUDE.md "What to ask before doing".
void imu_init()
{
    uint8_t who = 0;
    if (!qmi_read(QMI8658_REG_WHOAMI, &who, 1) || who != 0x05) return;
    qmi_write(QMI8658_REG_CTRL1, QMI8658_CTRL1_AUTOINC);
    qmi_write(QMI8658_REG_CTRL2, QMI8658_CTRL2_8G_62HZ);
    qmi_write(QMI8658_REG_CTRL7, QMI8658_CTRL7_ACCEL_EN);
    s_ok = true;
}

// Not called from main — returns 0.0 safely when imu_init() was not called.
void imu_update()
{
    if (!s_ok) return;
    uint8_t buf[6];
    if (!qmi_read(QMI8658_REG_AX_L, buf, 6)) return;
    int16_t ax = (int16_t)((buf[1] << 8) | buf[0]);
    int16_t ay = (int16_t)((buf[3] << 8) | buf[2]);
    int16_t az = (int16_t)((buf[5] << 8) | buf[4]);
    float gx = ax * QMI8658_ACCEL_SCALE;
    float gy = ay * QMI8658_ACCEL_SCALE;
    float gz = az * QMI8658_ACCEL_SCALE;
    s_roll  = atan2f(gy, gz) * (180.0f / (float)M_PI);
    s_pitch = atan2f(-gx, sqrtf(gy * gy + gz * gz)) * (180.0f / (float)M_PI);

    if (fabsf(s_roll) > CAPSIZE_ANGLE_DEG) {
        if (!s_capsize_start) s_capsize_start = millis();
        if ((millis() - s_capsize_start) >= CAPSIZE_DEBOUNCE_MS && !s_capsized) {
            s_capsized = true;
            Serial.println("imu: CAPSIZE DETECTED");
        }
    } else {
        s_capsize_start = 0;
        s_capsized = false;
    }
}

float imu_roll_deg()    { return s_roll; }
float imu_pitch_deg()   { return s_pitch; }
bool  imu_is_capsized() { return s_capsized; }
