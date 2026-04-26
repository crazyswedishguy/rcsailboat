#include "telemetry.h"
#ifdef GPS_ENABLED
#include "gps.h"
#endif

void telemetry_init()   {}
void telemetry_update() {}

#ifdef GPS_ENABLED
void telemetry_send_gps() {
    // Phase 6: pack gps_lat(), gps_lng(), gps_speed_kmh(), gps_heading_deg(),
    // gps_altitude_m(), gps_satellites() into a CRSF GPS frame (type 0x02) and
    // pass it to the ELRS stack for uplink to the base station.
}
#endif
