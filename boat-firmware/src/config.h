// config.h — central hardware configuration for the boat firmware.
//
// Single source of truth for GPIO pin numbers and closely-related hardware
// constants (I2C addresses, UART numbers, baud rates, channel assignments).
//
// Mirrors:
//   - docs/pinmap.md
//   - CLAUDE.md (Boat hardware / Pin map sections)
// If you change a value here, update both other places.
//
// Board: Waveshare ESP32-S3-Touch-AMOLED-1.64

#pragma once

#include <cstdint>

// =============================================================================
// I2C bus (shared by touch, IMU, PCA9685, INA219)
// =============================================================================
namespace pins {
constexpr uint8_t I2C_SDA = 47;
constexpr uint8_t I2C_SCL = 48;
}  // namespace pins

constexpr uint32_t I2C_FREQ_HZ = 400'000;  // 400 kHz fast mode

// I2C device addresses on this bus
namespace i2c_addr {
constexpr uint8_t FT3168   = 0x38;        // capacitive touch (onboard) — confirmed from Waveshare demo lcd_config.h
constexpr uint8_t PCA9685  = 0x40;        // servo driver (external)
constexpr uint8_t INA228   = 0x41;        // current/voltage sensor (external; A0=VS, A1=GND → 0x41)
constexpr uint8_t HMC5883L = 0x1E;        // compass on BN-880 GPS module (external)
constexpr uint8_t QMI8658  = 0x6B;        // IMU (onboard); verify with i2cdetect — may be 0x6A
}  // namespace i2c_addr


// =============================================================================
// ELRS receiver — CRSF over UART
// =============================================================================
namespace pins {
// Recommended placements on header P1 — confirm against actual wiring.
constexpr uint8_t CRSF_RX = 16;  // MCU RX ← receiver TX
constexpr uint8_t CRSF_TX = 17;  // MCU TX → receiver RX
}  // namespace pins

constexpr uint8_t  CRSF_UART_NUM = 1;          // hardware UART1 (UART0 is the USB console)
constexpr uint32_t CRSF_BAUD     = 420'000;    // CRSF standard


// =============================================================================
// PCA9685 servo / ESC driver
// =============================================================================
constexpr uint32_t PCA9685_PWM_FREQ_HZ = 50;   // standard hobby servo / ESC frame rate

// Channel assignments (0..15 on the PCA9685)
namespace pwm_ch {
constexpr uint8_t RUDDER     = 0;
constexpr uint8_t SAIL_WINCH = 1;
constexpr uint8_t MOTOR_ESC  = 2;
}  // namespace pwm_ch

// Pulse width range in microseconds (calibrate per-servo before relying on these)
constexpr uint16_t SERVO_PULSE_MIN_US = 1000;
constexpr uint16_t SERVO_PULSE_MID_US = 1500;
constexpr uint16_t SERVO_PULSE_MAX_US = 2000;


// =============================================================================
// GPS module — NMEA over UART2 (BN-880, u-blox M8N, 9600 baud default)
// =============================================================================
namespace pins {
constexpr uint8_t GPS_RX = 15;   // MCU RX ← GPS TX  (free on header P1)
constexpr uint8_t GPS_TX = 18;   // MCU TX → GPS RX  (free on header P1; optional)
}  // namespace pins

constexpr uint8_t  GPS_UART_NUM = 2;    // UART2 (UART0 = console, UART1 = CRSF)
constexpr uint32_t GPS_BAUD     = 9600; // BN-880 factory default

// =============================================================================
// Bilge monitoring
// =============================================================================
namespace pins {
constexpr uint8_t BILGE_SENSOR = 2;   // GPIO2 — DYIables water sensor SIG pin (ADC1_CH1); wire VCC→3.3V GND→GND
constexpr uint8_t BILGE_PUMP   = 3;   // GPIO3 — N-MOSFET gate driving the bilge pump
}  // namespace pins

// =============================================================================
// WiFi Direct mode
// =============================================================================
#define WIFI_AP_SSID "Mistral"
#define WIFI_AP_PASS "readyabout"

