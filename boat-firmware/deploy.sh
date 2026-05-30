#!/usr/bin/env bash
# Build and flash via the workbench portal.
# Usage: ./deploy.sh [env]   (default: esp32-s3)
set -euo pipefail

ENV="${1:-esp32-s3}"
WORKBENCH="${WORKBENCH_HOST:-http://workbench.local:8080}"
BUILD_DIR="$HOME/.cache/pio-build/rcsailboat/$ENV"

cd "$(dirname "$0")"

echo "==> Building env: $ENV"
pio run -e "$ENV"

echo ""
echo "==> Discovering slot on $WORKBENCH ..."
SLOT=$(curl -s "$WORKBENCH/api/devices" | \
    python3 -c "import sys,json; d=json.load(sys.stdin); s=d['slots']; print(s[0]['label']) if s else (print('ERROR: no slots',file=sys.stderr) or sys.exit(1))")
echo "    slot: $SLOT"

echo ""
echo "==> Flashing via $WORKBENCH (slot=$SLOT)"
echo "    bin dir: $BUILD_DIR"

# ESP32-S3 offsets: bootloader at 0x0000 (S3/C3/C6/H2 — not 0x1000 like classic ESP32).
# flash_mode=dio: QIO crashes the ROM loader on this board's Winbond W25Q128.
# flash_size=16MB: explicit — "keep" causes esptool to write 8 MB params.
result=$(curl -s -X POST "$WORKBENCH/api/flash" \
    -F "slot=$SLOT" \
    -F "chip=esp32s3" \
    -F "baud=921600" \
    -F "flash_mode=dio" \
    -F "flash_size=16MB" \
    -F "bin@0x0000=@$BUILD_DIR/bootloader.bin" \
    -F "bin@0x8000=@$BUILD_DIR/partitions.bin" \
    -F "bin@0x10000=@$BUILD_DIR/firmware.bin")

echo "$result" | python3 -c "
import sys, json
d = json.load(sys.stdin)
print(d.get('output', ''))
sys.exit(0 if d.get('ok') else 1)
"
