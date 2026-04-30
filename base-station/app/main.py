import asyncio
import json
import logging
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles

from .elrs_bridge import elrs_bridge_task
from .state import DesiredState, GpsPosition, TelemetryState

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

_STATIC = Path(__file__).parent.parent / "static"
_TILES  = _STATIC / "tiles"
_TILES.mkdir(parents=True, exist_ok=True)

_state = DesiredState()
_gps   = GpsPosition()
_telem = TelemetryState()

_connections: set[WebSocket] = set()


async def _broadcast(msg: dict) -> None:
    """Send a JSON message to every connected browser client."""
    dead: set[WebSocket] = set()
    for ws in _connections:
        try:
            await ws.send_json(msg)
        except Exception:
            dead.add(ws)
    _connections.difference_update(dead)


def _telemetry_payload() -> dict:
    """Build the full telemetry dict that gets broadcast to browsers."""
    return {
        "type": "telemetry",
        "bridge": _telem.bridge_connected,
        "gps": {
            "lat":         _gps.lat,
            "lng":         _gps.lng,
            "speed_kmh":   _gps.speed_kmh,
            "heading_deg": _gps.heading_deg,
            "altitude_m":  _gps.altitude_m,
            "satellites":  _gps.satellites,
            "has_fix":     _gps.has_fix,
        },
        "battery": {
            "voltage_v":   _telem.voltage_v,
            "current_a":   _telem.current_a,
            "mah_used":    _telem.mah_used,
            "battery_pct": _telem.battery_pct,
        },
        "attitude": {
            "pitch_deg": _telem.pitch_deg,
            "roll_deg":  _telem.roll_deg,
            "yaw_deg":   _telem.yaw_deg,
        },
        "sailboat": {
            "rudder":     _telem.rudder,
            "sail":       _telem.sail,
            "throttle":   _telem.throttle,
            "mcu_temp_c": _telem.mcu_temp_c,
        },
    }


def _on_telem_update() -> None:
    """Called by the bridge on each decoded frame; schedules a WebSocket broadcast."""
    asyncio.get_event_loop().create_task(_broadcast(_telemetry_payload()))


@asynccontextmanager
async def _lifespan(app: FastAPI):
    task = asyncio.create_task(
        elrs_bridge_task(
            state=_state,
            gps=_gps,
            telem=_telem,
            on_update=_on_telem_update,
        )
    )
    yield
    task.cancel()
    try:
        await task
    except asyncio.CancelledError:
        pass


app = FastAPI(title="rcsailboat base station", lifespan=_lifespan)
app.mount("/static", StaticFiles(directory=_STATIC), name="static")
app.mount("/tiles",  StaticFiles(directory=_TILES),  name="tiles")


@app.get("/")
async def root() -> FileResponse:
    return FileResponse(_STATIC / "index.html", headers={"Cache-Control": "no-store"})


@app.get("/telemetry")
async def telemetry() -> JSONResponse:
    """Polling endpoint — returns the latest telemetry snapshot as JSON."""
    return JSONResponse(_telemetry_payload())


@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket) -> None:
    await ws.accept()
    _connections.add(ws)
    logger.info("WebSocket client connected (%d total)", len(_connections))

    # Send current telemetry immediately on connect so the UI doesn't show stale data
    await ws.send_json(_telemetry_payload())

    try:
        while True:
            raw = await ws.receive_text()
            try:
                msg = json.loads(raw)
            except json.JSONDecodeError:
                logger.warning("Received non-JSON message: %s", raw)
                continue

            if msg.get("stop"):
                _state.stop()
            else:
                _state.apply(
                    rudder=float(msg.get("rudder",   _state.rudder)),
                    sail=float(msg.get("sail",     _state.sail)),
                    throttle=float(msg.get("throttle", _state.throttle)),
                    armed=bool(msg.get("armed",    _state.armed)),
                )

            logger.info(
                "state: rudder=%.2f sail=%.2f throttle=%.2f armed=%s",
                _state.rudder, _state.sail, _state.throttle, _state.armed,
            )
    except WebSocketDisconnect:
        logger.info("WebSocket client disconnected")
    finally:
        _connections.discard(ws)
