#!/usr/bin/env python3
"""
elrs-console.py — Interactive ELRS debugging console for rcsailboat.

Connects simultaneously to:
  XIAO ESP32-S3 (crsf-bridge, Mode 2 with serial command interface)
  Waveshare ESP32-S3 (elrs-test firmware — prints received channel values)

Interactive mode (default):
  Type commands for the XIAO; output from both boards is prefixed [XIAO] / [BOAT].

Automated test mode (--test):
  Sends pre-programmed channel values and verifies the Waveshare echoes them back.
  Exit code = number of failures (0 = all passed).

Commands accepted by the XIAO crsf-bridge:
  rud <-1..1>   — rudder channel (centred at 0)
  sail <0..1>   — sail trim channel
  thr <0..1>    — throttle channel
  arm           — arm (enables rudder/thr pass-through)
  disarm        — disarm
  pump <0/1>    — bilge pump override
  neutral       — all channels to centre/neutral, disarm
  lock          — suppress 500 ms control timeout (good for testing)
  unlock        — re-enable timeout
  show          — print current XIAO state
  ping          — send a CRSF DEVICE_PING to the Ranger Micro
  baud <hz>     — reinitialise XIAO→Ranger Micro UART at <hz>
"""

from __future__ import annotations

import argparse
import re
import select
import sys
import time

try:
    import serial
except ImportError:
    print("Error: pyserial not installed. Run: pip install pyserial", file=sys.stderr)
    sys.exit(1)

# Default serial ports on the Pi workbench
XIAO_PORT = "/dev/ttyACM0"
BOAT_PORT = "/dev/ttyACM1"
BAUD = 115200

# ── helpers ────────────────────────────────────────────────────────────────────

def open_port(path: str, label: str) -> serial.Serial:
    try:
        port = serial.Serial(path, BAUD, timeout=0)
        print(f"[console] {label} → {path}")
        return port
    except serial.SerialException as e:
        print(f"[console] ERROR: cannot open {path} ({label}): {e}", file=sys.stderr)
        sys.exit(1)


def send_cmd(xiao: serial.Serial, cmd: str) -> None:
    xiao.write((cmd.strip() + "\n").encode())
    print(f">> {cmd.strip()}")
    sys.stdout.flush()


def drain(port: serial.Serial, prefix: str, buf: list[str]) -> list[str]:
    """Non-blocking drain; returns complete lines emitted this call."""
    lines: list[str] = []
    raw = port.read(port.in_waiting or 1)
    if not raw:
        return lines
    for b in raw:
        ch = chr(b)
        if ch == "\n":
            line = "".join(buf).rstrip("\r")
            if line:
                print(f"{prefix} {line}", flush=True)
                lines.append(line)
            buf.clear()
        else:
            buf.append(ch)
    return lines


# ── interactive mode ───────────────────────────────────────────────────────────

def interactive(xiao: serial.Serial, boat: serial.Serial) -> None:
    xiao_buf: list[str] = []
    boat_buf: list[str] = []
    print("[console] Ready. Type commands for the XIAO. Ctrl-C or 'quit' to exit.")
    print("[console] Examples:  rud 0.5   arm   lock   neutral   show   baud 420000")
    sys.stdout.write("> ")
    sys.stdout.flush()
    while True:
        try:
            rlist, _, _ = select.select([sys.stdin, xiao, boat], [], [], 0.1)
        except KeyboardInterrupt:
            break
        for fd in rlist:
            if fd is sys.stdin:
                line = sys.stdin.readline().strip()
                if line in ("q", "quit", "exit"):
                    return
                if line:
                    send_cmd(xiao, line)
                sys.stdout.write("> ")
                sys.stdout.flush()
            elif fd is xiao:
                drain(xiao, "[XIAO]", xiao_buf)
            elif fd is boat:
                drain(boat, "[BOAT]", boat_buf)


# ── automated test suite ───────────────────────────────────────────────────────

