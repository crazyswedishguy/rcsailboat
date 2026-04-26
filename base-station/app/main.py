import json
import logging
from pathlib import Path

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from .state import DesiredState, GpsPosition

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

_STATIC = Path(__file__).parent.parent / "static"
_TILES  = _STATIC / "tiles"
_TILES.mkdir(parents=True, exist_ok=True)

app = FastAPI(title="rcsailboat base station")
app.mount("/static", StaticFiles(directory=_STATIC), name="static")
app.mount("/tiles",  StaticFiles(directory=_TILES),  name="tiles")

_state = DesiredState()
_gps   = GpsPosition()

# All currently open WebSocket connections — used for server-initiated GPS broadcasts.
_connections: set[WebSocket] = set()


async def broadcast(msg: dict) -> None:
    """Send a JSON message to every connected browser client.

    Silently drops connections that have gone away since the last broadcast.
    """
    dead: set[WebSocket] = set()
    for ws in _connections:
        try:
            await ws.send_json(msg)
        except Exception:
            dead.add(ws)
    _connections.difference_update(dead)


@app.get("/")
async def root() -> FileResponse:
    return FileResponse(_STATIC / "index.html", headers={"Cache-Control": "no-store"})


@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket) -> None:
    await ws.accept()
    _connections.add(ws)
    logger.info("WebSocket client connected (%d total)", len(_connections))
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
                    rudder=float(msg.get("rudder", _state.rudder)),
                    sail=float(msg.get("sail", _state.sail)),
                    throttle=float(msg.get("throttle", _state.throttle)),
                    armed=bool(msg.get("armed", _state.armed)),
                )

            logger.info(
                "state: rudder=%.2f sail=%.2f throttle=%.2f armed=%s",
                _state.rudder,
                _state.sail,
                _state.throttle,
                _state.armed,
            )
    except WebSocketDisconnect:
        logger.info("WebSocket client disconnected")
    finally:
        _connections.discard(ws)
