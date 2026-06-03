"""
state.py — shared mutable state dataclasses for the base-station app.

Three dataclasses are defined here and passed by reference between the FastAPI
request handlers and the ELRS bridge background task:

  DesiredState    — what the user wants the boat to do (set by WebSocket messages,
                    read by the ELRS bridge to build outgoing CRSF RC frames).

  GpsPosition     — latest GPS fix decoded from incoming CRSF GPS frames (0x02).
                    Written by the ELRS bridge, read by the WebSocket broadcaster.

  TelemetryState  — all other telemetry decoded from the boat (battery, attitude,
                    sailboat custom frame, bridge connection status).
                    Written by the ELRS bridge, read by the WebSocket broadcaster.

All three classes are plain dataclasses — no locking. The asyncio event loop is
single-threaded, so concurrent access between coroutines is safe without locks.
"""

import math
from dataclasses import dataclass, field


@dataclass
class DesiredState:
    """
    Command values sent to the boat via CRSF RC Channels Packed frames at 50 Hz.

    All values are normalised floats:
      rudder    — –1.0 (full port) .. +1.0 (full starboard)
      sail      —  0.0 (fully eased / sheeted out) .. +1.0 (fully trimmed in)
      throttle  — –1.0 (full reverse) .. +1.0 (full forward)
      armed     —  False = boat is disarmed (throttle forced to 0 in CRSF frame)
    """

    rudder: float = 0.0    # -1.0 (port) .. +1.0 (starboard)
    sail: float = 0.0      # 0.0 (sheeted out) .. +1.0 (sheeted in)
    throttle: float = 0.0  # -1.0 (reverse) .. +1.0 (forward)
    # armed is set by main.py based on whether an active controller is connected,
    # not by browser messages.  The ELRS bridge reads it to drive CH_ARM.
    armed: bool = False

    def apply(self, rudder: float, sail: float, throttle: float) -> None:
        """Update control values with range-clamped inputs. NaN and Inf map to 0."""
        self.rudder = _clamp(rudder, -1.0, 1.0)
        self.sail = _clamp(sail, 0.0, 1.0)
        self.throttle = _clamp(throttle, -1.0, 1.0)

    def disarm(self) -> None:
        """Clear armed flag and zero throttle — used when control is released."""
        self.throttle = 0.0
        self.armed = False


@dataclass
class GpsPosition:
    """
    Latest GPS fix data decoded from CRSF GPS frames (type 0x02, 1 Hz from boat).

    has_fix is True when the boat's GPS module reports a valid location fix
    with at least 3 satellites. Consumers should check has_fix before trusting
    lat/lng — the other fields may contain stale values when fix is lost.
    """

    lat: float = 0.0          # decimal degrees, WGS-84 (positive = north)
    lng: float = 0.0          # decimal degrees, WGS-84 (positive = east)
    speed_kmh: float = 0.0    # ground speed in km/h
    heading_deg: float = 0.0  # course over ground, degrees (0 = north, clockwise)
    altitude_m: float = 0.0   # metres above mean sea level
    satellites: int = 0       # number of satellites used in fix
    has_fix: bool = False      # True when location is valid and < 2 s old
    last_updated_at: float = 0.0  # time.monotonic() of last decoded GPS frame; 0 = never received


@dataclass
class TelemetryState:
    """
    All non-GPS telemetry decoded from incoming CRSF frames.

    Updated by the ELRS bridge at the rates the boat transmits:
      Battery (CRSF 0x08)  — 2 Hz: voltage, current, mAh, remaining %
      Attitude (CRSF 0x1E) — 5 Hz: pitch, roll, yaw (converted from rad to deg)
      Sailboat (CRSF 0x80) — 5 Hz: commanded servo positions + MCU temperature

    bridge_connected is True when the serial port to the ELRS TX module is open
    and receiving data. It goes False on disconnect and is restored on reconnect.
    """

    # Battery sensor (CRSF 0x08) — 2 Hz from boat
    voltage_v: float = 0.0     # bus voltage in volts
    current_a: float = 0.0     # instantaneous current draw in amps
    mah_used: float = 0.0      # milliamp-hours consumed since boat power-on
    battery_pct: int = 0       # estimated remaining capacity 0–100%

    # Attitude (CRSF 0x1E) — 5 Hz from boat
    pitch_deg: float = 0.0     # pitch angle in degrees (positive = bow up)
    roll_deg: float = 0.0      # heel angle in degrees (positive = starboard)
    yaw_deg: float = 0.0       # heading in degrees (from compass if fitted, else 0)

    # Sailboat custom frame (CRSF 0x80) — 5 Hz from boat
    rudder: float = 0.0        # commanded rudder position –1.0..+1.0
    sail: float = 0.0          # commanded sail trim 0.0..+1.0
    throttle: float = 0.0      # commanded throttle –1.0..+1.0
    mcu_temp_c: float = 0.0    # ESP32 internal die temperature in °C

    # Link statistics (CRSF 0x14) — sent by the ELRS TX module itself, variable rate
    rssi_uplink: int = 0        # uplink RSSI in dBm (negative, e.g. -85); 0 = unknown
    lq_uplink: int = 0          # uplink link quality 0–100 %
    snr_uplink: int = 0         # uplink SNR in dB (signed)
    rssi_downlink: int = 0      # downlink RSSI in dBm (negative); 0 = unknown
    lq_downlink: int = 0        # downlink link quality 0–100 %
    snr_downlink: int = 0       # downlink SNR in dB (signed)
    tx_power_mw: int = 0        # uplink TX power in mW (decoded from ELRS enum)

    # Timestamps — time.monotonic() of last successfully decoded frame; 0.0 = never received.
    # Use these to detect stale data: if (time.monotonic() - telem.last_updated_at) > threshold
    last_updated_at: float = 0.0

    # Device status (CRSF 0x81) — 0.2 Hz from boat
    # Maps ESP32 device id → level: 'absent' | 'ok' | 'warn' | 'error'
    # Keys: 'ft', 'qmi', 'pca', 'ina', 'sd', 'bilge', 'pump', 'rudder', 'winch', 'esc'
    # Bilge special: 'warn'=WET, 'error'=unverified (never triggered this session)
    device_status: dict[str, str] = field(default_factory=dict)

    # Bridge connection status
    bridge_connected: bool = False  # True when serial port to ELRS TX module is open


def _clamp(value: float, lo: float, hi: float) -> float:
    """Clamp value to [lo, hi]. Returns 0.0 for NaN or Inf inputs."""
    if math.isnan(value) or math.isinf(value):
        return 0.0
    return max(lo, min(hi, value))