def run_tests(xiao: serial.Serial, boat: serial.Serial) -> int:
    """Returns number of failed assertions."""
    xiao_buf: list[str] = []
    boat_buf: list[str] = []
    failures = 0

    def read_for(seconds: float) -> list[str]:
        deadline = time.monotonic() + seconds
        lines: list[str] = []
        while time.monotonic() < deadline:
            rem = deadline - time.monotonic()
            try:
                rlist, _, _ = select.select([xiao, boat], [], [], min(rem, 0.05))
            except Exception:
                break
            for fd in rlist:
                if fd is xiao:
                    drain(xiao, "[XIAO]", xiao_buf)
                elif fd is boat:
                    lines.extend(drain(boat, "[BOAT]", boat_buf))
        return lines

    def check(label: str, lines: list[str], pattern: str) -> bool:
        rx = re.compile(pattern)
        for ln in lines:
            if rx.search(ln):
                print(f"  PASS  {label}  → {ln.strip()}")
                return True
        print(f"  FAIL  {label}  (pattern={pattern!r}, last {len(lines)} BOAT lines)")
        return False

    nonlocal_failures = [0]

    def assert_ok(label: str, lines: list[str], pattern: str) -> None:
        if not check(label, lines, pattern):
            nonlocal_failures[0] += 1

    print("\n══════════════════════════════════════════════════")
    print("  ELRS channel pass-through test suite")
    print("══════════════════════════════════════════════════\n")

    # Warmup: let both boards settle and flush startup noise
    print("[ warmup 3 s ]")
    read_for(3.0)

    # ── T1a: XIAO receiving bytes from Ranger Micro? ──────────────────────────
    print("\n[T1a] XIAO ← Ranger Micro bytes (hardware RX check)")
    send_cmd(xiao, "show")
    lines_1a = read_for(1.0)
    xiao_lines = [ln for ln in lines_1a if "rx_bytes" in ln]
    rx_bytes = 0
    if xiao_lines:
        m = re.search(r"rx_bytes=(\d+)", xiao_lines[-1])
        if m:
            rx_bytes = int(m.group(1))
    if rx_bytes > 0:
        print(f"  PASS  Ranger Micro → XIAO RX: {rx_bytes} bytes received")
    else:
        print("  FAIL  rx_bytes=0 — XIAO Serial1 RX (GPIO44) receiving nothing from Ranger Micro")
        print("        Possible causes:")
        print("          1. GPIO44 (D7) not wired to Ranger Micro signal pin")
        print("          2. RP3 not powered/in-range (Ranger Micro has no telemetry to forward)")
        print("          3. Baud rate mismatch (try 'baud 400000' or 'baud 115200' interactively)")
        nonlocal_failures[0] += 1

    # ── T1b: link up on Waveshare? ────────────────────────────────────────────
    print("\n[T1b] ELRS link up on Waveshare")
    lines = read_for(1.0)
    # Look for any boat line that isn't "LINK DOWN"
    has_link = any("LQ=" in ln for ln in lines)
    if has_link:
        lq_match = re.search(r"LQ=\s*(\d+)", "\n".join(lines))
        lq = int(lq_match.group(1)) if lq_match else 0
        print(f"  PASS  link up  LQ={lq}%")
    else:
        print("  FAIL  Waveshare shows LINK DOWN (RP3 CRSF output → GPIO16 not connected)")
        nonlocal_failures[0] += 1

    # ── T2: lock + arm ────────────────────────────────────────────────────────
    print("\n[T2] lock + arm")
    send_cmd(xiao, "lock")
    send_cmd(xiao, "arm")
    time.sleep(0.15)  # allow frames to propagate

    # ── T3: rudder +0.5 ───────────────────────────────────────────────────────
    print("\n[T3] rudder +0.5")
    send_cmd(xiao, "rud 0.5")
    lines = read_for(1.2)
    assert_ok("rud≈+0.50", lines, r"rud=\+0\.[4-6]\d*")

    # ── T4: rudder -0.5 ───────────────────────────────────────────────────────
    print("\n[T4] rudder -0.5")
    send_cmd(xiao, "rud -0.5")
    lines = read_for(1.2)
    assert_ok("rud≈-0.50", lines, r"rud=-0\.[4-6]\d*")

    # ── T5: rudder centre ─────────────────────────────────────────────────────
    print("\n[T5] rudder neutral (rud 0)")
    send_cmd(xiao, "rud 0")
    lines = read_for(1.2)
    assert_ok("rud≈0.000", lines, r"rud=[+-]?0\.0[0-9]")

    # ── T6: sail 0.75 ─────────────────────────────────────────────────────────
    print("\n[T6] sail 0.75")
    send_cmd(xiao, "sail 0.75")
    lines = read_for(1.2)
    assert_ok("sail≈0.75", lines, r"sail=0\.[6-8]\d*")

    # ── T7: arm flag visible on boat ──────────────────────────────────────────
    print("\n[T7] arm=1 confirmed on boat")
    lines = read_for(0.8)
    assert_ok("arm=1", lines, r"\barm=1\b")

    # ── T8: disarm ────────────────────────────────────────────────────────────
    print("\n[T8] disarm")
    send_cmd(xiao, "disarm")
    lines = read_for(1.2)
    assert_ok("arm=0", lines, r"\barm=0\b")

    # ── T9: baud reinit — verify rx_bytes still flowing after baud change ────
    print("\n[T9] baud reinit: 420000 → 420000 (no-op), check rx_bytes still growing")
    send_cmd(xiao, "show")
    lines_pre = read_for(0.5)
    rb_pre = 0
    for ln in lines_pre:
        m = re.search(r"rx_bytes=(\d+)", ln)
        if m: rb_pre = int(m.group(1))

    send_cmd(xiao, "baud 420000")
    time.sleep(0.5)
    send_cmd(xiao, "ping")
    time.sleep(0.5)
    send_cmd(xiao, "show")
    lines_post = read_for(0.8)
    rb_post = 0
    for ln in lines_post:
        m = re.search(r"rx_bytes=(\d+)", ln)
        if m: rb_post = int(m.group(1))

    if rb_post > rb_pre:
        print(f"  PASS  rx_bytes still growing after baud reinit: {rb_pre}→{rb_post}")
    elif rb_pre == 0 and rb_post == 0:
        print("  SKIP  rx_bytes=0 both before and after (T1a already flagged this)")
    else:
        print(f"  FAIL  rx_bytes stopped after baud reinit: {rb_pre}→{rb_post}")
        nonlocal_failures[0] += 1

    # Cleanup
    send_cmd(xiao, "neutral")
    send_cmd(xiao, "unlock")

    total   = 10  # T1a, T1b, T2(no assert), T3–T8, T9
    passed  = total - nonlocal_failures[0]
    print(f"\n══════  {passed}/{total} passed  {'✓' if nonlocal_failures[0] == 0 else '✗'}  ══════\n")
    return nonlocal_failures[0]


# ── entry point ────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(
        description="ELRS debugging console — XIAO command interface + Waveshare channel viewer",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--test", action="store_true",
                        help="Run automated channel pass-through test suite (exits with #failures)")
    parser.add_argument("--xiao", default=XIAO_PORT, metavar="PORT",
                        help=f"XIAO serial port (default: {XIAO_PORT})")
    parser.add_argument("--boat", default=BOAT_PORT, metavar="PORT",
                        help=f"Waveshare serial port (default: {BOAT_PORT})")
    args = parser.parse_args()

    xiao = open_port(args.xiao, "XIAO (crsf-bridge)")
    boat = open_port(args.boat, "Waveshare (elrs-test)")

    try:
        if args.test:
            sys.exit(run_tests(xiao, boat))
        else:
            interactive(xiao, boat)
    except KeyboardInterrupt:
        print("\n[console] interrupted")
    finally:
        xiao.close()
        boat.close()


if __name__ == "__main__":
    main()
