PROTOCOL_VERSION = 3

# ELRS channel numbers (1-indexed, matching docs/protocol.md).
# NOTE: the boat firmware (boat-firmware/src/protocol.h) uses 0-based indices
# for array access — subtract 1 to convert. The Pi-side packer in
# base-station/app/elrs_bridge.py works in 0-based channel array indices.
CH_RUDDER   = 1  # -1.0 (port) .. +1.0 (starboard)
CH_SAIL     = 2  # 0.0 (sheeted out) .. +1.0 (sheeted in)
CH_THROTTLE = 3  # -1.0 (reverse) .. +1.0 (forward)
CH_ARM      = 4  # 0.0 = disarmed, 1.0 = armed
CH_MODE     = 5  # >0.5 (held >=300ms) = remote request for boat ELRS control
                  # mode (equivalent to the boat's touchscreen "Enable ELRS"
                  # button); <0.5 (held >=3000ms) releases back to WiFi mode --
                  # asymmetric debounce, quick to enter / slow to leave, since
                  # leaving tears down/restarts the boat's WiFi AP. Not yet
                  # sent by elrs_bridge.py -- see docs/protocol.md.
CH_RESTART  = 6  # hold at +1.0 for >=2 s while disarmed -> ESP.restart()
CH_PUMP     = 7  # 0.0 = off, 1.0 = on -- manual bilge pump override (not arm-gated)

# CRSF telemetry frame types emitted by the boat firmware (base station decodes these)
CRSF_FRAMETYPE_GPS       = 0x02  # standard: lat/lng/speed/heading/alt/sats
CRSF_FRAMETYPE_BATTERY   = 0x08  # standard: voltage/current/mAh/remaining
CRSF_FRAMETYPE_LINK_STATS = 0x14  # standard: RSSI/LQ/SNR/TX power
CRSF_FRAMETYPE_ATTITUDE  = 0x1E  # standard: pitch/roll/yaw (rad*10000, big-endian int16)
CRSF_FRAMETYPE_SAILBOAT  = 0x80  # custom:   rudder/sail/ESC commanded + ESP32 temp + status byte
CRSF_FRAMETYPE_DEVICES   = 0x81  # custom:   device-status bitmap (10 x 2-bit levels, LSB-first)

# SAILBOAT (0x80) status byte bit positions (see docs/protocol.md)
SB_STATUS_CAPSIZED  = 0
SB_STATUS_BILGE_WET = 1
SB_STATUS_PUMP      = 2
SB_STATUS_ARMED     = 3
SB_STATUS_FAILSAFE  = 4
