#pragma once

void telemetry_init();
void telemetry_update();

#ifdef GPS_ENABLED
void telemetry_send_gps();  // emit CRSF GPS frame (0x02) with current fix data
#endif
