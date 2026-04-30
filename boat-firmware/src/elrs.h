// elrs.h — ELRS receiver interface (CRSF over UART1, 420 000 baud).
//
// The boat's ELRS receiver connects to UART1 (GPIO16 RX / GPIO17 TX).
// Two paths run through this module:
//
//   RX (boat ← transmitter): incoming CRSF frames carry RC channel values
//   and link statistics. Parsed in elrs_update(); Phase 3 stub returns
//   safe defaults (0.0 / false) until that work is done.
//
//   TX (boat → transmitter): CRSF telemetry frames are written raw to
//   Serial1 by elrs_send_frame(). This direction works even before the
//   RX path is implemented, because Serial1.write() is fire-and-forget.
//
// Channel values returned by elrs_get_channel() are normalized to –1.0..+1.0
// (or 0.0..+1.0 for unidirectional channels). See protocol.h for the index
// constants (CH_RUDDER, CH_SAIL, etc.).

#pragma once
#include <stddef.h>
#include <stdint.h>

// Initialise UART1 for CRSF. Call once from setup().
// Phase 3 stub: currently a no-op — UART1 is not yet opened.
void    elrs_init();

// Parse any waiting CRSF bytes from Serial1. Call every loop() iteration.
// Updates the channel array and link statistics. Phase 3 stub: no-op.
void    elrs_update();

// Return the normalised value for RC channel ch (0-based, see protocol.h).
// Range: –1.0..+1.0 for bidirectional channels; 0.0..+1.0 for unipolar.
// Returns 0.0 in the Phase 3 stub.
float   elrs_get_channel(int ch);

// True when CRSF frames have been received within the last 500 ms.
// Always false in the Phase 3 stub.
bool    elrs_link_ok();

// Most recent RSSI in dBm (absolute value). 0 = unknown.
uint8_t elrs_rssi();

// Link quality as a percentage 0–100. Derived from LINK_STATISTICS (0x14) frames.
uint8_t elrs_link_quality();

// Write a pre-built CRSF frame to the ELRS TX module.
// Safe to call before elrs_init() — Serial1.write() is a no-op on an uninitialised port.
void    elrs_send_frame(const uint8_t *frame, size_t len);
