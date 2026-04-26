#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
pkill -f uvicorn || true
sleep 1
.venv/bin/uvicorn app.main:app --reload
