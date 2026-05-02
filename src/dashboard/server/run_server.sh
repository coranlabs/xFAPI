#!/bin/bash

# Copyright 2024-2026 coRAN LABS Private Limited
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

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
