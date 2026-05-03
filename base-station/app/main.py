"""
main.py — FastAPI application for the RC sailboat base station.

This is the entry point for the Pi-side server. It ties together three concerns:

  1. ELRS bridge (background task):
       Opened at startup via the FastAPI lifespan hook. Sends CRSF RC frames to
       the ELRS TX module at 50 Hz and decodes incoming CRSF telemetry frames.
       On each decoded frame it calls _on_telem_update() to push data to browsers.

  2. WebSocket hub:
       Every connected browser receives telemetry pushes as JSON objects. The
       WebSocket endpoint also accepts incoming JSON messages from the browser
       (slider values, arm/disarm) and applies them to the shared DesiredState.

  3. Static file serving:
       The Leaflet map UI (base-station/static/index.html) and offline OSM tiles
       (base-station/static/tiles/{z}/{x}/{y}.png) are served as static files.
       Tiles are pre-downloaded by tools/tile_downloader/tile_downloader.py.

Run with:
    uvicorn app.main:app --host 0.0.0.0 --port 8000

Environment variables:
    ELRS_PORT   — serial device for ELRS TX module (default /dev/ttyUSB0)
    ELRS_BAUD   — baud rate (default 420000)
"""

import asyncio
import json
import logging
import os
from contextlib import asynccontextmanager
from pathlib import Path

import httpx
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles

from .elrs_bridge import elrs_bridge_task
from .state import DesiredState, GpsPosition, TelemetryState

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# ── File system paths ─────────────────────────────────────────────────────────

_STATIC = Path(__file__).parent.parent / "static"
_TILES  = _STATIC / "tiles"
_TILES.mkdir(parents=True, exist_ok=True)   # ensure tiles directory exists on first run

# ── Shared state (all access is on the asyncio event loop — no locking needed) ─

_state = DesiredState()    # what the user wants the boat to do
_gps   = GpsPosition()     # latest GPS fix from the boat
_telem = TelemetryState()  # all other telemetry from the boat

# ── WebSocket connection registry ─────────────────────────────────────────────

_connections: set[WebSocket] = set()


async def _broadcast(msg: dict) -> None:
    """Send a JSON message to every connected browser client, pruning dead connections."""
    dead: set[WebSocket] = set()
    for ws in _connections:
        try:
            await ws.send_json(msg)
        except Exception:
            dead.add(ws)
    _connections.difference_update(dead)


# ── Telemetry payload builder ─────────────────────────────────────────────────

def _telemetry_payload() -> dict:
    """
    Build the canonical telemetry JSON dict that is sent to browser clients.

    This dict is sent on WebSocket connect (immediate snapshot) and on every
    decoded CRSF frame thereafter. The browser's handleTelemetry() function
    dispatches on the top-level 'type' field.
    """
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
    """
    Callback invoked by the ELRS bridge on each successfully decoded CRSF frame.

    Schedules a WebSocket broadcast on the running asyncio event loop.
    This is called from inside an asyncio coroutine, so create_task is safe.
    """
    asyncio.get_event_loop().create_task(_broadcast(_telemetry_payload()))


# ── Application lifespan ──────────────────────────────────────────────────────

@asynccontextmanager
async def _lifespan(app: FastAPI):
    """Start the ELRS bridge task on startup; cancel it cleanly on shutdown."""
    task = asyncio.create_task(
        elrs_bridge_task(
            state=_state,
            gps=_gps,
            telem=_telem,
            on_update=_on_telem_update,
        )
    )
    yield  # application runs here
    task.cancel()
    try:
        await task
    except asyncio.CancelledError:
        pass


# ── FastAPI application ───────────────────────────────────────────────────────

app = FastAPI(title="rcsailboat base station", lifespan=_lifespan)

# Serve static assets (CSS, JS, images) and pre-downloaded map tiles.
app.mount("/static", StaticFiles(directory=_STATIC), name="static")
app.mount("/tiles",  StaticFiles(directory=_TILES),  name="tiles")


@app.get("/")
async def root() -> FileResponse:
    """Serve the main web UI (index.html). Cache-Control: no-store for dev convenience."""
    return FileResponse(_STATIC / "index.html", headers={"Cache-Control": "no-store"})


_ESP32_HOST = os.environ.get("ESP32_HOST", "192.168.4.1")


@app.get("/api/esp32-diag")
async def esp32_diag(host: str | None = None) -> JSONResponse:
    """
    Proxy a /diag.json request to the ESP32 web interface.

    The ESP32 hosts its own WiFi AP ("darkandstormy", 192.168.4.1 by default).
    Connect the Pi (or test machine) to that AP, then call this endpoint.
    The host can be overridden via the query param or the ESP32_HOST env var.
    """
    target = host or _ESP32_HOST
    try:
        async with httpx.AsyncClient(timeout=3.0) as client:
            resp = await client.get(f"http://{target}/diag.json")
            resp.raise_for_status()
            return JSONResponse({"ok": True, "host": target, "devices": resp.json()})
    except httpx.HTTPStatusError as exc:
        return JSONResponse(
            {"ok": False, "host": target, "error": f"HTTP {exc.response.status_code}"},
            status_code=502,
        )
    except Exception as exc:  # noqa: BLE001
        return JSONResponse(
            {"ok": False, "host": target, "error": str(exc)},
            status_code=503,
        )


@app.get("/telemetry")
async def telemetry() -> JSONResponse:
    """
    Polling fallback — returns the latest telemetry snapshot as JSON.

    Browsers that cannot use WebSocket (or for debugging) can poll this endpoint.
    Equivalent to a single WebSocket push frame.
    """
    return JSONResponse(_telemetry_payload())


@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket) -> None:
    """
    WebSocket endpoint — bidirectional channel between the browser and the Pi.

    On connect: sends an immediate telemetry snapshot so the UI is populated
    without waiting for the next CRSF frame from the boat.

    Incoming messages (browser → Pi):
      { "rudder": float, "sail": float, "throttle": float, "armed": bool }
        — applies the new values to DesiredState (clamped by state.py)
      { "stop": true }
        — immediately disarms and zeroes throttle (emergency stop)

    All incoming values are validated by DesiredState.apply() and .stop().
    """
    await ws.accept()
    _connections.add(ws)
    logger.info("WebSocket client connected (%d total)", len(_connections))

    # Send current state immediately so the UI is not blank on load.
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
                    sail=float(msg.get("sail",       _state.sail)),
                    throttle=float(msg.get("throttle", _state.throttle)),
                    armed=bool(msg.get("armed",      _state.armed)),
                )

            logger.info(
                "state: rudder=%.2f sail=%.2f throttle=%.2f armed=%s",
                _state.rudder, _state.sail, _state.throttle, _state.armed,
            )
    except WebSocketDisconnect:
        logger.info("WebSocket client disconnected")
    finally:
        _connections.discard(ws)
