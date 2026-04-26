#include "elrs.h"
#include <Arduino.h>

static uint8_t s_rssi = 0;
static uint8_t s_lq   = 0;

void elrs_init() {}

void elrs_update() {
    // Phase 3: parse incoming CRSF frames from Serial1.
    // Parse LINK_STATISTICS (0x14) here to update s_rssi and s_lq.
}

float   elrs_get_channel(int ch)  { (void)ch; return 0.0f; }
bool    elrs_link_ok()            { return false; }
uint8_t elrs_rssi()               { return s_rssi; }
uint8_t elrs_link_quality()       { return s_lq; }

void elrs_send_frame(const uint8_t *frame, size_t len) {
    // Serial1 is initialized in Phase 3 (elrs_init). Safe to call before then —
    // Serial1.write() on an uninitialised port is a no-op on ESP32 Arduino.
    Serial1.write(frame, len);
}
