"""
ELRS bridge — bidirectional CRSF over serial between the Pi and the ELRS TX module.

TX path (Pi → boat, 50 Hz):
    Pack DesiredState into a CRSF RC Channels Packed frame (type 0x16) and write
    it to the TX module.  The TX module encodes it into ELRS and sends it over RF.

RX path (boat → Pi, variable rate):
    Parse incoming CRSF telemetry frames from the TX module and update the shared
    GpsPosition / TelemetryState objects.  Decoded frame types:
        0x02  GPS           (1 Hz when fix)
        0x08  Battery       (2 Hz)
        0x1E  Attitude      (5 Hz)
        0x80  Sailboat      (5 Hz, custom)

Configuration (environment variables):
    ELRS_PORT    Serial device path   (default /dev/ttyUSB0)
    ELRS_BAUD    Baud rate            (default 420000)
"""

import asyncio
import logging
import math
import os
import struct
from collections.abc import Callable

import serial_asyncio

from .state import DesiredState, GpsPosition, TelemetryState

logger = logging.getLogger(__name__)

# ── Configuration ─────────────────────────────────────────────────────────────
ELRS_PORT    = os.environ.get("ELRS_PORT", "/dev/ttyUSB0")
ELRS_BAUD    = int(os.environ.get("ELRS_BAUD", "420000"))
TRANSMIT_HZ  = 50
RETRY_DELAY  = 5.0   # seconds between reconnect attempts

# ── CRSF constants ────────────────────────────────────────────────────────────
CRSF_SYNC          = 0xC8
CRSF_TYPE_RC       = 0x16   # RC Channels Packed (Pi → TX module)
CRSF_TYPE_GPS      = 0x02   # GPS telemetry
CRSF_TYPE_BATTERY  = 0x08   # Battery sensor
CRSF_TYPE_ATTITUDE = 0x1E   # Attitude (pitch/roll/yaw)
CRSF_TYPE_SAILBOAT = 0x80   # Custom sailboat frame
CRSF_TYPE_DEVICES  = 0x81   # Custom device-status bitmap

CRSF_CH_MIN    = 172
CRSF_CH_CENTER = 992
CRSF_CH_MAX    = 1811

# Channel indices (from shared/protocol.h)
CH_RUDDER   = 0
CH_SAIL     = 1
CH_THROTTLE = 2
CH_ARM      = 3
CH_MODE     = 4


# ── CRC-8 (polynomial 0xD5, same as firmware's crsf_crc8) ─────────────────────
def _crc8(data: bytes | bytearray) -> int:
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0xD5) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


# ── RC frame builder ──────────────────────────────────────────────────────────

def _ch_centered(v: float) -> int:
    """Map -1.0..+1.0 to CRSF_CH_MIN..CRSF_CH_MAX."""
    return max(CRSF_CH_MIN, min(CRSF_CH_MAX,
               round(v * (CRSF_CH_MAX - CRSF_CH_CENTER) + CRSF_CH_CENTER)))


def _ch_unipolar(v: float) -> int:
    """Map 0.0..1.0 to CRSF_CH_MIN..CRSF_CH_MAX."""
    return max(CRSF_CH_MIN, min(CRSF_CH_MAX,
               round(v * (CRSF_CH_MAX - CRSF_CH_MIN) + CRSF_CH_MIN)))


def _build_rc_frame(state: DesiredState) -> bytes:
    """Build a 26-byte CRSF RC Channels Packed frame from the current DesiredState."""
    channels = [CRSF_CH_CENTER] * 16
    channels[CH_RUDDER]   = _ch_centered(state.rudder)
    channels[CH_SAIL]     = _ch_unipolar(state.sail)
    # Safety: clamp throttle to zero when disarmed
    channels[CH_THROTTLE] = _ch_centered(state.throttle if state.armed else 0.0)
    channels[CH_ARM]      = CRSF_CH_MAX if state.armed else CRSF_CH_MIN
    channels[CH_MODE]     = CRSF_CH_CENTER  # manual mode

    # Pack 16 × 11-bit channels into 22 bytes, LSB-first
    bits = 0
    for i, ch in enumerate(channels):
        bits |= (ch & 0x7FF) << (i * 11)
    payload = bits.to_bytes(22, "little")

    # Frame: [sync][length][type][22-byte payload][crc]
    # length = type(1) + payload(22) + crc(1) = 24
    header = bytes([CRSF_SYNC, 24, CRSF_TYPE_RC])
    frame  = header + payload
    return frame + bytes([_crc8(frame[2:])])   # CRC covers type + payload


# ── Telemetry frame decoders ──────────────────────────────────────────────────

def _decode_gps(payload: bytes, gps: GpsPosition) -> bool:
    """CRSF GPS frame (0x02) — 15-byte big-endian payload."""
    if len(payload) < 15:
        return False
    lat_raw, lng_raw, spd_raw, hdg_raw, alt_raw, sats = struct.unpack_from(">iiHHHB", payload)
    gps.lat         = lat_raw / 1e7
    gps.lng         = lng_raw / 1e7
    gps.speed_kmh   = spd_raw / 10.0
    gps.heading_deg = hdg_raw / 100.0
    gps.altitude_m  = alt_raw - 1000.0
    gps.satellites  = sats
    gps.has_fix     = sats >= 3
    return True


def _decode_battery(payload: bytes, telem: TelemetryState) -> bool:
    """CRSF Battery frame (0x08) — 8-byte big-endian payload."""
    if len(payload) < 8:
        return False
    voltage_raw, current_raw = struct.unpack_from(">HH", payload, 0)
    mah_raw = int.from_bytes(payload[4:7], "big")
    telem.voltage_v  = voltage_raw / 10.0
    telem.current_a  = current_raw / 10.0
    telem.mah_used   = float(mah_raw)
    telem.battery_pct = payload[7]
    return True


