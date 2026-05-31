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

PI="${1:-${PI:-pi@rcsailboat.local}}"
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
  --exclude='tiles/' \
  "${LOCAL_SRC}/" "${PI}:${REMOTE_DIR}/"

echo "Updating Python dependencies..."
ssh "${PI}" "
  /opt/rcsailboat/venv/bin/pip install -q -r ${REMOTE_DIR}/requirements.txt
"

echo "Restarting ${SERVICE}..."
ssh "${PI}" "sudo systemctl restart ${SERVICE}"

echo ""
echo "Done. Logs:"
echo "  ssh ${PI} 'sudo journalctl -u ${SERVICE} -f'"
