#!/usr/bin/env python3
"""
Tile Downloader — pre-fetch OpenStreetMap tiles for offline use on the boat's SD card.

Saves tiles as:  <output_dir>/{z}/{x}/{y}.png
Copy the output directory to the SD card root as  tiles/
The boat firmware serves them at:  http://192.168.4.1/tiles/{z}/{x}/{y}.png

Usage: python tile_downloader.py
Requires: pip install requests
"""

import math
import os
import threading
import time
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

import requests

OSM_TILE_URL = "https://tile.openstreetmap.org/{z}/{x}/{y}.png"
RATE_LIMIT_S = 0.5   # OSM tile usage policy: no more than 2 req/s
REQUEST_TIMEOUT_S = 15
USER_AGENT = "rcsailboat-tile-downloader/1.0 (offline map tool; github.com/user/rcsailboat)"


def lat_lng_to_tile(lat_deg: float, lng_deg: float, zoom: int) -> tuple[int, int]:
    lat_rad = math.radians(lat_deg)
    n = 2 ** zoom
    x = int((lng_deg + 180.0) / 360.0 * n)
    y = int((1.0 - math.log(math.tan(lat_rad) + 1.0 / math.cos(lat_rad)) / math.pi) / 2.0 * n)
    return x, y


def count_tiles(n_lat: float, s_lat: float, w_lng: float, e_lng: float,
                z_min: int, z_max: int) -> int:
    total = 0
    for z in range(z_min, z_max + 1):
        x0, y0 = lat_lng_to_tile(n_lat, w_lng, z)
        x1, y1 = lat_lng_to_tile(s_lat, e_lng, z)
        total += (abs(x1 - x0) + 1) * (abs(y1 - y0) + 1)
    return total


