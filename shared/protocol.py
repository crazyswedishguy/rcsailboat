PROTOCOL_VERSION = 1

# ELRS channel numbers (1-indexed, matching docs/protocol.md)
CH_RUDDER   = 1  # -1.0 (port) .. +1.0 (starboard)
CH_SAIL     = 2  # 0.0 (sheeted out) .. +1.0 (sheeted in)
CH_THROTTLE = 3  # -1.0 (reverse) .. +1.0 (forward)
CH_ARM      = 4  # 0.0 = disarmed, 1.0 = armed
CH_MODE     = 5  # 0.0 = manual; others reserved

# CRSF telemetry frame types emitted by the boat firmware (base station decodes these)
CRSF_FRAMETYPE_GPS      = 0x02  # standard: lat/lng/speed/heading/alt/sats
CRSF_FRAMETYPE_BATTERY  = 0x08  # standard: voltage/current/mAh/remaining
CRSF_FRAMETYPE_ATTITUDE = 0x1E  # standard: pitch/roll/yaw (rad*10000, big-endian int16)
CRSF_FRAMETYPE_SAILBOAT = 0x80  # custom:   rudder/sail/ESC commanded + ESP32 temp
