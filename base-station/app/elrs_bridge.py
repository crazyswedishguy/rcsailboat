import asyncio
import logging

from .state import DesiredState

logger = logging.getLogger(__name__)

TRANSMIT_HZ = 50


async def elrs_bridge_task(state: DesiredState) -> None:
    """Stub: will pack desired state into CRSF frames and write to the TX module at 50 Hz."""
    logger.info("ELRS bridge task started (stub — no serial output yet)")
    interval = 1.0 / TRANSMIT_HZ
    while True:
        await asyncio.sleep(interval)
