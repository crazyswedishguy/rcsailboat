// elrs.h — ELRS receiver interface (CRSF over UART1, 420 000 baud).
//
// The boat's ELRS receiver connects to UART1 (GPIO16 RX / GPIO17 TX).
//
//   RX (boat ← transmitter): AlfredoCRSF parses incoming CRSF frames and
//   delivers RC channel values via callbacks. elrs_update() must be called
//   every loop() iteration.
//
//   TX (boat → transmitter): CRSF telemetry frames are written raw to
//   Serial1 by elrs_send_frame().
//
// Channel values returned by elrs_get_channel() are normalized:
//   –1.0..+1.0  for bidirectional channels (rudder, throttle, arm, mode)
//    0.0..+1.0  for unipolar channels (sail trim, CH_SAIL)
// See protocol.h for channel index constants (CH_RUDDER, CH_SAIL, …).

#pragma once
#include <stddef.h>
#include <stdint.h>

// Initialise UART1 and AlfredoCRSF. Call once from setup().
void    elrs_init();

// Parse any waiting CRSF bytes. Call every loop() iteration.
// Also handles the CH_RESTART hold-to-reboot trigger.
void    elrs_update();

// Return the normalised value for RC channel ch (0-based, see protocol.h).
float   elrs_get_channel(int ch);

// True when CRSF frames have been received within the last 500 ms.
bool    elrs_link_ok();

// Most recent uplink RSSI in dBm (absolute value). 0 = unknown.
uint8_t elrs_rssi();

// Uplink link quality 0–100 %. Derived from LINK_STATISTICS (0x14) frames.
uint8_t elrs_link_quality();

// Write a pre-built CRSF frame to the ELRS TX module.
void    elrs_send_frame(const uint8_t *frame, size_t len);
