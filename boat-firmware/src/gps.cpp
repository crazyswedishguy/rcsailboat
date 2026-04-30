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
    while (s_serial.available())
        s_gps.encode(s_serial.read());
}

bool    gps_has_fix()     { return s_gps.location.isValid() && s_gps.location.age() < 2000; }
double  gps_lat()         { return s_gps.location.isValid() ? s_gps.location.lat() : 0.0; }
double  gps_lng()         { return s_gps.location.isValid() ? s_gps.location.lng() : 0.0; }
float   gps_speed_kmh()   { return s_gps.speed.isValid()    ? (float)s_gps.speed.kmph() : 0.0f; }
float   gps_heading_deg() { return s_gps.course.isValid()   ? (float)s_gps.course.deg()  : 0.0f; }
float   gps_altitude_m()  { return s_gps.altitude.isValid() ? (float)s_gps.altitude.meters() : 0.0f; }
uint8_t gps_satellites()  { return s_gps.satellites.isValid() ? (uint8_t)s_gps.satellites.value() : 0; }

#endif  // GPS_ENABLED
