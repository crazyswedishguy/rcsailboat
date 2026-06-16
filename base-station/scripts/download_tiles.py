#!/usr/bin/env python3
"""
download_tiles.py — download map tile images for offline use.

Run this once at home (with internet access) before going to the field.
Tiles are saved under base-station/static/tiles/{z}/{x}/{y}.png and served
by the FastAPI app at /tiles/{z}/{x}/{y}.png.

Usage examples:
  # NOAA nautical charts (recommended for sailing — shows depths, channels, hazards)
  python scripts/download_tiles.py --lat 40.948 --lon -73.069 --radius 20.0 --source noaa

  # OpenStreetMap (requires prior permission / your own mirror for bulk downloads)
  python scripts/download_tiles.py --lat 40.948 --lon -73.069 --radius 20.0 --source osm

  # Fetch Leaflet library files too
  python scripts/download_tiles.py --lat 40.948 --lon -73.069 --radius 20.0 --source noaa --fetch-leaflet

OSM tile usage policy: https://operations.osmfoundation.org/policies/tiles/
NOAA chart tiles: https://tileservice.charts.noaa.gov/
"""
import argparse
import math
import struct
import time
import urllib.request
from pathlib import Path

# ── Tile sources ──────────────────────────────────────────────────────────────

SOURCES = {
    "carto": {
        # CartoDB Voyager — street/terrain style, no API key, allows reasonable bulk use.
        # Uses subdomains a-d for parallel fetching (single-threaded here, just 'a').
        "url":        "https://a.basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}.png",
        "user_agent": "rcsailboat-tile-downloader/1.0 (offline sailing map; contact andreas.baum@gmail.com)",
        "rate_s":     0.5,
        "min_zoom":   12,
        "max_zoom":   16,
    },
    "noaa": {
        # NOAA nautical chart tiles — best for sailing but server is sometimes unreachable.
        "url":        "https://tileservice.charts.noaa.gov/tiles/50000_1/{z}/{x}/{y}.png",
        "user_agent": "rcsailboat-tile-downloader/1.0 (offline sailing map; contact andreas.baum@gmail.com)",
        "rate_s":     0.5,
        "min_zoom":   12,
        "max_zoom":   16,
    },
    "osm": {
        # OpenStreetMap — requires prior approval for bulk downloads; Pi IP may be blocked.
        "url":        "https://tile.openstreetmap.org/{z}/{x}/{y}.png",
        "user_agent": "rcsailboat-tile-downloader/1.0 (offline sailing map; contact andreas.baum@gmail.com)",
        "rate_s":     1.5,   # OSM policy: ≤1 req/s; use 1.5 to be safe
        "min_zoom":   12,
        "max_zoom":   16,
    },
}

SCRIPT_DIR = Path(__file__).resolve().parent
TILES_DIR  = SCRIPT_DIR.parent / "static" / "tiles"

LEAFLET_VERSION = "1.9.4"
LEAFLET_BASE    = f"https://cdnjs.cloudflare.com/ajax/libs/leaflet/{LEAFLET_VERSION}"
LEAFLET_FILES   = {
    "leaflet.min.js":  f"{LEAFLET_BASE}/leaflet.min.js",
    "leaflet.min.css": f"{LEAFLET_BASE}/leaflet.min.css",
}
LEAFLET_DIR = SCRIPT_DIR.parent / "static" / "leaflet"

# PNG magic bytes
PNG_MAGIC = b'\x89PNG\r\n\x1a\n'


# ── Tile coordinate math ──────────────────────────────────────────────────────

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
    # Northern lat → smaller tile-Y (top of map); southern lat → larger tile-Y.
    x_min, y_min = lat_lon_to_tile(lat + lat_delta, lon - lon_delta, zoom)
    x_max, y_max = lat_lon_to_tile(lat - lat_delta, lon + lon_delta, zoom)
    return x_min, y_min, x_max, y_max


def count_tiles(lat: float, lon: float, radius_km: float, min_zoom: int, max_zoom: int) -> int:
    total = 0
    for z in range(min_zoom, max_zoom + 1):
        x0, y0, x1, y1 = tile_bbox(lat, lon, radius_km, z)
        total += (x1 - x0 + 1) * (y1 - y0 + 1)
    return total


# ── Download helpers ──────────────────────────────────────────────────────────

def _get(url: str, user_agent: str, timeout: int = 20) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": user_agent})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read()


