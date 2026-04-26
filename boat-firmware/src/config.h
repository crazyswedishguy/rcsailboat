#pragma once

// GPIO pin assignments — keep in sync with docs/pinmap.md.
// Derived from Waveshare ESP32-S3-Touch-AMOLED-1.64 schematic.
// IO47/IO48 are taken by the onboard I2C bus (IMU + touch); use a separate bus.
#define PIN_I2C_SDA    6   // External I2C bus — PCA9685 + INA219
#define PIN_I2C_SCL    7   // External I2C bus
#define PIN_CRSF_RX    17  // UART1 RX — receives CRSF from ELRS RP3 TX
#define PIN_CRSF_TX    18  // UART1 TX — sends CRSF to ELRS RP3 RX
#define PIN_STATUS_LED 9   // Status LED (active HIGH, add 330Ω to GND)

// Optional GPS module — build with -DGPS_ENABLED (see platformio.ini [env:esp32-s3-gps])
#ifdef GPS_ENABLED
#define PIN_GPS_RX 1  // UART2 RX — receives NMEA sentences from GPS module TX
#define PIN_GPS_TX 2  // UART2 TX — optional, used to reconfigure GPS baud rate
#endif

// I2C device addresses
// INA219 is moved to 0x41 (solder A0) to avoid collision with PCA9685 @ 0x40.
#define PCA9685_ADDR 0x40
#define INA219_ADDR  0x41

// PCA9685 output channel assignments
#define CH_RUDDER_PWM 0
#define CH_SAIL_PWM   1
#define CH_ESC_PWM    2
