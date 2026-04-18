#pragma once

#define PROTOCOL_VERSION 1

// CRSF channel indices (0-based for array access).
// The 1-based channel numbers in docs/protocol.md map to these by subtracting 1.
#define CH_RUDDER   0  // -1.0 (port) .. +1.0 (starboard)
#define CH_SAIL     1  // 0.0 (out) .. +1.0 (in)
#define CH_THROTTLE 2  // -1.0 (reverse) .. +1.0 (forward)
#define CH_ARM      3  // 0.0 = disarmed, 1.0 = armed
#define CH_MODE     4  // 0.0 = manual; others reserved
