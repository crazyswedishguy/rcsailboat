#pragma once

// GPIO pin assignments — keep in sync with docs/pinmap.md.
// Values are TBD until the Waveshare ESP32-S3 AMOLED pinout is confirmed.
#define PIN_I2C_SDA    -1  // TBD
#define PIN_I2C_SCL    -1  // TBD
#define PIN_CRSF_RX    -1  // TBD — UART RX, connects to RP3 TX
#define PIN_CRSF_TX    -1  // TBD — UART TX, connects to RP3 RX
#define PIN_STATUS_LED -1  // TBD

// I2C device addresses
// INA219 is moved to 0x41 (solder A0) to avoid collision with PCA9685 @ 0x40.
#define PCA9685_ADDR 0x40
#define INA219_ADDR  0x41

// PCA9685 output channel assignments
#define CH_RUDDER_PWM 0
#define CH_SAIL_PWM   1
#define CH_ESC_PWM    2
