import asyncio
import logging
import struct
from collections.abc import Callable

from .state import DesiredState, GpsPosition

logger = logging.getLogger(__name__)

TRANSMIT_HZ = 50

# CRSF frame types handled on the telemetry (boat → Pi) path
CRSF_FRAMETYPE_GPS = 0x02


async def elrs_bridge_task(
    state: DesiredState,
    gps: GpsPosition,
    on_gps_update: Callable[[], None] | None = None,
) -> None:
    """Stub: will pack desired state into CRSF frames and write to the TX module at 50 Hz.

    Also decodes incoming CRSF telemetry frames (battery, GPS, attitude) and
    updates the shared state objects.  When a GPS frame is decoded and
    on_gps_update is provided, the callback is called so callers can broadcast
    the new position to WebSocket clients.
    """
    logger.info("ELRS bridge task started (stub — no serial output yet)")
    interval = 1.0 / TRANSMIT_HZ
    while True:
        await asyncio.sleep(interval)


def _decode_gps_frame(payload: bytes, gps: GpsPosition) -> bool:
    """Parse a CRSF GPS telemetry frame (type 0x02) into gps in-place.

    CRSF GPS payload layout — 15 bytes, big-endian:
      int32   latitude        degrees × 1e7
      int32   longitude       degrees × 1e7
      uint16  groundspeed     km/h × 10
      uint16  gps_heading     degrees × 100
      uint16  altitude        metres + 1000 offset (so 1000 = 0 m MSL)
      uint8   satellites_in_use

    Returns True if the payload was long enough and parsed successfully.
    """
    if len(payload) < 15:
        return False
    lat_raw, lng_raw, spd_raw, hdg_raw, alt_raw, sats = struct.unpack_from(
        ">iiHHHB", payload
    )
    gps.lat = lat_raw / 1e7
    gps.lng = lng_raw / 1e7
    gps.speed_kmh = spd_raw / 10.0
    gps.heading_deg = hdg_raw / 100.0
    gps.altitude_m = alt_raw - 1000.0
    gps.satellites = sats
    gps.has_fix = sats >= 3
    return True
