#!/usr/bin/env bash
# install.sh — set up the RC Sailboat base station on the Raspberry Pi.
#
# Run once on the Pi (as root or with sudo) from inside the cloned repo:
#   sudo base-station/scripts/install.sh
#
# What it does:
#   1. Installs system dependencies (python3-venv, python3-dev)
#   2. Copies base-station/ to /opt/rcsailboat/base-station/
#   3. Creates a Python venv at /opt/rcsailboat/venv/ and installs deps
#   4. Writes /etc/rcsailboat.env with default settings (edit after running)
#   5. Installs and enables the rcsailboat-base.service systemd unit
#
# After running:
#   - Edit /etc/rcsailboat.env to set the correct serial ports
#   - Run setup-udev.sh to create /dev/elrs-tx and /dev/esp32-debug
#   - sudo systemctl start rcsailboat-base

set -euo pipefail

INSTALL_DIR="/opt/rcsailboat"
APP_DIR="${INSTALL_DIR}/base-station"
VENV_DIR="${INSTALL_DIR}/venv"
ENV_FILE="/etc/rcsailboat.env"
SERVICE_NAME="rcsailboat-base"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"

# Resolve the repo root relative to this script
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_BASE_STATION="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Detect the real user behind sudo (falls back to current user if run as root directly)
REAL_USER="${SUDO_USER:-${USER}}"
REAL_GROUP="$(id -gn "${REAL_USER}" 2>/dev/null || echo "${REAL_USER}")"

if [[ $EUID -ne 0 ]]; then
  echo "Run as root: sudo $0"
  exit 1
fi

echo "=== RC Sailboat Base Station Installer ==="
echo "  Source : ${REPO_BASE_STATION}"
echo "  Install: ${APP_DIR}"
echo ""

# ── 1. System dependencies ────────────────────────────────────────────────────
echo "[1/5] Installing system dependencies..."
apt-get install -y python3-venv python3-dev >/dev/null

# ── 2. Copy application files ─────────────────────────────────────────────────
echo "[2/5] Copying application files to ${APP_DIR}..."
mkdir -p "${APP_DIR}"
rsync -a --delete \
  --exclude='.venv' \
  --exclude='__pycache__' \
  --exclude='*.pyc' \
  --exclude='*.egg-info' \
  --exclude='tiles/' \
  "${REPO_BASE_STATION}/" "${APP_DIR}/"
chown -R "${REAL_USER}:${REAL_GROUP}" "${INSTALL_DIR}"

# ── 3. Python venv + dependencies ─────────────────────────────────────────────
echo "[3/5] Creating Python venv and installing requirements..."
if [[ ! -d "${VENV_DIR}" ]]; then
  python3 -m venv "${VENV_DIR}"
fi
"${VENV_DIR}/bin/pip" install -q --upgrade pip
"${VENV_DIR}/bin/pip" install -q -r "${APP_DIR}/requirements.txt"
chown -R "${REAL_USER}:${REAL_GROUP}" "${VENV_DIR}"

# ── 4. Environment file ───────────────────────────────────────────────────────
echo "[4/5] Writing environment file ${ENV_FILE}..."
if [[ -f "${ENV_FILE}" ]]; then
  echo "  (${ENV_FILE} already exists — skipping to preserve existing config)"
else
  cat > "${ENV_FILE}" <<'EOF'
# RC Sailboat base station environment variables.
# Edit these to match your actual serial port devices.
# Use setup-udev.sh to create stable /dev/elrs-tx and /dev/esp32-debug symlinks.

ELRS_PORT=/dev/elrs-tx
ELRS_BAUD=400000

# Remove or comment out ESP32_DBG_PORT to disable the USB console tab.
ESP32_DBG_PORT=/dev/esp32-debug
ESP32_DBG_BAUD=115200

# Shared passphrase required on WebSocket connect.
# Set the same value in the web UI tweaks panel (Connection → Access token).
# Leave empty to disable authentication (not recommended on a shared network).
RCSB_TOKEN=changeme
EOF
  echo "  Created. Edit ${ENV_FILE} to set the correct serial ports."
fi

# ── 5. Systemd service ────────────────────────────────────────────────────────
echo "[5/5] Installing systemd service..."
# Substitute the placeholder user with the detected real user
sed "s/^User=pi$/User=${REAL_USER}/" \
  "${REPO_BASE_STATION}/rcsailboat-base.service" > "${SERVICE_FILE}"
systemctl daemon-reload
systemctl enable "${SERVICE_NAME}"

echo ""
echo "=== Installation complete ==="
echo ""
echo "Next steps:"
echo "  1. sudo base-station/scripts/setup-udev.sh   # stable device names"
echo "  2. sudo nano ${ENV_FILE}                      # verify port names"
echo "  3. sudo systemctl start ${SERVICE_NAME}       # start the server"
echo "  4. sudo journalctl -u ${SERVICE_NAME} -f      # watch logs"
echo ""
echo "The web UI will be at http://$(hostname -I | awk '{print $1}'):8000/"
