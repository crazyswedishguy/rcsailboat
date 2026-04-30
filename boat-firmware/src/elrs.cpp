// elrs.cpp — ELRS / CRSF interface implementation.
//
// Phase 3 stub: the receive path (UART1 open, frame parsing, channel decode)
// is not yet implemented. All getter functions return safe defaults so the
// rest of the firmware compiles and runs in WiFi mode without ELRS hardware.
//
// What to implement in Phase 3:
//   elrs_init()   — Serial1.begin(CRSF_BAUD, SERIAL_8N1, pins::CRSF_RX, pins::CRSF_TX)
//   elrs_update() — read available bytes into a ring buffer, scan for 0xC8 sync,
//                   extract type + payload, verify CRC-8/DVB-S2 (polynomial 0xD5),
//                   call decode functions for type 0x16 (RC channels) and 0x14 (link stats)
//
// The TX direction (telemetry) is already functional: elrs_send_frame() writes
// raw bytes to Serial1. Serial1.write() silently drops bytes if Serial1 has not
// been opened yet, so telemetry.cpp can safely call it before Phase 3 is done.
//
// CRSF RC Channels Packed (0x16) decode: 16 × 11-bit values packed LSB-first into
// 22 bytes. Raw range 172–1811, centre 992. Normalise to –1.0..+1.0 as:
//   float v = ((raw - 992) / (float)(1811 - 992));   // bipolar
//   float v = ((raw - 172) / (float)(1811 - 172));   // unipolar (sail)

#include "elrs.h"
#include <Arduino.h>

// Link statistics — updated from CRSF LINK_STATISTICS frames (type 0x14).
static uint8_t s_rssi = 0;  // RSSI in dBm (absolute value)
static uint8_t s_lq   = 0;  // Link quality 0–100%

void elrs_init() {
    // Phase 3: open UART1 here.
    // Serial1.begin(CRSF_BAUD, SERIAL_8N1, pins::CRSF_RX, pins::CRSF_TX);
}

void elrs_update() {
    // Phase 3: read bytes from Serial1 into a frame buffer, decode CRSF frames.
    // Parse LINK_STATISTICS (0x14) here to update s_rssi and s_lq.
    // Parse RC_CHANNELS_PACKED (0x16) to update the channel array.
}

// Phase 3 stub — returns 0.0 (neutral / disarmed) for all channels.
float   elrs_get_channel(int ch)  { (void)ch; return 0.0f; }

// Phase 3 stub — always false; failsafe_update() treats link as lost.
bool    elrs_link_ok()            { return false; }

uint8_t elrs_rssi()               { return s_rssi; }
uint8_t elrs_link_quality()       { return s_lq; }

void elrs_send_frame(const uint8_t *frame, size_t len) {
    // Serial1 opened in Phase 3. Writing before then is a safe no-op on ESP32 Arduino.
    Serial1.write(frame, len);
}
