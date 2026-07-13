#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Sync the OAI nFAPI sources into this repo's src/ipc/nfapi tree.

Usage:
  sync_nfapi.sh [-n] [-v] [OAI_DIR]
  sync_nfapi.sh -h | --help

Arguments:
  OAI_DIR        Path to an OAI checkout (contains an nfapi/ directory).
                 May also be set via the $OAI_DIR environment variable.

Options:
  -n             Dry run — list what would be copied, write nothing.
  -v             Verbose — print each copied file.
  -h, --help     Show this help and exit.

Environment:
  OAI_DIR        Default OAI checkout path (overridden by the positional arg).
  NFAPI_DIR      Destination tree (default: <script dir>/src/ipc/nfapi).

Examples:
  sync_nfapi.sh /home/user/openairinterface
  OAI_DIR=/opt/oai sync_nfapi.sh -v
  sync_nfapi.sh -n /opt/oai
EOF
}

# Handle long --help before getopts (getopts only parses short options).
for arg in "$@"; do
  case "$arg" in
    --help) usage; exit 0 ;;
  esac
done

DRY_RUN=0
VERBOSE=0
while getopts "nvh" opt; do
  case "$opt" in
    n) DRY_RUN=1 ;;
    v) VERBOSE=1 ;;
    h) usage; exit 0 ;;
    *) echo "unknown option" >&2; echo >&2; usage >&2; exit 2 ;;
  esac
done
shift $((OPTIND - 1))

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NFAPI_DIR="${NFAPI_DIR:-$SCRIPT_DIR/src/ipc/nfapi}"
OAI="${1:-${OAI_DIR:-}}"

[[ -n "$OAI" ]]        || { echo "error: OAI dir not set — pass it as the first argument or via \$OAI_DIR" >&2; echo >&2; usage >&2; exit 1; }
[[ -d "$OAI" ]]        || { echo "error: OAI dir not found: $OAI" >&2; exit 1; }
[[ -d "$OAI/nfapi" ]]  || { echo "error: '$OAI/nfapi' not found — is this an OAI checkout?" >&2; exit 1; }

# Create the destination tree if it does not exist yet.
if [[ ! -d "$NFAPI_DIR" ]]; then
  echo "nfapi dest not found — creating: $NFAPI_DIR"
  [[ $DRY_RUN -eq 1 ]] || mkdir -p "$NFAPI_DIR"
fi

echo "OAI source : $OAI"
echo "nfapi dest : $NFAPI_DIR"
[[ $DRY_RUN -eq 1 ]] && echo "(dry run — no files will be written)"
echo

copied=0
missing=0
declare -a missing_list=()

# Copy one source -> dest (relative) pair.
copy_one() {
  local src="$1" rel="$2"
  if [[ ! -f "$src" ]]; then
    missing=$((missing + 1))
    missing_list+=("$rel  <-  ${src#"$OAI"/}")
    return
  fi
  local dest="$NFAPI_DIR/$rel"
  if [[ $DRY_RUN -eq 1 ]]; then
    [[ $VERBOSE -eq 1 ]] && echo "would copy  $rel"
  else
    mkdir -p "$(dirname "$dest")"
    cp -p "$src" "$dest"
    [[ $VERBOSE -eq 1 ]] && echo "copied      $rel"
  fi
  copied=$((copied + 1))
}

# 1) Mirror the whole OAI nfapi/ tree.
while IFS= read -r -d '' src; do
  rel="${src#"$OAI"/nfapi/}"
  copy_one "$src" "$rel"
done < <(find "$OAI/nfapi" -type f -print0)

# 2) The local oai_common/ folder, gathered from scattered OAI locations.
copy_one "$OAI/common/platform_types.h"      "oai_common/common/platform_types.h"
copy_one "$OAI/common/utils/assertions.h"    "oai_common/common/utils/assertions.h"
copy_one "$OAI/common/utils/ds/byte_array.h" "oai_common/common/utils/ds/byte_array.h"
copy_one "$OAI/common/utils/nr/nr_common.h"  "oai_common/common/utils/nr/nr_common.h"
copy_one "$OAI/common/utils/utils.h"         "oai_common/common/utils/utils.h"
copy_one "$OAI/common/utils/minimal_stub.c"  "oai_common/minimal_stub.c"

echo
echo "Summary: $copied file(s) $([[ $DRY_RUN -eq 1 ]] && echo 'to copy' || echo 'copied'), $missing missing in OAI"
if [[ $missing -gt 0 ]]; then
  echo
  echo "Sources not found in OAI (skipped):"
  printf '  %s\n' "${missing_list[@]}"
fi
