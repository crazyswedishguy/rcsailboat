# Pin map — ESP32-S3 (Waveshare 1.64" AMOLED dev board)

**This is the canonical pin assignment.** The firmware's `config.h` must match this table exactly. When pins change, update both simultaneously.

## ESP32-S3 pin assignments

| GPIO | Direction | Function | Connects to | Notes |
|-----:|-----------|----------|-------------|-------|
| IO6  | BIDIR | I²C SDA | PCA9685 SDA, INA219 SDA | External bus; pull-ups on PCA9685 breakout are sufficient |
| IO7  | BIDIR | I²C SCL | PCA9685 SCL, INA219 SCL | External bus |
| IO17 | IN  | UART1 RX (CRSF from ELRS) | ELRS RP3 receiver TX pin | 420000 baud, CRSF; use UART1 (UART0 = USB-CDC on IO43/44) |
| IO18 | OUT | UART1 TX (CRSF to ELRS) | ELRS RP3 receiver RX pin | 420000 baud, CRSF |
| IO9  | OUT | Status LED | External LED + resistor to GND | Heartbeat / failsafe indicator |
| IO1  | IN  | UART2 RX (NMEA from GPS) | GPS module TX pin | **Optional** — requires `-DGPS_ENABLED` build flag; 9600 baud NMEA |
| IO2  | OUT | UART2 TX (config to GPS) | GPS module RX pin | **Optional** — only needed to change GPS baud rate or rate config |

## Pins consumed by the board itself — do not use

Derived from the Waveshare ESP32-S3-Touch-AMOLED-1.64 schematic:

| GPIO | Function |
|-----:|----------|
| IO0  | BOOT strapping pin |
| IO4  | Battery voltage ADC (internal) |
| IO10 | OLED QSPI CS |
| IO11 | OLED QSPI CLK |
| IO12 | OLED QSPI D0 |
| IO13 | OLED QSPI D1 |
| IO14 | OLED QSPI D2 |
| IO15 | OLED QSPI D3 |
| IO19 | USB D− |
| IO20 | USB D+ |
| IO21 | OLED RESET |
| IO38 | SD card CS / SDIO D3 |
| IO39 | SD card MOSI / SDIO CMD |
| IO40 | SD card MISO / SDIO D0 |
| IO41 | SD card CLK / SDIO SCK |
| IO43 | UART0 TX (USB-CDC serial monitor) |
| IO44 | UART0 RX (USB-CDC serial monitor) |
| IO45 | LCD CLK / boot strapping |
| IO46 | IMU INT1 (QMI8658) |
| IO47 | Onboard I²C SDA (IMU QMI8658 + touch CST816S) |
| IO48 | Onboard I²C SCL (IMU QMI8658 + touch CST816S) |

## Free GPIOs (available for external peripherals)

| GPIO | Status |
|---:|---|
| IO1 | **GPS RX** (when GPS_ENABLED) |
| IO2 | **GPS TX** (when GPS_ENABLED) |
| IO3, IO5, IO8, IO16, IO42 | Unassigned — available for future use |

## I²C addresses

The external bus (IO6/IO7) carries only our peripherals:

| Device | Address | Notes |
|---|---|---|
| PCA9685 | 0x40 (default) | PWM servo driver |
| INA219 | 0x41 | **Moved from default 0x40** — solder A0 jumper on INA219 breakout |

The onboard I²C bus (IO47/IO48) carries only the board's own IMU and touch controller — do not connect external devices to those pins.

> **Address collision**: PCA9685 and INA219 both default to 0x40. Move INA219 to 0x41 by soldering the A0 pad on the INA219 breakout *before* first power-on.

## PCA9685 channel assignments

| PCA9685 channel | Purpose | PWM neutral (µs) | PWM range (µs) | Notes |
|---:|---|---:|---|---|
| 0 | Rudder servo | 1500 | 1000–2000 | Reverse direction in firmware if mounted flipped |
| 1 | Sail winch servo | 1500 | ~500–2500 | Winch servos have extended travel — verify with HS-785HB datasheet |
| 2 | Motor ESC | 1500 | 1000–2000 | ESC must see 1500 µs at power-on to arm |
| 3–15 | Reserved | — | — | |

PCA9685 PWM frequency: **50 Hz** (standard servo/ESC rate).
