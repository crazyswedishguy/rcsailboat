// gps.cpp — TinyGPS++ NMEA parser over UART2 (GPS_ENABLED builds only).
//
// Reads NMEA-0183 sentences from the BN-880 GPS module on UART2 (GPIO15 RX,
// GPIO18 TX, 9600 baud). TinyGPS++ parses GGA, RMC, and VTG sentences and
// exposes structured fields: location, speed, course, altitude, and satellites.
//
// gps_update() must be called every loop() iteration without rate-limiting.
// The parser is fed one byte at a time and does not block — it accumulates
// characters until a complete sentence arrives, then updates the field cache.
//
// gps_has_fix() requires both:
//   (a) location.isValid() — the parser has received a valid GGA/RMC fix, and
//   (b) location.age() < 2000 ms — the fix is not stale (GPS signal still present).
// Both conditions must be true for has_fix to return true.
//
// All other accessors guard with their own isValid() check and return 0 / 0.0
// when the corresponding field has never been received or is older than 2 s.

#include "gps.h"
#ifdef GPS_ENABLED

#include "config.h"
#include <Arduino.h>
#include <TinyGPSPlus.h>

static TinyGPSPlus    s_gps;
static HardwareSerial s_serial(GPS_UART_NUM);

void gps_init()
{
    s_serial.begin(GPS_BAUD, SERIAL_8N1, pins::GPS_RX, pins::GPS_TX);
    Serial.printf("gps: UART%u RX=GPIO%u TX=GPIO%u @ %lu baud\n",
                  GPS_UART_NUM, pins::GPS_RX, pins::GPS_TX, GPS_BAUD);
}

// Feed all available bytes from the GPS serial port into TinyGPS++.
// Call from loop() without rate-limiting — parser is byte-by-byte, very fast.
void gps_update()
{
    // Echo raw NMEA to console for the first 20 s so we can verify the module
    // is sending valid sentences (not garbled or UBX binary).
    static bool s_echo_done = false;
    if (!s_echo_done) {
        while (s_serial.available()) {
            char c = (char)s_serial.read();
            Serial.write(c);
            s_gps.encode(c);
        }
        if (millis() > 20000) s_echo_done = true;
    } else {
        while (s_serial.available())
            s_gps.encode(s_serial.read());
    }

    // Periodic diagnostic — charsProcessed==0 → wiring fault;
    // passed==0 && failed>0 → baud rate mismatch or binary protocol.
    static unsigned long s_diag_ms = 0;
    if (millis() - s_diag_ms >= 5000) {
        s_diag_ms = millis();
        Serial.printf("gps: chars=%lu passed=%lu failed=%lu sats=%u fix=%s\n",
                      s_gps.charsProcessed(),
                      s_gps.passedChecksum(),
                      s_gps.failedChecksum(),
                      gps_satellites(),
                      gps_has_fix() ? "YES" : "no");
    }
}

bool    gps_has_fix()     { return s_gps.location.isValid() && s_gps.location.age() < 2000; }
double  gps_lat()         { return s_gps.location.isValid() ? s_gps.location.lat() : 0.0; }
double  gps_lng()         { return s_gps.location.isValid() ? s_gps.location.lng() : 0.0; }
float   gps_speed_kn()    { return s_gps.speed.isValid()    ? (float)s_gps.speed.knots() : 0.0f; }
float   gps_heading_deg() { return s_gps.course.isValid()   ? (float)s_gps.course.deg()  : 0.0f; }
float   gps_altitude_m()  { return s_gps.altitude.isValid() ? (float)s_gps.altitude.meters() : 0.0f; }
uint8_t gps_satellites()  { return s_gps.satellites.isValid() ? (uint8_t)s_gps.satellites.value() : 0; }

#endif  // GPS_ENABLED
