#include "gps.h"
#ifdef GPS_ENABLED

// Phase 6: replace these stubs with TinyGPS++ parsing on Serial2 (PIN_GPS_RX / PIN_GPS_TX).

#include "config.h"

void    gps_init()        {}
void    gps_update()      {}
bool    gps_has_fix()     { return false; }
double  gps_lat()         { return 0.0; }
double  gps_lng()         { return 0.0; }
float   gps_speed_kmh()   { return 0.0f; }
float   gps_heading_deg() { return 0.0f; }
float   gps_altitude_m()  { return 0.0f; }
uint8_t gps_satellites()  { return 0; }

#endif  // GPS_ENABLED