def _is_valid_tile(data: bytes) -> bool:
    """Return True if data is a valid PNG tile (not an error page or 1×1 stub)."""
    if not data.startswith(PNG_MAGIC):
        return False
    # PNG IHDR starts at byte 16: 4B width, 4B height
    if len(data) < 24:
        return False
    w = struct.unpack('>I', data[16:20])[0]
    h = struct.unpack('>I', data[20:24])[0]
    # Reject 1×1 error stubs; accept any larger tile including small solid-colour ocean tiles
    return w >= 4 and h >= 4


def download_tile(z: int, x: int, y: int, url_tmpl: str, user_agent: str) -> str:
    """Download one tile; return 'fetched', 'cached', or 'error:<reason>'."""
    out = TILES_DIR / str(z) / str(x) / f"{y}.png"
    if out.exists():
        return "cached"
    out.parent.mkdir(parents=True, exist_ok=True)
    data = _get(url_tmpl.format(z=z, x=x, y=y), user_agent)
    if not _is_valid_tile(data):
        return f"error:invalid tile ({len(data)} bytes)"
    out.write_bytes(data)
    return "fetched"


def fetch_leaflet() -> None:
    """Download Leaflet.js library files to static/leaflet/ for offline serving."""
    LEAFLET_DIR.mkdir(parents=True, exist_ok=True)
    dummy_ua = "rcsailboat-tile-downloader/1.0"
    for filename, url in LEAFLET_FILES.items():
        dest = LEAFLET_DIR / filename
        if dest.exists():
            print(f"  [cached] {filename}")
            continue
        print(f"  Downloading {filename} ...", end="", flush=True)
        dest.write_bytes(_get(url, dummy_ua))
        print(" done")


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--lat",      type=float, required=True)
    parser.add_argument("--lon",      type=float, required=True)
    parser.add_argument("--radius",   type=float, default=20.0,
                        help="Radius in km (default: 20.0)")
    parser.add_argument("--min-zoom", type=int,   default=None,
                        help="Override minimum zoom (default: source default)")
    parser.add_argument("--max-zoom", type=int,   default=None,
                        help="Override maximum zoom (default: source default)")
    parser.add_argument("--source",   choices=list(SOURCES.keys()), default="carto",
                        help="Tile source (default: noaa)")
    parser.add_argument("--fetch-leaflet", action="store_true",
                        help="Also download Leaflet.js and leaflet.css")
    args = parser.parse_args()

    src = SOURCES[args.source]
    min_zoom = args.min_zoom if args.min_zoom is not None else src["min_zoom"]
    max_zoom = args.max_zoom if args.max_zoom is not None else src["max_zoom"]

    if args.fetch_leaflet:
        print("Fetching Leaflet.js library files ...")
        fetch_leaflet()
        print()

    total = count_tiles(args.lat, args.lon, args.radius, min_zoom, max_zoom)
    eta_min = round(total * src["rate_s"] / 60)
    print(
        f"Tile download plan: centre {args.lat:.5f},{args.lon:.5f}  "
        f"radius={args.radius} km  zoom {min_zoom}–{max_zoom}  source={args.source}"
    )
    print(f"Up to {total} tiles  |  ~{eta_min} min at {src['rate_s']}s/tile  |  output: {TILES_DIR}")
    print(f"Tiles that fail validation are skipped and can be retried.\n")

    fetched = 0
    skipped = 0
    errors  = 0
    done    = 0

    for z in range(min_zoom, max_zoom + 1):
        x0, y0, x1, y1 = tile_bbox(args.lat, args.lon, args.radius, z)
        ztotal = (x1 - x0 + 1) * (y1 - y0 + 1)
        zfetched = 0
        print(f"  zoom {z:2d}: {ztotal:5d} tiles  ({x1-x0+1} × {y1-y0+1})", flush=True)
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                try:
                    result = download_tile(z, x, y, src["url"], src["user_agent"])
                    if result == "fetched":
                        fetched += 1
                        zfetched += 1
                        time.sleep(src["rate_s"])
                    elif result == "cached":
                        skipped += 1
                    else:
                        errors += 1
                        print(f"    SKIP z={z} x={x} y={y}: {result}", flush=True)
                except Exception as exc:
                    errors += 1
                    print(f"    WARN z={z} x={x} y={y}: {exc}", flush=True)
                done += 1
                if done % 100 == 0:
                    print(f"    {done}/{total}  fetched={fetched} skip={skipped} err={errors}", flush=True)

    print(f"\nDone. Fetched {fetched}, skipped {skipped} (cached), {errors} invalid/errors.")
    if errors:
        print("Re-run to retry failed tiles — already-downloaded tiles will be skipped.")


if __name__ == "__main__":
    main()
