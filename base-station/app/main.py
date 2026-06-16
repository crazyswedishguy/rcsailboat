"""
main.py — FastAPI application for the RC sailboat base station.

Ties together three concerns:

  1. ELRS bridge (background task):
       Opened at startup. Sends CRSF RC frames to the ELRS TX module at 50 Hz
       and decodes incoming CRSF telemetry frames.

  2. ESP32 USB console (background task):
       When ESP32_DBG_PORT is set, reads serial lines from the tethered ESP32
       and forwards them to browsers as {"type":"console","line":"..."} messages.

  3. WebSocket hub:
       Every connected browser receives telemetry and control-state pushes.
       Incoming messages implement a controller / observer model:
         - any authenticated client may request control
         - only the active controller can send servo commands
         - releasing control with a live pending request transfers directly;
           otherwise the boat goes to failsafe positions

Run with:
    uvicorn app.main:app --host 0.0.0.0 --port 8000

Environment variables:
    ELRS_PORT      — serial device for XIAO CRSF bridge (default /dev/ttyACM0)
    ELRS_BAUD      — baud rate                          (default 400000)
    ESP32_DBG_PORT — serial device for ESP32 USB debug  (default: unset = disabled)
    RCSB_TOKEN     — shared passphrase for WS auth      (default: unset = disabled)
"""

import asyncio
import json
import logging
import os
import time
import uuid
from contextlib import asynccontextmanager
from pathlib import Path

import httpx
from fastapi import FastAPI, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles

from .elrs_bridge import elrs_bridge_task
from .esp32_console import CONSOLE_ENABLED, esp32_console_task
from .state import DesiredState, GpsPosition, TelemetryState

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# ── File system paths ─────────────────────────────────────────────────────────

_STATIC = Path(__file__).parent.parent / "static"
_TILES  = _STATIC / "tiles"
_TILES.mkdir(parents=True, exist_ok=True)

# ── Shared state (single asyncio event loop — no locking needed) ──────────────

_state = DesiredState()
_gps   = GpsPosition()
_telem = TelemetryState()

# ── Auth ──────────────────────────────────────────────────────────────────────

RCSB_TOKEN = os.environ.get("RCSB_TOKEN", "changeme")  # set to "" to disable auth

# ── Controller / observer state ───────────────────────────────────────────────

_connections: set[WebSocket] = set()
_sessions: dict[WebSocket, str] = {}             # ws → short session ID

_controller: WebSocket | None = None             # client currently holding the helm
_controller_label: str = ""
# Pending claim: (ws, label, expires_at).  None when no request is live.
_pending: tuple[WebSocket, str, float] | None = None

PENDING_TIMEOUT = 10.0   # seconds a claim request stays valid

# ── Broadcast helpers ─────────────────────────────────────────────────────────

async def _broadcast(msg: dict) -> None:
    """Send a JSON message to every connected browser, pruning dead sockets."""
    dead: set[WebSocket] = set()
    for ws in _connections:
        try:
            await ws.send_json(msg)
        except Exception:
            dead.add(ws)
    _connections.difference_update(dead)


def _control_state_payload() -> dict:
    """Snapshot of the current controller / observer state for broadcast."""
    now = time.monotonic()
    pending_label     = None
    pending_expires_in = None
    pending_session   = None

    if _pending:
        ws, label, expires_at = _pending
        remaining = expires_at - now
        if remaining > 0:
            pending_label      = label
            pending_expires_in = round(remaining, 1)
            pending_session    = _sessions.get(ws)

    return {
        "type":               "control_state",
        "controller_session": _sessions.get(_controller) if _controller else None,
        "controller_label":   _controller_label if _controller else None,
        "pending_session":    pending_session,
        "pending_label":      pending_label,
        "pending_expires_in": pending_expires_in,
    }


async def _broadcast_control_state() -> None:
    global _pending
    # Lazily expire a stale pending request before broadcasting.
    if _pending and time.monotonic() > _pending[2]:
        _pending = None
    await _broadcast(_control_state_payload())


