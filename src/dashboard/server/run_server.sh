#!/bin/bash

# XFAPI Dashboard server launcher.
#
# - Installs Python deps from requirements.txt
# - Builds the React client (Vite) if dist/ is missing
# - Starts the FastAPI server on http://0.0.0.0:8080

set -e
cd "$(dirname "$0")"

echo "[xfapi] Checking Python 3 + pip3 …"
command -v python3 >/dev/null 2>&1 || { echo "Error: python3 not in PATH"; exit 1; }
command -v pip3    >/dev/null 2>&1 || { echo "Error: pip3 not in PATH";    exit 1; }

echo "[xfapi] Installing Python dependencies …"
pip3 install --quiet -r requirements.txt

CLIENT_DIR="$(cd ../client && pwd)"
DIST_DIR="$CLIENT_DIR/dist"

if [ ! -f "$DIST_DIR/index.html" ]; then
  echo "[xfapi] React client not built — building now …"
  command -v npm >/dev/null 2>&1 || { echo "Error: npm not in PATH (needed to build the client)"; exit 1; }
  ( cd "$CLIENT_DIR" && [ -d node_modules ] || npm install --silent )
  ( cd "$CLIENT_DIR" && npm run build )
fi

echo "[xfapi] Starting dashboard on http://0.0.0.0:8080"
echo "[xfapi] Press Ctrl+C to stop"
python3 main.py
