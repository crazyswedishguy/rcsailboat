import logging

from fastapi import FastAPI, WebSocket

from .state import DesiredState

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

app = FastAPI(title="rcsailboat base station")

_state = DesiredState()


@app.get("/")
async def root() -> dict[str, str]:
    return {"status": "ok"}


@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket) -> None:
    await ws.accept()
    logger.info("WebSocket client connected (stub — no control handling yet)")
    try:
        while True:
            await ws.receive_text()
    except Exception:
        pass
