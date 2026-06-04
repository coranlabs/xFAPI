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


BUILD_DIR="build"
MODE=""
SIM_MODE=""
MODE_COUNT=0
SIM_MODE_COUNT=0
VERBOSE_FLAG=""

show_help() {
    echo "Usage: $0 [OPTION]"
    echo ""
    echo "Build options (choose only one of --mode or --sim_mode):"
    echo "  --mode=<value>         Build for one of the L1+L2 integration modes:"
    echo "                         oai_osc, flexran_osc, flexran_oai, aerial_osc, ocudu_ocudu, oai_ocudu"
    echo ""
    echo "  --sim_mode=<value>     Build for one of the simulation modes:"
    echo "                         L1_SIM_OAI_OSC"
    echo "                         L1_SIM_OAI_OAI"
    echo "                         L1_SIM_FRAN_OSC"
    echo "                         L1_SIM_FRAN_OAI"
    echo "                         L2_SIM_OAI_OAI"
    echo "                         L2_SIM_OAI_FRAN"
    echo "                         L2_SIM_OSC_OAI"
    echo "                         L2_SIM_OSC_FRAN"
    echo ""
    echo "Utility options:"
    echo "  --clean                Remove the build and bin directories"
    echo "  -v, --verbose          Enable verbose make output"
    echo "  -h, --help             Show this help message and exit"
    echo ""
    exit 0
}

# Clean build directory
if [[ "$1" == "--clean" ]]; then
    echo "Cleaning build and bin directories..."
    rm -rf "$BUILD_DIR"
    rm -rf bin/
    echo "Clean complete."
    exit 0
fi

# Parse arguments
for arg in "$@"; do
    case $arg in
        --mode=oai_osc)
            MODE="-DOAI_OSC=ON"
            ((MODE_COUNT++))
            ;;
        --mode=flexran_osc)
            MODE="-DFLEXRAN_OSC=ON"
            ((MODE_COUNT++))
            ;;
        --mode=flexran_oai)
            MODE="-DFLEXRAN_OAI=ON"
            ((MODE_COUNT++))
            ;;
        --mode=aerial_osc)
            MODE="-DAERIAL_OSC=ON"
            ((MODE_COUNT++))
            ;;
        --mode=ocudu_ocudu)
            MODE="-DOCUDU_OCUDU=ON"
            ((MODE_COUNT++))
            ;;
        --mode=oai_ocudu)
            MODE="-DOAI_OCUDU=ON"
            ((MODE_COUNT++))
            ;;
        --sim_mode=L1_SIM_OAI_OSC)
            SIM_MODE="-DL1_SIM_OAI_OSC=ON"
            ((SIM_MODE_COUNT++))
            ;;
        --sim_mode=L1_SIM_OAI_OAI)
            SIM_MODE="-DL1_SIM_OAI_OAI=ON"
            ((SIM_MODE_COUNT++))
            ;;
        --sim_mode=L1_SIM_FRAN_OSC)
            SIM_MODE="-DL1_SIM_FRAN_OSC=ON"
            ((SIM_MODE_COUNT++))
            ;;
        --sim_mode=L1_SIM_FRAN_OAI)
            SIM_MODE="-DL1_SIM_FRAN_OAI=ON"
            ((SIM_MODE_COUNT++))
            ;;
        --sim_mode=L2_SIM_OAI_OAI)
            SIM_MODE="-DL2_SIM_OAI_OAI=ON"
            ((SIM_MODE_COUNT++))
            ;;
        --sim_mode=L2_SIM_OAI_FRAN)
            SIM_MODE="-DL2_SIM_OAI_FRAN=ON"
            ((SIM_MODE_COUNT++))
            ;;
        --sim_mode=L2_SIM_OSC_OAI)
            SIM_MODE="-DL2_SIM_OSC_OAI=ON"
            ((SIM_MODE_COUNT++))
            ;;
        --sim_mode=L2_SIM_OSC_FRAN)
            SIM_MODE="-DL2_SIM_OSC_FRAN=ON"
            ((SIM_MODE_COUNT++))
            ;;
        -v|--verbose)
            VERBOSE_FLAG="VERBOSE=1"
            ;;
        -h|--help)
            show_help
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Use --help to see available options."
            exit 1
            ;;
    esac
done

# Validation: Only one category allowed
if [ "$MODE_COUNT" -gt 0 ] && [ "$SIM_MODE_COUNT" -gt 0 ]; then
    echo "Error: You can only specify one of --mode or --sim_mode."
    exit 1
fi

if [ "$MODE_COUNT" -gt 1 ] || [ "$SIM_MODE_COUNT" -gt 1 ]; then
    echo "Error: You can only specify one value for --mode or --sim_mode."
    exit 1
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR" || exit 1

# Build
if [ -n "$MODE" ]; then
    echo "Building with mode: $MODE"
    cmake .. $MODE
elif [ -n "$SIM_MODE" ]; then
    echo "Building with simulation mode: $SIM_MODE"
    cmake .. $SIM_MODE
else
    echo "No build mode specified. Run with --help for usage."
    exit 1
fi

# Compile
make -j$(nproc) $VERBOSE_FLAG
