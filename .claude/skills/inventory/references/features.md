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
| GPS positioning | GPS, NMEA, latitude, longitude, u-blox, BN-880, Neo-6M, GPS_RX | Any GPS module with UART or I²C output | u-blox M8N (e.g., BN-880) — best accuracy/cost balance; u-blox M9N — higher accuracy, higher cost; Neo-6M — budget option, lower accuracy |
| Compass / heading | compass, heading, HMC5883L, QMC5883L, magnetometer, MAG_INT | HMC5883L, QMC5883L, or IMU with magnetometer axis | HMC5883L — common, I²C; QMC5883L — cheaper clone; Onboard IMU magnetometer axis (if present) |
| Inertial measurement | IMU, accelerometer, gyro, QMI8658, MPU-6050, LSM6, MPU6050, ICM42, pitch, roll, tilt, heel | Any 6-axis or 9-axis IMU | MPU-6050 — common I²C breakout, well-documented; QMI8658 — lower noise, on Waveshare ESP32-S3 AMOLED board; LSM6DS3 — ST Micro, lower noise; ICM-42688-P — high performance |
| Motor control | motor, ESC, propulsion, throttle, brushed, brushless, MOTOR, L298N, DRV8833, DRV8825, A4988 | ESC, H-bridge, or motor driver + motor | Brushed motor + brushed ESC or H-bridge (L298N, DRV8833) — simpler, lower cost; Brushless motor + brushless ESC — more efficient, higher cost; Stepper motor + stepper driver (A4988, DRV8825) — precise position control |
| Servo control | servo, SERVO, PCA9685, rudder, winch, pan, tilt, PWM_SERVO | Servo driver (PCA9685) or direct MCU PWM | PCA9685 PWM driver — recommended for 2+ servos, I²C; Direct MCU PWM — simpler for a single servo |
| Display | display, OLED, LCD, TFT, e-paper, screen, SSD1306, SH1106, ST7735, ILI9341, CO5300, DISPLAY_CS | OLED, LCD, TFT, or e-paper display module | SSD1306 OLED (128×64, I²C) — common, cheap, good for status text; ILI9341 TFT (240×320, SPI) — colour, good for dashboards; e-paper — ultra-low power, readable in direct sunlight |
| Wireless / Bluetooth | Bluetooth, BLE, nRF24, HC-05, HC-06, BT_TX, BT_RX, radio, 2.4GHz, 433MHz | BLE module, classic BT module, or nRF24L01 | ESP32 built-in BLE — no extra hardware if MCU is ESP32; nRF24L01+ — low power 2.4 GHz, up to 1 km with PA/LNA; HC-05/HC-06 — classic Bluetooth UART bridge, simple to use |
| Temperature / humidity sensing | temperature, humidity, DHT, BME280, DHT22, DHT11, DS18B20, SHT, TEMP, TEMP_PIN | Temperature or humidity sensor | DHT22 — combined temp+humidity, single-wire, ±0.5°C; BME280 — combined temp+humidity+pressure, I²C/SPI, higher precision; DS18B20 — waterproof probe, single-wire, ±0.5°C, good for liquids |
| Distance / proximity sensing | distance, proximity, ultrasonic, HC-SR04, VL53, TOF, IR sensor, TRIG, ECHO, TRIG_PIN, ECHO_PIN | Ultrasonic, IR, or ToF sensor | HC-SR04 — ultrasonic, 2–400 cm, simple 2-pin trigger/echo; VL53L0X — laser ToF, I²C, ±3% accuracy; IR proximity (TCRT5000 etc.) — short range only, affected by colour and ambient light |
| Relay / load switching | relay, solenoid, valve, RELAY, RELAY_PIN, MOSFET, load switch, pump, PUMP | Relay module, MOSFET, or solid-state relay | Relay module — mechanical, handles AC and DC loads, audible click; N-channel MOSFET (e.g. IRLZ44N) — solid-state, fast switching, DC only; Solid-state relay (SSR) — no mechanical wear, silent, higher current AC/DC |
| Moisture / water detection | moisture, water, wet, flood, rain, rain sensor, MOISTURE, WATER_SENSOR, water level | Resistive, capacitive, or float moisture sensor | Resistive probe — simplest, analog output, corrodes slowly over time; Capacitive sensor — no exposed contacts, longer lifespan, more stable; Float switch — binary on/off, mechanically robust |
| Current / power monitoring | current sensor, power monitor, INA, ACS, watt, amperage, INA228, INA219, INA226, ACS712 | INA228, INA219, INA226, or ACS712 | INA228 — 20-bit resolution, I²C, high precision; INA219 — 12-bit, I²C, common breakout; ACS712 — Hall-effect, analog output, simpler but noisier |
| Data logging | logging, SD card, SD_CS, TF card, flash, SPIFFS, LittleFS, data storage, logger | SD/TF card module, SPI flash, or internal flash | SD/TF card — removable, large capacity, CSV logging; SPI flash chip — soldered, smaller, no card slot needed; Internal flash (SPIFFS/LittleFS) — no extra hardware, limited write cycles |
| Battery voltage monitoring | battery voltage, voltage divider, BAT_ADC, VBAT, battery monitor, cell voltage | Resistor divider on ADC pin, or dedicated monitor IC | Resistor divider + ADC — zero extra hardware; INA228/INA219 — monitors current alongside voltage |
| WiFi web interface | WiFi, web UI, browser, HTTP, websocket, AP, WIFI_AP, webserver, HTML | ESP32/ESP8266 built-in WiFi | ESP32 built-in WiFi — no extra hardware if MCU is ESP32 or ESP8266; ESP8266 add-on (Wemos D1 mini etc.) — if MCU lacks WiFi |
| Button / encoder input | button, pushbutton, encoder, BUTTON, BTN_PIN, rotary, switch, tactile, KEY | Pushbutton, toggle switch, or rotary encoder | Pushbutton — simplest digital input, debounce in firmware; Rotary encoder — position-sensitive input without end stops; Toggle switch — latching on/off |
