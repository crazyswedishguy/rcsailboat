#pragma once
#include <stddef.h>
#include <stdint.h>

void    elrs_init();
void    elrs_update();
float   elrs_get_channel(int ch);       // normalized value for channel ch (0-based)
bool    elrs_link_ok();
uint8_t elrs_rssi();                    // RSSI in dBm (absolute value, 0 = unknown)
uint8_t elrs_link_quality();            // link quality 0–100 %
void    elrs_send_frame(const uint8_t *frame, size_t len);  // write CRSF frame to TX module
