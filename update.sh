#!/usr/bin/env bash
set -euo pipefail

cd ~/Documents/Projects/rcsailboat
git pull
sudo bash base-station/scripts/install.sh
sudo systemctl restart rcsailboat-base
