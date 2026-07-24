// elrs.h — SX1262 LoRa radio interface for the boat.
//
// Replaces the previous AlfredoCRSF / ELRS-receiver driver.  The radio link
// is now a direct SX1262 → SX1262 LoRa link between the XIAO (base station)
// and this ESP32-S3 (boat).
//
// Wire layout (software SPI — both hardware SPI buses are occupied):
//   SPI2/FSPI = CO5300 AMOLED display (QSPI)
//   SPI3/HSPI = TF card
//   SX1262    = software bit-bang on GPIO5 CLK / GPIO6 MOSI / GPIO7 MISO
//               CS=GPIO8, RESET=GPIO1, DIO1=GPIO42, BUSY=GPIO45
//               RF switch: RXEN=GPIO16, TXEN=GPIO17
//
// Protocol over the air: raw CRSF frames.  The XIAO transmits
// RC_CHANNELS_PACKED (type 0x16) frames at 50 Hz; the boat transmits CRSF
// telemetry frames back.  Frame format is identical to the old UART-based
// CRSF path, so all frame-building code in telemetry.cpp is unchanged.
//
// Public API is unchanged from the old ELRS / UART driver so main.cpp
// requires no edits.

#pragma once
#include <stddef.h>
#include <stdint.h>

// Initialise SX1262 and start listening.  Call once from setup().
void    elrs_init();

// Poll for received packets and run the CH_RESTART hold-to-reboot trigger.
// Must be called every loop() iteration.
void    elrs_update();

// Return the normalised value for RC channel ch (0-based, see protocol.h).
//   CH_RUDDER, CH_THROTTLE, CH_ARM, CH_MODE → –1.0 .. +1.0 (bipolar)
//   CH_SAIL                                 →  0.0 .. +1.0 (unipolar)
float   elrs_get_channel(int ch);

// True when a valid LoRa frame was received within the last 500 ms.
bool    elrs_link_ok();

// Last uplink RSSI in dBm (absolute value, always positive).  0 = unknown.
uint8_t elrs_rssi();

// Estimated uplink link quality 0–100 %.
// Derived from recent packet-received rate relative to the expected 50 Hz.
uint8_t elrs_link_quality();

// Queue a pre-built CRSF frame for transmission on the next TX opportunity.
// Called by telemetry.cpp; the frame is sent after the next RC frame arrives.
void    elrs_send_frame(const uint8_t *frame, size_t len);
