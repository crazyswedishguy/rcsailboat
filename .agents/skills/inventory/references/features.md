# Inventory Skill — Features Reference

Maps feature keywords to common implementations.

**How to use:**
- **Detection (Step 2):** search component names, GPIO identifiers, and README/TODO text for
  the keywords in the "Keywords" column. Any match means the feature is likely intended.
- **Gap notes (Step 3):** when a feature has no BOM component and multiple implementations
  exist, present the "Alternatives (if gap)" column verbatim. Do not choose for the user.
- **Single implementation:** if only one approach is listed and the BOM has no match,
  add it as 🛒 Buy with a ⚠ Gap note.

## Feature Map

| Feature name | Keywords | BOM coverage check | Alternatives (if gap) |
|---|---|---|---|
| Water detection / bilge monitoring | bilge, water sensor, water detection, flood, BILGE_SENSOR | Any water/bilge sensor (resistive, float, capacitive) | Resistive probe (e.g., DYIables) — simplest, analog output; Float switch — binary on/off; Capacitive sensor — no exposed contacts, requires calibration |
| GPS positioning | GPS, NMEA, latitude, longitude, u-blox, BN-880, Neo-6M, GPS_RX | Any GPS module with UART output | u-blox M8N (e.g., BN-880) — best accuracy/cost balance; u-blox M9N — higher accuracy, higher cost; Neo-6M — budget option, lower accuracy |
| Compass / heading | compass, heading, HMC5883L, QMC5883L, magnetometer | HMC5883L, QMC5883L, or IMU with magnetometer axis | HMC5883L (often bundled with BN-880 GPS) — common, I²C 0x1E; QMC5883L — cheaper clone; Onboard IMU magnetometer axis (if present) |
| Inertial measurement / heel angle | IMU, accelerometer, gyro, QMI8658, MPU-6050, LSM6, heel, pitch, roll | Any IMU (accelerometer + gyro) | Onboard QMI8658 — free if using Waveshare ESP32-S3 AMOLED board (already on PCB); MPU-6050 — common I²C breakout; LSM6DS3 — lower noise ST Micro part |
| Motor control / propulsion | motor, ESC, propulsion, throttle, brushed, brushless, MOTOR_ESC | ESC + motor pair | Brushed motor + brushed ESC — simpler, lower cost; Brushless motor + brushless ESC — more efficient, higher cost and complexity |
| Servo control | servo, rudder, winch, sail, RUDDER, SAIL_WINCH, PCA9685 | Servo driver (PCA9685) or direct MCU PWM | PCA9685 PWM driver — recommended for 2+ servos, I²C; Direct MCU PWM — simpler for a single servo, uses a GPIO pin |
| Radio control link | ELRS, ExpressLRS, FrSky, Spektrum, CRSF, CRSF_RX | ELRS receiver + transmitter module | ExpressLRS (ELRS) — open-source, long range; FrSky — proprietary, widely supported; Spektrum — proprietary, common in US market |
| Current / power monitoring | current sensor, power monitor, INA, ACS, amperage, INA228, INA219 | INA228, INA219, INA226, or ACS712 | INA228 — 20-bit resolution, I²C, high precision; INA219 — 12-bit, I²C, common breakout; ACS712 — Hall-effect, analog output, simpler but noisier |
| Data logging | logging, SD card, TF card, flash, data storage, SD_CS | SD/TF card module, SPI flash, or WiFi remote logging | Onboard TF card slot — free if using Waveshare ESP32-S3 AMOLED board; External SPI flash — smaller, solderable; WiFi remote logging — no local storage needed |
| Wind sensing / anemometer | wind, anemometer, wind speed, wind direction | Ultrasonic or mechanical anemometer | Ultrasonic — no moving parts, better for marine; Mechanical cup — cheaper, moving parts can jam or corrode |
| Battery voltage monitoring | battery voltage, voltage divider, BAT_ADC, battery monitor | Resistor divider on ADC, or dedicated monitor IC | Resistor divider — already present on Waveshare board (BAT_ADC on GPIO4); INA228/INA219 — also monitors voltage alongside current |
| WiFi web interface | WiFi, web UI, browser, HTTP, websocket, AP, WIFI_AP | ESP32 built-in WiFi | ESP32 built-in WiFi — already available on ESP32-S3, no extra hardware needed |
| Bilge pump control | bilge pump, pump, BILGE_PUMP, MOSFET | N-MOSFET gate driver circuit | N-channel MOSFET (e.g., IRLZ44N) — switches pump from GPIO; Relay module — simpler but bulkier |