class App(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("RC Sailboat — Map Tile Downloader")
        self.resizable(False, False)
        self._running = False
        self._build_ui()

    def _build_ui(self) -> None:
        PAD = 12
        frm = ttk.Frame(self, padding=PAD)
        frm.grid(row=0, column=0, sticky="nsew")

        # ── Bounding box ──────────────────────────────────────────────────────
        ttk.Label(frm, text="Bounding box (decimal degrees):").grid(
            row=0, column=0, columnspan=4, sticky="w", pady=(0, 4))

        fields = [
            ("North lat", "_north", ""),
            ("South lat", "_south", ""),
            ("West lng",  "_west",  ""),
            ("East lng",  "_east",  ""),
        ]
        for col, (label, attr, default) in enumerate(fields):
            ttk.Label(frm, text=label).grid(row=1, column=col, padx=4)
            var = tk.StringVar(value=default)
            setattr(self, attr, var)
            ttk.Entry(frm, textvariable=var, width=13).grid(row=2, column=col, padx=4)

        # ── Zoom range ────────────────────────────────────────────────────────
        ttk.Label(frm, text="Zoom levels:").grid(
            row=3, column=0, columnspan=4, sticky="w", pady=(12, 4))
        zfrm = ttk.Frame(frm)
        zfrm.grid(row=4, column=0, columnspan=4, sticky="w")
        ttk.Label(zfrm, text="Min").pack(side="left")
        self._z_min = tk.IntVar(value=14)
        ttk.Spinbox(zfrm, from_=1, to=19, textvariable=self._z_min,
                    width=5, wrap=False).pack(side="left", padx=4)
        ttk.Label(zfrm, text="Max").pack(side="left", padx=(14, 0))
        self._z_max = tk.IntVar(value=16)
        ttk.Spinbox(zfrm, from_=1, to=19, textvariable=self._z_max,
                    width=5, wrap=False).pack(side="left", padx=4)
        ttk.Label(zfrm, text="(zoom 14–16 is a good default for local sailing)",
                  foreground="gray").pack(side="left", padx=(10, 0))

        # ── Output directory ──────────────────────────────────────────────────
        ttk.Label(frm, text="Output directory:").grid(
            row=5, column=0, columnspan=4, sticky="w", pady=(12, 4))
        ofrm = ttk.Frame(frm)
        ofrm.grid(row=6, column=0, columnspan=4, sticky="ew")
        self._outdir = tk.StringVar(value="tiles")
        ttk.Entry(ofrm, textvariable=self._outdir, width=40).pack(
            side="left", expand=True, fill="x")
        ttk.Button(ofrm, text="Browse…", command=self._browse).pack(
            side="left", padx=(6, 0))

        # ── Action buttons ────────────────────────────────────────────────────
        bfrm = ttk.Frame(frm)
        bfrm.grid(row=7, column=0, columnspan=4, pady=12, sticky="ew")
        ttk.Button(bfrm, text="Count tiles",
                   command=self._count).pack(side="left", padx=(0, 6))
        self._dl_btn = ttk.Button(bfrm, text="Download",
                                  command=self._start)
        self._dl_btn.pack(side="left", padx=6)
        self._stop_btn = ttk.Button(bfrm, text="Stop",
                                    command=self._stop, state="disabled")
        self._stop_btn.pack(side="left", padx=6)

        # ── Status / progress ─────────────────────────────────────────────────
        self._status = tk.StringVar(value="Ready.")
        ttk.Label(frm, textvariable=self._status,
                  wraplength=480, justify="left").grid(
            row=8, column=0, columnspan=4, sticky="w")
        self._pbar = ttk.Progressbar(frm, length=480, mode="determinate")
        self._pbar.grid(row=9, column=0, columnspan=4, sticky="ew", pady=(6, 0))

        # ── OSM attribution note ──────────────────────────────────────────────
        ttk.Label(frm,
                  text="Tiles © OpenStreetMap contributors (openstreetmap.org/copyright)",
                  foreground="gray", font=("", 9)).grid(
            row=10, column=0, columnspan=4, sticky="w", pady=(10, 0))

    # ── Helpers ───────────────────────────────────────────────────────────────
    def _browse(self) -> None:
        d = filedialog.askdirectory(title="Select output directory for tiles")
        if d:
            self._outdir.set(d)

    def _parse_box(self) -> tuple[float, float, float, float] | None:
        try:
            n = float(self._north.get())
            s = float(self._south.get())
            w = float(self._west.get())
            e = float(self._east.get())
        except ValueError:
            messagebox.showerror("Invalid input",
                                 "Enter decimal degrees for all four coordinates.")
            return None
        if n <= s:
            messagebox.showerror("Invalid input",
                                 "North latitude must be greater than South latitude.")
            return None
        if e <= w:
            messagebox.showerror("Invalid input",
                                 "East longitude must be greater than West longitude.")
            return None
        return n, s, w, e

    def _validate_zoom(self) -> tuple[int, int] | None:
        z_min, z_max = self._z_min.get(), self._z_max.get()
        if z_min > z_max:
            messagebox.showerror("Invalid input",
                                 "Min zoom must be ≤ Max zoom.")
            return None
        return z_min, z_max

    # ── Count ─────────────────────────────────────────────────────────────────
    def _count(self) -> None:
        box = self._parse_box()
        if not box:
            return
        zoom = self._validate_zoom()
        if not zoom:
            return
        n, s, w, e = box
        z_min, z_max = zoom
        total = count_tiles(n, s, w, e, z_min, z_max)
        secs = total * RATE_LIMIT_S
        mins = secs / 60
        self._status.set(
            f"~{total:,} tiles across zoom {z_min}–{z_max}. "
            f"Estimated download time: {mins:.0f} min "
            f"(at {1/RATE_LIMIT_S:.0f} req/s per OSM policy)."
        )

    # ── Download ──────────────────────────────────────────────────────────────
    def _start(self) -> None:
        box = self._parse_box()
        if not box:
            return
        zoom = self._validate_zoom()
        if not zoom:
            return
        outdir = self._outdir.get().strip()
        if not outdir:
            messagebox.showerror("Invalid input", "Select an output directory.")
            return

        n, s, w, e = box
        z_min, z_max = zoom
        self._running = True
        self._dl_btn.config(state="disabled")
        self._stop_btn.config(state="normal")
        self._pbar["value"] = 0

        threading.Thread(
            target=self._worker,
            args=(n, s, w, e, z_min, z_max, outdir),
            daemon=True,
        ).start()

    def _stop(self) -> None:
        self._running = False

    def _worker(self, n: float, s: float, w: float, e: float,
                z_min: int, z_max: int, outdir: str) -> None:
        total = count_tiles(n, s, w, e, z_min, z_max)
        done = skipped = errors = 0
        session = requests.Session()
        session.headers["User-Agent"] = USER_AGENT

        for z in range(z_min, z_max + 1):
            if not self._running:
                break
            x0, y0 = lat_lng_to_tile(n, w, z)
            x1, y1 = lat_lng_to_tile(s, e, z)
            for x in range(min(x0, x1), max(x0, x1) + 1):
                if not self._running:
                    break
                for y in range(min(y0, y1), max(y0, y1) + 1):
                    if not self._running:
                        break

                    path = os.path.join(outdir, str(z), str(x), f"{y}.png")
                    if os.path.exists(path):
                        skipped += 1
                    else:
                        os.makedirs(os.path.dirname(path), exist_ok=True)
                        url = OSM_TILE_URL.format(z=z, x=x, y=y)
                        try:
                            resp = session.get(url, timeout=REQUEST_TIMEOUT_S)
                            resp.raise_for_status()
                            with open(path, "wb") as f:
                                f.write(resp.content)
                        except Exception as exc:
                            errors += 1
                            self._set_status(f"Error on z={z} x={x} y={y}: {exc}")
                        time.sleep(RATE_LIMIT_S)

                    done += 1
                    self._set_progress(done / total * 100 if total else 100)
                    self._set_status(
                        f"Zoom {z} — tile ({x}, {y})  "
                        f"{done}/{total}  |  {skipped} cached  |  {errors} errors"
                    )

        stopped = not self._running
        self._running = False
        self.after(0, lambda: self._finish(done, total, skipped, errors, stopped))

    def _set_status(self, msg: str) -> None:
        self.after(0, lambda: self._status.set(msg))

    def _set_progress(self, pct: float) -> None:
        self.after(0, lambda: self._pbar.__setitem__("value", pct))

    def _finish(self, done: int, total: int, skipped: int,
                errors: int, stopped: bool) -> None:
        self._dl_btn.config(state="normal")
        self._stop_btn.config(state="disabled")
        self._pbar["value"] = done / total * 100 if total else 100
        word = "Stopped" if stopped else "Done"
        self._status.set(
            f"{word}. {done}/{total} processed — "
            f"{done - skipped - errors} downloaded, "
            f"{skipped} already cached, {errors} errors."
        )


if __name__ == "__main__":
    App().mainloop()
