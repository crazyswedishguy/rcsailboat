// imu.cpp — QMI8658 accelerometer-based attitude estimator.
//
// Communicates with the QMI8658 via I²C using raw register reads/writes rather
// than a library — the QMI8658 is uncommon and well-documented register-level.
//
// Attitude computation (accelerometer-only):
//   The gravity vector (gx, gy, gz) projected onto the sensor axes gives roll
//   and pitch via:
//     roll  = atan2(gy, gz)          — rotation around the fore-aft axis (heel)
//     pitch = atan2(-gx, √(gy²+gz²)) — rotation around the athwartships axis (trim)
//
//   This is accurate when the boat is not accelerating, which is a reasonable
//   assumption for the slow speeds typical of a radio-controlled sailboat.
//   Adding gyroscope fusion would improve dynamic accuracy but is not worth the
//   complexity at this stage.
//
// Register map used (from QMI8658 datasheet in docs/datasheets/):
//   0x00 WHOAMI  — should read 0x05 to confirm device identity
//   0x02 CTRL1   — bit 6 (AUTOINC) enables auto-address-increment on burst reads
//   0x03 CTRL2   — accel ODR and range (0x25 = ±8g, 62.5 Hz)
//   0x08 CTRL7   — sensor enable bits (0x01 = accel on)
//   0x35 AX_L    — first byte of 6-byte accel burst (AX_L, AX_H, AY_L, AY_H, AZ_L, AZ_H)

#include "imu.h"
#include "config.h"
#include <Arduino.h>
#include <Wire.h>
#include <math.h>

// QMI8658 register addresses (minimal subset for accelerometer attitude)
#define QMI8658_REG_WHOAMI 0x00  // device ID register — must read 0x05
#define QMI8658_REG_CTRL1  0x02  // general control: auto-increment, SPI/I2C mode
#define QMI8658_REG_CTRL2  0x03  // accelerometer: ODR and full-scale range
#define QMI8658_REG_CTRL7  0x08  // sensor enable: which sensors are active
#define QMI8658_REG_AX_L   0x35  // start of 6-byte accel burst (little-endian pairs)

// Control register values
#define QMI8658_CTRL1_AUTOINC  0x40   // enable register address auto-increment on burst reads
#define QMI8658_CTRL2_8G_62HZ  0x25   // accel: ±8g full scale, 62.5 Hz output data rate
#define QMI8658_CTRL7_ACCEL_EN 0x01   // enable accelerometer, leave gyroscope off

// Scale factor: converts a raw 16-bit signed integer to g (gravitational units).
// At ±8g full scale, 1 g = 32768/8 = 4096 LSB.
#define QMI8658_ACCEL_SCALE    (8.0f / 32768.0f)

// Capsize threshold and hold time before declaring capsize
#define CAPSIZE_ANGLE_DEG   110.0f   // degrees of roll to consider capsized
#define CAPSIZE_DEBOUNCE_MS 2000     // must sustain this angle for 2 s before flagging

static bool          s_ok            = false;  // false if WHOAMI failed or I²C error
static float         s_roll          = 0.0f;   // heel angle (degrees)
static float         s_pitch         = 0.0f;   // pitch angle (degrees)
static bool          s_capsized      = false;  // capsize flag (self-clears on recovery)
static unsigned long s_capsize_start = 0;      // millis() when roll first exceeded threshold

// Write a single byte to a QMI8658 register. Returns true on I²C ACK.
static bool qmi_write(uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(i2c_addr::QMI8658);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

// Burst-read len bytes starting at reg into buf[]. Returns true on success.
// The AUTOINC flag in CTRL1 makes the QMI8658 increment the address automatically,
// so we can read all 6 accel bytes in a single I²C transaction.
static bool qmi_read(uint8_t reg, uint8_t *buf, uint8_t len)
{
    Wire.beginTransmission(i2c_addr::QMI8658);
    Wire.write(reg);
    // Use STOP (true) not repeated-start (false): the esp32-s3 i2c-ng driver's
    // combined write-read path (i2c_master_transmit_receive) returns
    // ESP_ERR_INVALID_STATE (259) under load. Two separate transactions are reliable.
    if (Wire.endTransmission(true) != 0) return false;
    Wire.requestFrom((int)i2c_addr::QMI8658, (int)len);
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

void imu_init()
{
    // Verify device identity before configuring — prevents corruption if address is wrong.
    uint8_t who = 0;
    if (!qmi_read(QMI8658_REG_WHOAMI, &who, 1) || who != 0x05) {
        Serial.printf("imu: QMI8658 not found (WHOAMI=0x%02X, expected 0x05)\n", who);
        return;
    }
    qmi_write(QMI8658_REG_CTRL1, QMI8658_CTRL1_AUTOINC);   // enable burst reads
    qmi_write(QMI8658_REG_CTRL2, QMI8658_CTRL2_8G_62HZ);   // accel: ±8g, 62.5 Hz
    qmi_write(QMI8658_REG_CTRL7, QMI8658_CTRL7_ACCEL_EN);  // power on accelerometer
    s_ok = true;
    Serial.println("imu: QMI8658 ready");
}

void imu_update()
{
    if (!s_ok) return;

    // Read 6 bytes: AX_L, AX_H, AY_L, AY_H, AZ_L, AZ_H (little-endian pairs)
    uint8_t buf[6];
    if (!qmi_read(QMI8658_REG_AX_L, buf, 6)) return;

    // Reconstruct signed 16-bit values from little-endian byte pairs
    int16_t ax = (int16_t)((buf[1] << 8) | buf[0]);
    int16_t ay = (int16_t)((buf[3] << 8) | buf[2]);
    int16_t az = (int16_t)((buf[5] << 8) | buf[4]);

    // Convert raw counts to g using scale factor
    float gx = ax * QMI8658_ACCEL_SCALE;
    float gy = ay * QMI8658_ACCEL_SCALE;
    float gz = az * QMI8658_ACCEL_SCALE;

    // Compute roll and pitch from the gravity vector orientation.
    // When the boat is stationary, (gx, gy, gz) is approximately the unit gravity vector.
    s_roll  = atan2f(gy, gz) * (180.0f / (float)M_PI);
    s_pitch = atan2f(-gx, sqrtf(gy * gy + gz * gz)) * (180.0f / (float)M_PI);

    // Capsize detection with 2 s debounce: flag only if roll stays extreme.
    if (fabsf(s_roll) > CAPSIZE_ANGLE_DEG) {
        if (!s_capsize_start) s_capsize_start = millis();
        if ((millis() - s_capsize_start) >= CAPSIZE_DEBOUNCE_MS && !s_capsized) {
            s_capsized = true;
            Serial.println("imu: CAPSIZE DETECTED");
        }
    } else {
        // Boat is upright — clear both the timer and the flag.
        s_capsize_start = 0;
        s_capsized = false;
    }
}

float imu_roll_deg()    { return s_roll; }
float imu_pitch_deg()   { return s_pitch; }
bool  imu_is_capsized() { return s_capsized; }
