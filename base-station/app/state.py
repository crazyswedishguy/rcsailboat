import math
from dataclasses import dataclass


@dataclass
class DesiredState:
    rudder: float = 0.0    # -1.0 (port) .. +1.0 (starboard)
    sail: float = 0.0      # 0.0 (sheeted out) .. +1.0 (sheeted in)
    throttle: float = 0.0  # -1.0 (reverse) .. +1.0 (forward)
    armed: bool = False

    def apply(self, rudder: float, sail: float, throttle: float, armed: bool) -> None:
        """Validate and apply incoming control values in-place."""
        self.rudder = _clamp(rudder, -1.0, 1.0)
        self.sail = _clamp(sail, 0.0, 1.0)
        self.throttle = _clamp(throttle, -1.0, 1.0)
        self.armed = armed

    def stop(self) -> None:
        self.throttle = 0.0
        self.armed = False


@dataclass
class GpsPosition:
    lat: float = 0.0            # degrees, WGS-84 (positive = north)
    lng: float = 0.0            # degrees, WGS-84 (positive = east)
    speed_kmh: float = 0.0      # ground speed in km/h
    heading_deg: float = 0.0    # course over ground, 0 = north, clockwise
    altitude_m: float = 0.0     # metres above mean sea level
    satellites: int = 0         # number of satellites used in the fix
    has_fix: bool = False        # true once a valid 3D fix is acquired


def _clamp(value: float, lo: float, hi: float) -> float:
    if math.isnan(value) or math.isinf(value):
        return 0.0
    return max(lo, min(hi, value))