// =============================================================================
// TF card (SPI mode — on SPI3/HSPI, separate from display's SPI2/FSPI)
// =============================================================================
namespace pins {
constexpr uint8_t SD_CS   = 38;
constexpr uint8_t SD_MOSI = 39;
constexpr uint8_t SD_MISO = 40;
constexpr uint8_t SD_SCLK = 41;
}  // namespace pins


// =============================================================================
// Battery monitoring
// =============================================================================
namespace pins {
constexpr uint8_t BAT_ADC = 4;             // GPIO4 → BAT_ADC via on-board divider
}  // namespace pins

// On-board divider — calibrate against a known voltage before trusting these.
// Values here are placeholders; replace after measurement.
constexpr float BAT_ADC_DIVIDER_RATIO = 2.0f;   // Vbat = Vadc * ratio
constexpr float BAT_ADC_VREF          = 3.3f;


// =============================================================================
// Onboard peripherals NOT currently used by the firmware.
//
// These are defined for reference only. Before enabling any of these in code,
// see CLAUDE.md "What to ask before doing" — the architecture intentionally
// leaves the display, touch, and IMU out of scope for now.
// =============================================================================
namespace pins_unused {

// AMOLED display (CO5300, QSPI)
constexpr uint8_t  OLED_CS    = 9;
constexpr uint8_t  OLED_CLK   = 10;
constexpr uint8_t  OLED_D0    = 11;
constexpr uint8_t  OLED_D1    = 12;
constexpr uint8_t  OLED_D2    = 13;
constexpr uint8_t  OLED_D3    = 14;
constexpr uint8_t  OLED_RESET = 21;
constexpr int8_t   OLED_TE    = -1;        // not used — Waveshare demo does not wire TE

// Touch (FT3168)
constexpr int8_t   TOUCH_INT   = -1;       // not used — demo polls I²C directly, no INT line to MCU
constexpr int8_t   TOUCH_RESET = -1;       // not used — no separate reset pin

// IMU (QMI8658)
constexpr uint8_t  IMU_INT1 = 46;          // strapping pin — input only at boot
// IMU INT2 is not connected on this board

}  // namespace pins_unused


// =============================================================================
// Failsafe servo positions
//
// Applied when the ELRS link is lost (ELRS mode) or the WiFi connection
// times out (WiFi mode). See docs/failsafe.md for rationale.
//
// SAIL assumes servo minimum (−1.0) = sail fully eased (let out).
// Verify against your physical rig and flip the sign if your winch is
// rigged the opposite way.
//
// RUDDER = +1.0 (full starboard). A drifting boat with one-sided rudder
// turns in slow circles, keeping it in roughly the same area.
// =============================================================================
namespace failsafe_pos {
constexpr float THROTTLE = 0.0f;   // motor off
constexpr float SAIL     = -1.0f;  // fully eased — sail luffs, boat decelerates
constexpr float RUDDER   = 1.0f;   // full starboard — circular drift pattern
}  // namespace failsafe_pos


// =============================================================================
// Build-time sanity checks
// =============================================================================
static_assert(pins::CRSF_RX != pins::CRSF_TX,
              "CRSF RX and TX must be different pins");
static_assert(pwm_ch::RUDDER < 16 && pwm_ch::SAIL_WINCH < 16 && pwm_ch::MOTOR_ESC < 16,
              "PCA9685 channels must be in range 0..15");
static_assert(pwm_ch::RUDDER != pwm_ch::SAIL_WINCH &&
              pwm_ch::RUDDER != pwm_ch::MOTOR_ESC &&
              pwm_ch::SAIL_WINCH != pwm_ch::MOTOR_ESC,
              "PCA9685 channels must be unique");
static_assert(SERVO_PULSE_MIN_US < SERVO_PULSE_MID_US &&
              SERVO_PULSE_MID_US < SERVO_PULSE_MAX_US,
              "Servo pulse range must be monotonically increasing");
