"""
esp32_console.py — stream ESP32 USB serial output to WebSocket clients.

When ESP32_DBG_PORT is set (e.g. /dev/esp32-debug), this task opens that serial
port at 115200 baud and calls on_line() for each line received.  main.py
forwards those lines to browsers as {"type": "console", "line": "..."} messages.

If ESP32_DBG_PORT is not set the task returns immediately — console disabled.
The base station continues to operate normally; this only affects the debug tab.

Configuration (environment variables):
    ESP32_DBG_PORT  — serial device  (default: unset → disabled)
    ESP32_DBG_BAUD  — baud rate      (default: 115200)
"""

import asyncio
import logging
import os
from collections.abc import Callable

import serial_asyncio

logger = logging.getLogger(__name__)

ESP32_DBG_PORT = os.environ.get("ESP32_DBG_PORT", "")
ESP32_DBG_BAUD = int(os.environ.get("ESP32_DBG_BAUD", "115200"))
CONSOLE_ENABLED = bool(ESP32_DBG_PORT)

_RETRY_DELAY = 5.0


async def esp32_console_task(on_line: Callable[[str], None]) -> None:
    """
    Read lines from the ESP32 USB serial port and call on_line() for each.

    Reconnects automatically on disconnect.  Returns immediately (no-op) if
    ESP32_DBG_PORT is not configured.
    """
    if not ESP32_DBG_PORT:
        logger.info("ESP32_DBG_PORT not set — USB console disabled")
        return

    logger.info("ESP32 console: opening %s at %d baud", ESP32_DBG_PORT, ESP32_DBG_BAUD)

    while True:
        try:
            reader, writer = await serial_asyncio.open_serial_connection(
                url=ESP32_DBG_PORT, baudrate=ESP32_DBG_BAUD
            )
            logger.info("ESP32 console: connected")
            try:
                while True:
                    raw = await reader.readline()
                    if not raw:
                        break  # EOF — device unplugged
                    text = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                    if text:
                        on_line(text)
            finally:
                writer.close()

        except (OSError, serial_asyncio.serial.SerialException) as exc:
            logger.debug("ESP32 console: %s — retry in %.0fs", exc, _RETRY_DELAY)

        await asyncio.sleep(_RETRY_DELAY)