def _telemetry_payload() -> dict:
    return {
        "type":   "telemetry",
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
        "link": {
            "rssi_uplink":   _telem.rssi_uplink,
            "lq_uplink":     _telem.lq_uplink,
            "snr_uplink":    _telem.snr_uplink,
            "rssi_downlink": _telem.rssi_downlink,
            "lq_downlink":   _telem.lq_downlink,
            "snr_downlink":  _telem.snr_downlink,
            "tx_power_mw":   _telem.tx_power_mw,
        },
        "device_status": _telem.device_status,
        "console":        {"enabled": CONSOLE_ENABLED},
        "updated_at": {
            "telem": _telem.last_updated_at,
            "gps":   _gps.last_updated_at,
        },
    }


def _on_telem_update() -> None:
    asyncio.get_event_loop().create_task(_broadcast(_telemetry_payload()))


def _on_console_line(line: str) -> None:
    asyncio.get_event_loop().create_task(_broadcast({"type": "console", "line": line}))


# ── Controller state machine ──────────────────────────────────────────────────

async def _handle_request_control(ws: WebSocket, label: str) -> None:
    global _controller, _controller_label, _pending

    if _controller is None:
        # Helm is free — claim it immediately.
        _controller       = ws
        _controller_label = label
        _state.armed      = True
        logger.info("Control claimed by '%s' (%s)", label, _sessions.get(ws, "?"))
        await _broadcast_control_state()
        return

    if ws is _controller:
        return  # already in control

    # Queue a pending request and schedule its expiry notification.
    expires_at = time.monotonic() + PENDING_TIMEOUT
    _pending = (ws, label, expires_at)
    logger.info("Control requested by '%s' (%s)", label, _sessions.get(ws, "?"))
    await _broadcast_control_state()
    asyncio.get_event_loop().create_task(_pending_expiry_task(expires_at))


async def _pending_expiry_task(expires_at: float) -> None:
    global _pending
    await asyncio.sleep(expires_at - time.monotonic())
    if _pending and _pending[2] == expires_at:
        logger.info("Control request timed out")
        _pending = None
        await _broadcast_control_state()


async def _release_control(ws: WebSocket) -> None:
    """Release control from ws.  Transfers to a pending requester if one exists."""
    global _controller, _controller_label, _pending

    if ws is not _controller:
        return

    if _pending and _pending[2] > time.monotonic():
        # Live request waiting — hand off directly, no failsafe gap.
        new_ws, new_label, _ = _pending
        _pending          = None
        _controller       = new_ws
        _controller_label = new_label
        logger.info("Control transferred to '%s' (%s)", new_label, _sessions.get(new_ws, "?"))
    else:
        # No valid request — failsafe.
        _pending          = None
        _controller       = None
        _controller_label = ""
        _state.disarm()
        logger.info("Control released — failsafe active")

    await _broadcast_control_state()


async def _dismiss_request() -> None:
    global _pending
    if _pending:
        logger.info("Request from '%s' dismissed by controller", _pending[1])
        _pending = None
        await _broadcast_control_state()


async def _handle_disconnect(ws: WebSocket) -> None:
    global _controller, _controller_label, _pending

    _connections.discard(ws)
    sid = _sessions.pop(ws, "?")

    if ws is _controller:
        # Controller dropped — always failsafe on unexpected disconnect.
        _pending          = None
        _controller       = None
        _controller_label = ""
        _state.disarm()
        logger.warning("Controller disconnected (%s) — failsafe active", sid)
        await _broadcast_control_state()
    elif _pending and _pending[0] is ws:
        _pending = None
        await _broadcast_control_state()


# ── Application lifespan ──────────────────────────────────────────────────────

@asynccontextmanager
async def _lifespan(app: FastAPI):
    elrs_task    = asyncio.create_task(
        elrs_bridge_task(state=_state, gps=_gps, telem=_telem, on_update=_on_telem_update)
    )
    console_task = asyncio.create_task(esp32_console_task(on_line=_on_console_line))
    yield
    for t in (elrs_task, console_task):
        t.cancel()
        try:
            await t
        except asyncio.CancelledError:
            pass


# ── FastAPI application ───────────────────────────────────────────────────────

app = FastAPI(title="rcsailboat base station", lifespan=_lifespan)

app.mount("/static", StaticFiles(directory=_STATIC), name="static")
app.mount("/tiles",  StaticFiles(directory=_TILES),  name="tiles")


