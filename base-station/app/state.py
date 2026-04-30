import math
from dataclasses import dataclass, field


@dataclass
class DesiredState:
    rudder: float = 0.0    # -1.0 (port) .. +1.0 (starboard)
    sail: float = 0.0      # 0.0 (sheeted out) .. +1.0 (sheeted in)
    throttle: float = 0.0  # -1.0 (reverse) .. +1.0 (forward)
    armed: bool = False

    def apply(self, rudder: float, sail: float, throttle: float, armed: bool) -> None:
        self.rudder = _clamp(rudder, -1.0, 1.0)
        self.sail = _clamp(sail, 0.0, 1.0)
        self.throttle = _clamp(throttle, -1.0, 1.0)
        self.armed = armed

    def stop(self) -> None:
        self.throttle = 0.0
        self.armed = False


@dataclass
class GpsPosition:
    lat: float = 0.0
    lng: float = 0.0
    speed_kmh: float = 0.0
    heading_deg: float = 0.0
    altitude_m: float = 0.0
    satellites: int = 0
    has_fix: bool = False


@dataclass
class TelemetryState:
    # Battery (CRSF 0x08) — 2 Hz from boat
    voltage_v: float = 0.0
    current_a: float = 0.0
    mah_used: float = 0.0
    battery_pct: int = 0
    # Attitude (CRSF 0x1E) — 5 Hz from boat
    pitch_deg: float = 0.0
    roll_deg: float = 0.0
    yaw_deg: float = 0.0
    # Sailboat custom (CRSF 0x80) — 5 Hz from boat
    rudder: float = 0.0
    sail: float = 0.0
    throttle: float = 0.0
    mcu_temp_c: float = 0.0
    # Bridge connection status
    bridge_connected: bool = False


def _clamp(value: float, lo: float, hi: float) -> float:
    if math.isnan(value) or math.isinf(value):
        return 0.0
    return max(lo, min(hi, value))
