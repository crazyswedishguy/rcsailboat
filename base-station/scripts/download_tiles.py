#!/usr/bin/env python3
"""
download_tiles.py — download OpenStreetMap tile images for offline use.

Run this once at home (with internet access) before going to the field.
Tiles are saved under base-station/static/tiles/{z}/{x}/{y}.png and served
by the FastAPI app at /tiles/{z}/{x}/{y}.png.

Usage examples:
  python scripts/download_tiles.py --lat 51.500 --lon -0.100 --radius 2.0
  python scripts/download_tiles.py --lat 51.500 --lon -0.100 --radius 2.0 \\
      --min-zoom 14 --max-zoom 17 --fetch-leaflet

OSM tile usage policy: https://operations.osmfoundation.org/policies/tiles/
  - Identify yourself via the User-Agent header  (done below)
  - Do not make more than ~1 request per second   (enforced below)
  - Cache aggressively — do not re-download tiles that already exist
"""
import argparse
import math
import time
import urllib.request
from pathlib import Path

TILE_URL = "https://tile.openstreetmap.org/{z}/{x}/{y}.png"
USER_AGENT = "rcsailboat-tile-downloader/1.0 (offline map for RC sailboat project)"
RATE_LIMIT_S = 1.1  # seconds between tile requests — stays within OSM policy

SCRIPT_DIR = Path(__file__).resolve().parent
TILES_DIR  = SCRIPT_DIR.parent / "static" / "tiles"

LEAFLET_VERSION = "1.9.4"
LEAFLET_BASE    = f"https://unpkg.com/leaflet@{LEAFLET_VERSION}/dist"
LEAFLET_FILES   = {
    "leaflet.min.js":  f"{LEAFLET_BASE}/leaflet.min.js",
    "leaflet.min.css": f"{LEAFLET_BASE}/leaflet.min.css",
}
LEAFLET_DIR = SCRIPT_DIR.parent / "static" / "leaflet"


# ── Tile coordinate math ────────────────────────────────────────────────────────

def lat_lon_to_tile(lat: float, lon: float, zoom: int) -> tuple[int, int]:
    """Convert WGS-84 lat/lon to OSM tile x/y at the given zoom level."""
    lat_r = math.radians(lat)
    n = 2 ** zoom
    x = int((lon + 180) / 360 * n)
    y = int((1 - math.log(math.tan(lat_r) + 1 / math.cos(lat_r)) / math.pi) / 2 * n)
    return x, y


def tile_bbox(
    lat: float, lon: float, radius_km: float, zoom: int
) -> tuple[int, int, int, int]:
    """Return (x_min, y_min, x_max, y_max) tile range covering a lat/lon circle."""
    lat_delta = radius_km / 111.0
    lon_delta = radius_km / (111.0 * math.cos(math.radians(lat)))
    x_min, y_max = lat_lon_to_tile(lat + lat_delta, lon - lon_delta, zoom)
    x_max, y_min = lat_lon_to_tile(lat - lat_delta, lon + lon_delta, zoom)
    return x_min, y_min, x_max, y_max


def count_tiles(lat: float, lon: float, radius_km: float, min_zoom: int, max_zoom: int) -> int:
    total = 0
    for z in range(min_zoom, max_zoom + 1):
        x0, y0, x1, y1 = tile_bbox(lat, lon, radius_km, z)
        total += (x1 - x0 + 1) * (y1 - y0 + 1)
    return total


# ── Download helpers ────────────────────────────────────────────────────────────

def _get(url: str, timeout: int = 20) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read()


def download_tile(z: int, x: int, y: int) -> bool:
    """Download one tile; return True if fetched, False if already cached."""
    out = TILES_DIR / str(z) / str(x) / f"{y}.png"
    if out.exists():
        return False
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(_get(TILE_URL.format(z=z, x=x, y=y)))
    return True


def fetch_leaflet() -> None:
    """Download Leaflet.js library files to static/leaflet/ for offline serving."""
    LEAFLET_DIR.mkdir(parents=True, exist_ok=True)
    for filename, url in LEAFLET_FILES.items():
        dest = LEAFLET_DIR / filename
        if dest.exists():
            print(f"  [cached] {filename}")
            continue
        print(f"  Downloading {filename} ...", end="", flush=True)
        dest.write_bytes(_get(url))
        print(" done")


# ── Main ────────────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--lat",      type=float, required=True,
                        help="Centre latitude in decimal degrees")
    parser.add_argument("--lon",      type=float, required=True,
                        help="Centre longitude in decimal degrees")
    parser.add_argument("--radius",   type=float, default=2.0,
                        help="Radius in km around the centre point (default: 2.0)")
    parser.add_argument("--min-zoom", type=int,   default=12,
                        help="Minimum (most zoomed-out) zoom level to download (default: 12)")
    parser.add_argument("--max-zoom", type=int,   default=17,
                        help="Maximum (most zoomed-in) zoom level to download (default: 17)")
    parser.add_argument("--fetch-leaflet", action="store_true",
                        help="Also download Leaflet.js and leaflet.css to static/leaflet/")
    args = parser.parse_args()

    if args.fetch_leaflet:
        print("Fetching Leaflet.js library files ...")
        fetch_leaflet()
        print()

    total = count_tiles(args.lat, args.lon, args.radius, args.min_zoom, args.max_zoom)
    print(
        f"Tile download plan: centre {args.lat:.5f},{args.lon:.5f}  "
        f"radius={args.radius} km  zoom {args.min_zoom}–{args.max_zoom}"
    )
    print(f"Up to {total} tiles  |  output: {TILES_DIR}")
    print(f"Rate-limited to 1 request per {RATE_LIMIT_S}s — please be patient.\n")

    fetched = 0
    skipped = 0
    errors  = 0

    for z in range(args.min_zoom, args.max_zoom + 1):
        x0, y0, x1, y1 = tile_bbox(args.lat, args.lon, args.radius, z)
        ztotal = (x1 - x0 + 1) * (y1 - y0 + 1)
        print(f"  zoom {z:2d}: {ztotal:5d} tiles  ({x1-x0+1} × {y1-y0+1})", flush=True)
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                try:
                    if download_tile(z, x, y):
                        fetched += 1
                        time.sleep(RATE_LIMIT_S)
                    else:
                        skipped += 1
                except Exception as exc:
                    errors += 1
                    print(f"    WARN z={z} x={x} y={y}: {exc}")

    print(f"\nDone. Fetched {fetched}, skipped {skipped} (cached), {errors} errors.")
    if errors:
        print("Re-run to retry failed tiles — already-downloaded tiles will be skipped.")


if __name__ == "__main__":
    main()
