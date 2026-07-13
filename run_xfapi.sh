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


# --- Configuration Paths ---
MAIN_CONFIG_FILE="conf/aerial_oai_config.yaml"
SIM_CONFIG_FILE=""
# --- End Configuration Paths ---

BIN_PATH="bin/xfapi_main"

show_help() {
    echo "Usage: ./run_xfapi.sh"
    echo ""
    echo "This script runs the xFAPI application using configuration paths"
    echo "defined directly within the script itself."
    echo ""
    echo "Configuration files can be updated by modifying the 'MAIN_CONFIG_FILE'"
    echo "and 'SIM_CONFIG_FILE' variables at the top of this script."
    echo ""
    echo "Options:"
    echo "  -h, --help     Show this help message and exit"
}

# Handle -h or --help
if [[ "$1" == "-h" || "$1" == "--help" ]]; then
    show_help
    exit 0
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "Error: xfapi_main must run as root (DPDK PA-mode IOVA and /dev/hugepages)."
    echo "Re-run with: sudo $0"
    exit 1
fi

# Check if binary exists
if [ ! -f "$BIN_PATH" ]; then
    echo "Error: '$BIN_PATH' not found. Please build the project first using './build.sh'."
    exit 1
fi

# Check if main config file exists
if [ ! -f "$MAIN_CONFIG_FILE" ]; then
    echo "Error: Main configuration file '$MAIN_CONFIG_FILE' not found."
    exit 1
fi

# Construct the command for xfapi_main
# Start with the binary and the mandatory main config file
COMMAND="$BIN_PATH --cfgfile \"$MAIN_CONFIG_FILE\""

# If a simulation config file path is provided, append it to the command
if [ -n "$SIM_CONFIG_FILE" ]; then
    # Check if sim config file exists before adding to command
    if [ ! -f "$SIM_CONFIG_FILE" ]; then
        echo "Error: Simulation configuration file '$SIM_CONFIG_FILE' is specified but not found."
        exit 1
    fi
    COMMAND="$COMMAND --simcfg \"$SIM_CONFIG_FILE\""
fi

# Run the binary
echo "Running xfapi_main command: $COMMAND"
eval $COMMAND # Use eval to correctly handle quoted paths within the command