@app.middleware("http")
async def no_store_for_src(request: Request, call_next):
    """JSX source files must never be cached — Babel compiles them at load time."""
    response = await call_next(request)
    if request.url.path.startswith("/static/src/") or request.url.path == "/":
        response.headers["Cache-Control"] = "no-store"
    return response


@app.get("/")
async def root() -> FileResponse:
    return FileResponse(_STATIC / "index.html")


_ESP32_HOST = os.environ.get("ESP32_HOST", "192.168.4.1")


@app.get("/api/esp32-diag")
async def esp32_diag(host: str | None = None) -> JSONResponse:
    """Proxy a /diag.json request to the ESP32 WiFi AP for device status."""
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
        return JSONResponse({"ok": False, "host": target, "error": str(exc)}, status_code=503)


@app.post("/api/esp32-restart")
async def esp32_restart(host: str | None = None) -> JSONResponse:
    """Tell the ESP32 WiFi AP to reboot."""
    target = host or _ESP32_HOST
    try:
        async with httpx.AsyncClient(timeout=5.0) as client:
            await client.post(f"http://{target}/restart")
        return JSONResponse({"ok": True, "host": target})
    except Exception as exc:  # noqa: BLE001
        return JSONResponse({"ok": False, "host": target, "error": str(exc)}, status_code=503)


@app.get("/telemetry")
async def telemetry() -> JSONResponse:
    """Polling fallback — latest telemetry snapshot as JSON."""
    return JSONResponse(_telemetry_payload())


@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket) -> None:
    """
    WebSocket endpoint — bidirectional channel between the browser and the Pi.

    Query params:
      token  — required when RCSB_TOKEN env var is set

    Inbound message types  (browser → server):
      {"type":"control",        "rudder":f, "sail":f, "throttle":f}
        — only accepted from the active controller
      {"type":"request_control","label":str}
        — claim control (immediately if free; queued for PENDING_TIMEOUT s otherwise)
      {"type":"release_control"}
        — controller releases; transfers to pending requester if live, else failsafe
      {"type":"accept_handoff"}
        — controller explicitly hands off to the pending requester
      {"type":"dismiss_request"}
        — controller rejects the pending request
      {"type":"stop"}
        — controller: zero throttle + release (failsafe unless requester is live)

    Outbound message types  (server → browser):
      {"type":"session",       "id":str}              — sent once on connect
      {"type":"telemetry",     ...}                   — on each decoded CRSF frame
      {"type":"console",       "line":str}            — ESP32 USB serial lines
      {"type":"control_state", ...}                   — on any role change or expiry
    """
    # ── Token auth ────────────────────────────────────────────────────────────
    if RCSB_TOKEN:
        if ws.query_params.get("token", "") != RCSB_TOKEN:
            await ws.close(code=4001, reason="unauthorized")
            return

    await ws.accept()

    session_id = str(uuid.uuid4())[:8]
    _sessions[ws] = session_id
    _connections.add(ws)
    logger.info("WS connected: session=%s  total=%d", session_id, len(_connections))

    # Send initial snapshots so the UI isn't blank.
    await ws.send_json({"type": "session", "id": session_id})
    await ws.send_json(_telemetry_payload())
    await ws.send_json(_control_state_payload())

    try:
        while True:
            raw = await ws.receive_text()
            try:
                msg = json.loads(raw)
            except json.JSONDecodeError:
                continue

            t = msg.get("type", "")

            if t == "control":
                if ws is _controller:
                    _state.apply(
                        rudder=float(msg.get("rudder",   _state.rudder)),
                        sail=float(msg.get("sail",       _state.sail)),
                        throttle=float(msg.get("throttle", _state.throttle)),
                    )

            elif t == "request_control":
                label = str(msg.get("label", "unnamed"))[:32].strip() or "unnamed"
                await _handle_request_control(ws, label)

            elif t == "release_control":
                await _release_control(ws)

            elif t == "accept_handoff":
                if ws is _controller and _pending:
                    await _release_control(ws)

            elif t == "dismiss_request":
                if ws is _controller and _pending:
                    await _dismiss_request()

            elif t == "stop":
                if ws is _controller:
                    _state.throttle = 0.0
                    await _release_control(ws)

    except WebSocketDisconnect:
        logger.info("WS disconnected: session=%s", session_id)
    finally:
        await _handle_disconnect(ws)
