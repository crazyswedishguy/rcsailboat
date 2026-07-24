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
// SX1262 LoRa radio — software SPI (both hardware SPI buses are occupied:
//   SPI2/FSPI = CO5300 AMOLED display via QSPI;
//   SPI3/HSPI = TF card).
// Bit-bang SPI is implemented in elrs.cpp via a SoftSPI wrapper class.
// =============================================================================
namespace pins {
// SPI data lines — software bit-bang on any free GPIOs
constexpr uint8_t SX_CLK   = 5;   // software SPI clock
constexpr uint8_t SX_MOSI  = 6;   // software SPI MOSI
constexpr uint8_t SX_MISO  = 7;   // software SPI MISO
// SPI chip-select and RadioLib control signals
constexpr uint8_t SX_CS    = 8;   // active-low chip select
constexpr uint8_t SX_RESET = 1;   // active-low module reset
constexpr uint8_t SX_DIO1  = 42;  // interrupt out: TX done / RX done / CAD done
constexpr uint8_t SX_BUSY  = 45;  // busy flag (GPIO45 = VDD_SPI strapping pin — safe as input post-boot)
// RF-switch control — Waveshare SX1262 module has separate RXEN / TXEN
constexpr uint8_t SX_RXEN  = 16;  // high = LNA enabled (RX mode)
constexpr uint8_t SX_TXEN  = 17;  // high = PA enabled  (TX mode)
}  // namespace pins


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

constexpr uint8_t  GPS_UART_NUM = 2;    // UART2 (UART0 = console; UART1 unused — freed from ELRS)
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
// LoRa radio parameters
// =============================================================================
constexpr float    LORA_FREQ_MHZ   = 915.0f;  // 915 MHz ISM band (US/AU); change to 868.0 for EU
constexpr float    LORA_BW_KHZ     = 500.0f;  // 500 kHz bandwidth — widest option; maximises throughput
constexpr uint8_t  LORA_SF         = 7;        // spreading factor 7 — fastest, shortest range
constexpr uint8_t  LORA_CR         = 5;        // coding rate 4/5
constexpr uint8_t  LORA_SYNC_WORD  = 0x12;    // private network; 0x34 for LoRaWAN public
constexpr int8_t   LORA_POWER_DBM  = 14;       // 14 dBm ≈ 25 mW — headroom before regulatory limit
constexpr uint16_t LORA_PREAMBLE   = 8;        // 8 symbols — RadioLib default

// =============================================================================
// Failsafe servo positions
//
// Applied when the radio link is lost or the WiFi connection times out.
// See docs/failsafe.md for rationale.
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
static_assert(pins::SX_RXEN != pins::SX_TXEN, "SX1262 RXEN and TXEN must be different pins");
static_assert(pins::SX_CS   != pins::SX_CLK && pins::SX_CS != pins::SX_MOSI && pins::SX_CS != pins::SX_MISO,
              "SX1262 CS pin must not alias CLK/MOSI/MISO");
static_assert(pwm_ch::RUDDER < 16 && pwm_ch::SAIL_WINCH < 16 && pwm_ch::MOTOR_ESC < 16,
              "PCA9685 channels must be in range 0..15");
static_assert(pwm_ch::RUDDER != pwm_ch::SAIL_WINCH &&
              pwm_ch::RUDDER != pwm_ch::MOTOR_ESC &&
              pwm_ch::SAIL_WINCH != pwm_ch::MOTOR_ESC,
              "PCA9685 channels must be unique");
static_assert(SERVO_PULSE_MIN_US < SERVO_PULSE_MID_US &&
              SERVO_PULSE_MID_US < SERVO_PULSE_MAX_US,
              "Servo pulse range must be monotonically increasing");
