from dataclasses import dataclass, field


@dataclass
class DesiredState:
    rudder: float = 0.0    # -1.0 (port) .. +1.0 (starboard)
    sail: float = 0.0      # 0.0 (sheeted out) .. +1.0 (sheeted in)
    throttle: float = 0.0  # -1.0 (reverse) .. +1.0 (forward)
    armed: bool = False
