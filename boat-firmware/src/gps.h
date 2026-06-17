#pragma once
#ifdef GPS_ENABLED

// GPS module interface — NMEA over UART2, parsed by TinyGPS++.
// Full implementation deferred to Phase 6; all functions return safe defaults until then.

#include <stdint.h>

void    gps_init();           // init UART2 at 9600 baud, start TinyGPS++ parser
void    gps_update();         // call each loop() — feeds chars from Serial2 into the parser

bool    gps_has_fix();        // true once NMEA reports a valid 3D fix
double  gps_lat();            // degrees, WGS-84 (positive = north)
double  gps_lng();            // degrees, WGS-84 (positive = east)
float   gps_speed_kn();       // ground speed in knots
float   gps_heading_deg();    // course over ground in degrees (0 = north, CW)
float   gps_altitude_m();     // metres above mean sea level
uint8_t gps_satellites();     // number of satellites used in the fix

#endif  // GPS_ENABLED
