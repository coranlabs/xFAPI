#!/bin/bash
# Simple script to run xfapi_app with config (no logging)

EXEC="./bin/xfapi_app"
CONFIG="conf/xfapi_configs.cfg"

if [ ! -f "$EXEC" ]; then
    echo "Error: Executable $EXEC not found!"
    exit 1
fi

if [ ! -f "$CONFIG" ]; then
    echo "Error: Config file $CONFIG not found!"
    exit 1
fi

# Run the binary
"$EXEC" "$CONFIG"
