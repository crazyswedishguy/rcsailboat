PROTOCOL_VERSION = 1

# ELRS channel numbers (1-indexed, matching docs/protocol.md)
CH_RUDDER   = 1  # -1.0 (port) .. +1.0 (starboard)
CH_SAIL     = 2  # 0.0 (sheeted out) .. +1.0 (sheeted in)
CH_THROTTLE = 3  # -1.0 (reverse) .. +1.0 (forward)
CH_ARM      = 4  # 0.0 = disarmed, 1.0 = armed
CH_MODE     = 5  # 0.0 = manual; others reserved