def _decode_attitude(payload: bytes, telem: TelemetryState) -> bool:
    """CRSF Attitude frame (0x1E) — 6-byte big-endian payload (rad × 10000)."""
    if len(payload) < 6:
        return False
    pitch_raw, roll_raw, yaw_raw = struct.unpack_from(">hhh", payload)
    telem.pitch_deg = math.degrees(pitch_raw / 10000.0)
    telem.roll_deg  = math.degrees(roll_raw  / 10000.0)
    telem.yaw_deg   = math.degrees(yaw_raw   / 10000.0)
    return True


_DEVICE_ORDER  = ('ft', 'qmi', 'pca', 'ina', 'sd', 'bilge', 'pump', 'rudder', 'winch', 'esc')
_DEVICE_LEVELS = ('absent', 'ok', 'warn', 'error')


def _decode_devices(payload: bytes, telem: TelemetryState) -> bool:
    """Custom device-status frame (0x81) — 3-byte payload, 2 bits per device, LSB-first."""
    if len(payload) < 3:
        return False
    bits = payload[0] | (payload[1] << 8) | (payload[2] << 16)
    telem.device_status = {
        name: _DEVICE_LEVELS[(bits >> (i * 2)) & 3]
        for i, name in enumerate(_DEVICE_ORDER)
    }
    return True


def _decode_sailboat(payload: bytes, telem: TelemetryState) -> bool:
    """Custom sailboat frame (0x80) — 8-byte big-endian payload."""
    if len(payload) < 8:
        return False
    rudder_raw, sail_raw, esc_raw, temp_raw = struct.unpack_from(">hhhh", payload)
    telem.rudder    = rudder_raw / 10000.0
    telem.sail      = sail_raw   / 10000.0
    telem.throttle  = esc_raw    / 10000.0
    telem.mcu_temp_c = temp_raw  / 10.0
    return True


# ── Frame parser (streaming byte buffer) ──────────────────────────────────────

def _parse_frames(
    buf: bytearray,
    gps: GpsPosition,
    telem: TelemetryState,
    on_update: Callable[[], None] | None,
) -> None:
    """Consume all complete, valid CRSF frames from buf in-place."""
    while len(buf) >= 2:
        # Scan for sync byte
        if buf[0] != CRSF_SYNC:
            del buf[0]
            continue

        length = buf[1]
        total  = 2 + length   # sync + length-field + length bytes
        if len(buf) < total:
            break  # wait for more data

        frame   = buf[:total]
        del buf[:total]

        # Verify CRC (covers type + payload, i.e. frame[2:-1])
        if _crc8(frame[2:-1]) != frame[-1]:
            logger.debug("CRSF CRC mismatch — dropped frame type 0x%02X", frame[2])
            continue

        frame_type = frame[2]
        payload    = bytes(frame[3:-1])

        updated = False
        if frame_type == CRSF_TYPE_GPS:
            updated = _decode_gps(payload, gps)
        elif frame_type == CRSF_TYPE_BATTERY:
            updated = _decode_battery(payload, telem)
        elif frame_type == CRSF_TYPE_ATTITUDE:
            updated = _decode_attitude(payload, telem)
        elif frame_type == CRSF_TYPE_SAILBOAT:
            updated = _decode_sailboat(payload, telem)
        elif frame_type == CRSF_TYPE_DEVICES:
            updated = _decode_devices(payload, telem)

        if updated and on_update:
            on_update()


# ── Main bridge coroutine ─────────────────────────────────────────────────────

async def elrs_bridge_task(
    state: DesiredState,
    gps: GpsPosition,
    telem: TelemetryState,
    on_update: Callable[[], None] | None = None,
) -> None:
    """
    Run the ELRS bridge forever, reconnecting on serial errors.

    - Opens ELRS_PORT at ELRS_BAUD.
    - Sends CRSF RC frames at TRANSMIT_HZ.
    - Parses incoming CRSF telemetry and calls on_update() on each decoded frame.
    """
    logger.info("ELRS bridge starting — port=%s baud=%d", ELRS_PORT, ELRS_BAUD)
    interval = 1.0 / TRANSMIT_HZ

    while True:
        try:
            reader, writer = await serial_asyncio.open_serial_connection(
                url=ELRS_PORT, baudrate=ELRS_BAUD
            )
            telem.bridge_connected = True
            logger.info("ELRS bridge connected to %s", ELRS_PORT)

            buf: bytearray = bytearray()
            tx_task = asyncio.create_task(_tx_loop(writer, state, interval))

            try:
                while True:
                    chunk = await asyncio.wait_for(reader.read(256), timeout=2.0)
                    if not chunk:
                        break
                    buf.extend(chunk)
                    _parse_frames(buf, gps, telem, on_update)
            except asyncio.TimeoutError:
                logger.warning("ELRS serial RX timeout — no data for 2 s")
            finally:
                tx_task.cancel()
                try:
                    await tx_task
                except asyncio.CancelledError:
                    pass
                writer.close()

        except (OSError, serial_asyncio.serial.SerialException) as exc:
            logger.warning("ELRS serial error: %s — retrying in %.0fs", exc, RETRY_DELAY)
        finally:
            telem.bridge_connected = False

        await asyncio.sleep(RETRY_DELAY)


async def _tx_loop(writer: asyncio.StreamWriter, state: DesiredState, interval: float) -> None:
    """Send RC frames at TRANSMIT_HZ until cancelled."""
    while True:
        frame = _build_rc_frame(state)
        writer.write(frame)
        await writer.drain()
        await asyncio.sleep(interval)
