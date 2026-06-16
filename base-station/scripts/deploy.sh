#!/usr/bin/env bash
# deploy.sh — push base station updates to the Pi and restart the service.
#
# Run from the repo root on your dev machine:
#   base-station/scripts/deploy.sh [user@host]
#
# Default target: pi@rcsailboat.local
# Override:       PI=pi@192.168.1.42 base-station/scripts/deploy.sh
#
# Requires: ssh key auth set up to the Pi (ssh-copy-id pi@rcsailboat.local)
# First-time setup: run install.sh on the Pi first.

set -euo pipefail

PI="${1:-${PI:-asb@rcsailboat.local}}"
REMOTE_DIR="/opt/rcsailboat/base-station"
SERVICE="rcsailboat-base"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOCAL_SRC="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "Deploying to ${PI}:${REMOTE_DIR}..."

rsync -az --delete \
  --exclude='.venv' \
  --exclude='__pycache__' \
  --exclude='*.pyc' \
  --exclude='*.egg-info' \
  --exclude='static/tiles/' \
  --exclude='static/leaflet/' \
  "${LOCAL_SRC}/" "${PI}:${REMOTE_DIR}/"

# Restore the tiles symlink — rsync replaces it with an empty real dir even with --exclude.
# Tiles live in the git repo working tree and are served via symlink so they don't need
# to be copied on every deploy.
ssh "${PI}" "
  TILES_LINK=${REMOTE_DIR}/static/tiles
  TILES_REAL=\$HOME/Documents/Projects/rcsailboat/base-station/static/tiles
  if [ ! -L \"\$TILES_LINK\" ]; then
    rm -rf \"\$TILES_LINK\"
    ln -s \"\$TILES_REAL\" \"\$TILES_LINK\"
    echo 'Restored tiles symlink.'
  fi
"

# Deploy Leaflet library files (not tracked by git — downloaded by download_tiles.py --fetch-leaflet).
# If the local copy is absent, copy from the Pi's own repo copy (which may already have them).
LEAFLET_SRC="${LOCAL_SRC}/static/leaflet"
if [ -f "${LEAFLET_SRC}/leaflet.min.js" ]; then
  rsync -az "${LEAFLET_SRC}/" "${PI}:${REMOTE_DIR}/static/leaflet/"
else
  ssh "${PI}" "
    SRC=\$HOME/Documents/Projects/rcsailboat/base-station/static/leaflet
    DEST=${REMOTE_DIR}/static/leaflet
    [ -f \"\$SRC/leaflet.min.js\" ] && cp \"\$SRC/leaflet.min.js\" \"\$SRC/leaflet.min.css\" \"\$DEST/\" || true
  "
fi

echo "Updating Python dependencies..."
ssh "${PI}" "
  /opt/rcsailboat/venv/bin/pip install -q -r ${REMOTE_DIR}/requirements.txt
"

echo "Restarting ${SERVICE}..."
ssh "${PI}" "sudo systemctl restart ${SERVICE}"

echo ""
echo "Done. Logs:"
echo "  ssh ${PI} 'sudo journalctl -u ${SERVICE} -f'"
