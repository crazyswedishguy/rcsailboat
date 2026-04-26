#include "imu.h"
#include "config.h"
#include <Wire.h>
#include <math.h>

// QMI8658 register map (minimal subset for accelerometer-based attitude)
#define QMI8658_REG_WHOAMI 0x00  // should read 0x05
#define QMI8658_REG_CTRL1  0x02  // general config
#define QMI8658_REG_CTRL2  0x03  // accel ODR + range
#define QMI8658_REG_CTRL7  0x08  // sensor enable
#define QMI8658_REG_AX_L   0x35  // first of 6 accel bytes (AX_L .. AZ_H, little-endian)

// CTRL2: ±8g range (0b010 << 4) | 62.5 Hz ODR (0x05) = 0x25
// CTRL7: bit 0 = accel enable
#define QMI8658_CTRL1_AUTOINC  0x40
#define QMI8658_CTRL2_8G_62HZ  0x25
#define QMI8658_CTRL7_ACCEL_EN 0x01
#define QMI8658_ACCEL_SCALE    (8.0f / 32768.0f)  // g per LSB at ±8g full scale

static bool  s_ok    = false;
static float s_roll  = 0.0f;
static float s_pitch = 0.0f;

static bool qmi_write(uint8_t reg, uint8_t val) {
    Wire1.beginTransmission(QMI8658_ADDR);
    Wire1.write(reg);
    Wire1.write(val);
    return Wire1.endTransmission() == 0;
}

static bool qmi_read(uint8_t reg, uint8_t *buf, uint8_t len) {
    Wire1.beginTransmission(QMI8658_ADDR);
    Wire1.write(reg);
    if (Wire1.endTransmission(false) != 0) return false;
    Wire1.requestFrom((uint8_t)QMI8658_ADDR, len);
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire1.read();
    return true;
}

void imu_init() {
    uint8_t who = 0;
    if (!qmi_read(QMI8658_REG_WHOAMI, &who, 1) || who != 0x05) return;
    qmi_write(QMI8658_REG_CTRL1, QMI8658_CTRL1_AUTOINC);
    qmi_write(QMI8658_REG_CTRL2, QMI8658_CTRL2_8G_62HZ);
    qmi_write(QMI8658_REG_CTRL7, QMI8658_CTRL7_ACCEL_EN);
    s_ok = true;
}

void imu_update() {
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
}

float imu_roll_deg()  { return s_roll; }
float imu_pitch_deg() { return s_pitch; }
