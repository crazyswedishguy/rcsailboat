#pragma once

#define PROTOCOL_VERSION 1

// CRSF channel indices (0-based for array access).
// The 1-based channel numbers in docs/protocol.md map to these by subtracting 1.
#define CH_RUDDER   0  // -1.0 (port) .. +1.0 (starboard)
#define CH_SAIL     1  // 0.0 (out) .. +1.0 (in)
#define CH_THROTTLE 2  // -1.0 (reverse) .. +1.0 (forward)
#define CH_ARM      3  // 0.0 = disarmed, 1.0 = armed
#define CH_MODE     4  // 0.0 = manual; others reserved
#define CH_RESTART  5  // hold at +1.0 for ≥2 s while disarmed → ESP.restart()

// Standard CRSF frame types used by telemetry.cpp.
// Names and values must match the crsf_frame_type_e enum in AlfredoCRSF's
// crsf_protocol.h. Include <crsf_protocol.h> for the full enum; the defines
// below cover only what our firmware emits and avoid redefinition conflicts.
//
// CRSF_FRAMETYPE_GPS (0x02), CRSF_FRAMETYPE_BATTERY_SENSOR (0x08), and
// CRSF_FRAMETYPE_ATTITUDE (0x1E) are defined by the library — use them directly
// in translation units that include <crsf_protocol.h> / <AlfredoCRSF.h>.

// Custom sailboat frame types (not in the CRSF spec; assigned to the vendor range).
#define CRSF_FRAMETYPE_SAILBOAT 0x80  // rudder/sail/ESC commanded positions + ESP32 temp
#define CRSF_FRAMETYPE_DEVICES  0x81  // device status bitmap (10 × 2-bit levels, LSB-first)
