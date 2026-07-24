#pragma once

#define PROTOCOL_VERSION 4

// CRSF channel indices (0-based for array access).
// The 1-based channel numbers in docs/protocol.md map to these by subtracting 1.
#define CH_RUDDER   0  // -1.0 (port) .. +1.0 (starboard)
#define CH_SAIL     1  // 0.0 (out) .. +1.0 (in)
#define CH_THROTTLE 2  // -1.0 (reverse) .. +1.0 (forward)
#define CH_ARM      3  // 0.0 = disarmed, 1.0 = armed
#define CH_MODE     4  // >0.5 (held >=300ms) = remote request for ELRS control
                       // mode, equivalent to the touchscreen "Enable ELRS"
                       // button; <0.5 (held >=3000ms) releases back to WiFi
                       // mode. Asymmetric debounce: quick to enter, slow to
                       // leave, since leaving tears down/restarts the boat's
                       // WiFi AP. See docs/protocol.md "Remote mode switching".
#define CH_RESTART  5  // hold at +1.0 for ≥2 s while disarmed → ESP.restart()
#define CH_PUMP     6  // 0.0 = off, 1.0 = on — manual bilge pump override (not arm-gated)

// SAILBOAT (0x80) status byte bit positions (see docs/protocol.md).
#define SB_STATUS_CAPSIZED  0
#define SB_STATUS_BILGE_WET 1
#define SB_STATUS_PUMP      2
#define SB_STATUS_ARMED     3
#define SB_STATUS_FAILSAFE  4

// CRSF frame type bytes (subset used by this firmware).
// Source: CRSF WG spec v5; custom types 0x80–0x81 are vendor-assigned.
#define CRSF_FRAMETYPE_GPS            0x02  // GPS position
#define CRSF_FRAMETYPE_BATTERY_SENSOR 0x08  // battery voltage / current / mAh
#define CRSF_FRAMETYPE_LINK_STATS     0x14  // uplink/downlink signal quality
#define CRSF_FRAMETYPE_RC_CHANNELS    0x16  // 16-channel RC input (packed 11-bit)
#define CRSF_FRAMETYPE_ATTITUDE       0x1E  // pitch / roll / yaw (int16 × 10000 rad)

// Custom sailboat frame types (vendor-assigned range 0x80–0xFF).
#define CRSF_FRAMETYPE_SAILBOAT 0x80  // rudder/sail/ESC commanded positions + ESP32 temp
#define CRSF_FRAMETYPE_DEVICES  0x81  // device status bitmap (10 × 2-bit levels, LSB-first)
