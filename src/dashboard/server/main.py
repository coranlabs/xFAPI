#!/usr/bin/env python3

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

"""
XFAPI Message Stats Dashboard Server
Provides REST API for analyzing 5G message exchange statistics
"""

from fastapi import FastAPI, HTTPException, Query
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import HTMLResponse, FileResponse
from fastapi.staticfiles import StaticFiles
from typing import List, Optional, Dict, Any
import html
import json
import logging
import os
import sys
import time
from datetime import datetime
import uvicorn

def _e(s) -> str:
    """HTML-escape a value before embedding in a content_preview string.
    Captured 5G messages can contain arbitrary bytes including <, >, &, ", '
    which would be interpreted as HTML by the dashboard. All values that
    end up inside <span>…</span> or otherwise reach a `dangerouslySetInnerHTML`
    sink must pass through this."""
    return html.escape("" if s is None else str(s), quote=True)


def _path_override_allowed() -> bool:
    """Path-override endpoints (/api/load-path, /api/validate-path) let any
    caller probe the server's filesystem and force the dashboard to read
    arbitrary JSON. They're off by default. Set XFAPI_ALLOW_PATH_OVERRIDE=1
    in the server's environment to opt back in (only safe behind a trusted
    reverse proxy)."""
    return os.environ.get("XFAPI_ALLOW_PATH_OVERRIDE", "0") == "1"


def _path_root() -> str:
    """Root directory that all user-supplied paths must resolve INSIDE.
    Defaults to the resolved XFAPI repo root. Override with $XFAPI_PATH_ROOT
    to widen or narrow the permitted area."""
    return os.path.realpath(
        os.environ.get("XFAPI_PATH_ROOT", current_xfapi_path or "/")
    )


def _resolve_user_path(user_path: str) -> str:
    """Resolve a user-supplied XFAPI directory path to an absolute file
    path for message_stats.json. Rejects anything that escapes _path_root().
    Raises HTTPException(400) on rule violations so the caller can surface
    a clean error message instead of leaking filesystem info."""
    expanded = os.path.expanduser(user_path)
    if expanded.endswith('/generated_logs/message_stats.json'):
        target = expanded
    elif os.path.isabs(expanded):
        target = os.path.join(expanded, "generated_logs", "message_stats.json")
    else:
        target = os.path.join(os.path.expanduser("~"), expanded,
                              "generated_logs", "message_stats.json")
    real_target = os.path.realpath(target)
    root = _path_root()
    try:
        if os.path.commonpath([real_target, root]) != root:
            raise ValueError("path escapes allowed root")
    except ValueError:
        raise HTTPException(
            status_code=400,
            detail="Path is outside the allowed root",
        )
    return real_target

# Process start time, used by /api/health for uptime reporting.
_PROCESS_STARTED_AT = time.time()


# ── Structured JSON logging ─────────────────────────────────────────────
# Emits one JSON object per line on stdout — friendly for `docker logs`,
# log shippers (Vector, Promtail), and aggregators (Loki, ELK). Falls back
# to plain text if XFAPI_LOG_FORMAT=text is set (useful for grep-y dev).
class _JsonFormatter(logging.Formatter):
    def format(self, record: logging.LogRecord) -> str:
        payload = {
            "ts":     datetime.utcfromtimestamp(record.created).isoformat(timespec="milliseconds") + "Z",
            "level":  record.levelname,
            "logger": record.name,
            "msg":    record.getMessage(),
        }
        # Anything attached via logger.info("...", extra={"key": val}).
        for k, v in getattr(record, "__dict__", {}).items():
            if k in ("name", "msg", "args", "levelname", "levelno", "pathname",
                     "filename", "module", "exc_info", "exc_text", "stack_info",
                     "lineno", "funcName", "created", "msecs", "relativeCreated",
                     "thread", "threadName", "processName", "process", "message",
                     "taskName"):
                continue
            try:
                json.dumps(v)
                payload[k] = v
            except TypeError:
                payload[k] = str(v)
        if record.exc_info:
            payload["exc_info"] = self.formatException(record.exc_info)
        return json.dumps(payload, default=str)


def _setup_logging() -> logging.Logger:
    level_name = os.environ.get("XFAPI_LOG_LEVEL", "INFO").upper()
    level = getattr(logging, level_name, logging.INFO)
    fmt = os.environ.get("XFAPI_LOG_FORMAT", "json").lower()

    handler = logging.StreamHandler(sys.stdout)
    if fmt == "text":
        handler.setFormatter(logging.Formatter("%(asctime)s %(levelname)-5s %(name)s %(message)s"))
    else:
        handler.setFormatter(_JsonFormatter())

    root = logging.getLogger()
    # Remove any pre-attached handlers (uvicorn adds its own; we keep ours
    # at root and let uvicorn's logger chain to it).
    for h in list(root.handlers):
        root.removeHandler(h)
    root.addHandler(handler)
    root.setLevel(level)

    # Quiet uvicorn's access logger by default — flip via env if needed.
    if os.environ.get("XFAPI_UVICORN_ACCESS_LOG", "0") != "1":
        logging.getLogger("uvicorn.access").setLevel(logging.WARNING)
    return logging.getLogger("xfapi")


log = _setup_logging()

app = FastAPI(
    title="XFAPI Message Stats Dashboard",
    description="REST API for analyzing 5G message exchange statistics",
    version="2.1.0"
)

# CORS: same-origin in production (FastAPI serves the built bundle), but in
# dev the Vite server on :5173 needs to reach the API. Default allows just
# localhost variants; override with $XFAPI_CORS_ORIGINS (comma-separated)
# or "*" for fully-open development.
_default_origins = "http://localhost:5173,http://127.0.0.1:5173,http://localhost:8080,http://127.0.0.1:8080"
_cors_env = os.environ.get("XFAPI_CORS_ORIGINS", _default_origins).strip()
_cors_origins = ["*"] if _cors_env == "*" else [o.strip() for o in _cors_env.split(",") if o.strip()]
app.add_middleware(
    CORSMiddleware,
    allow_origins=_cors_origins,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Path to the built React client (Vite output at client/dist/).
_CLIENT_DIST = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "client", "dist"))
_CLIENT_INDEX = os.path.join(_CLIENT_DIST, "index.html")
_CLIENT_ASSETS = os.path.join(_CLIENT_DIST, "assets")
if os.path.isdir(_CLIENT_ASSETS):
    app.mount("/assets", StaticFiles(directory=_CLIENT_ASSETS), name="assets")

# Global variables to store loaded message data
message_data: List[Dict[str, Any]] = []
stats_summary: Dict[str, Any] = {}
# Default capture path. Honour $XFAPI_HOME if set, otherwise fall back to "~/XFAPI".
def _autodetect_xfapi_path():
    """Resolve the XFAPI repo root automatically.

    Resolution order:
      1. $XFAPI_HOME            — explicit override (kept for power users / CI)
      2. walk-up from this file — server/main.py lives inside the XFAPI repo
                                  at src/dashboard/server/, so the repo root
                                  is typically <this file>/../../../. We walk
                                  up looking for generated_logs/message_stats.json
                                  to be robust to deeper / shallower nesting.
      3. ~/XFAPI                — last-ditch fallback (matches the old default)

    Returns (path_string, source) where source describes how it was found,
    used by /api/current-path to tell the UI whether the value was auto-
    detected and shouldn't be hand-edited casually.
    """
    env = os.environ.get("XFAPI_HOME")
    if env:
        return env, "env"

    here = os.path.abspath(os.path.dirname(__file__))
    parent = here
    for _ in range(8):  # generous limit; prevents runaway walks
        parent = os.path.dirname(parent)
        if not parent or parent == "/":
            break
        candidate = os.path.join(parent, "generated_logs", "message_stats.json")
        if os.path.exists(candidate):
            return parent, "autodetect"

    return "~/XFAPI", "fallback"


_xfapi_path_value, _xfapi_path_source = _autodetect_xfapi_path()
current_xfapi_path = _xfapi_path_value  # may still be edited via /api/load-path
json_file_path = "../../../generated_logs/message_stats.json"  # legacy fallback for load_json_data()
log.info("resolved XFAPI path", extra={"path": current_xfapi_path, "source": _xfapi_path_source})
current_data_source = "directory"  # Track whether data comes from "directory" or "vault"

# Sorted list of SLOT_INDICATION (original_index, timestamp_ns) pairs, rebuilt
# whenever message_data changes. Used by index_messages() for O(log n) lookup
# of the most-recent SLOT_INDICATION before any given message.
_slot_index: List[tuple] = []

# ── Single source of truth for latency thresholds ────────────────────────
# All units in microseconds. Used by stats/diff/timeseries computations AND
# served to the client via /api/thresholds so charts and row tints match.
THRESHOLDS = {
    "sla_us":             50,    # green / amber boundary, dashed line on charts
    "outlier_us":         100,   # amber / red boundary; counted in stats.outliers
    "hard_outlier_us":    500,   # red dot on latency chart
    "histogram_bins_us":  [1, 2, 3, 5, 10, 20, 30, 50, 100, 200, 500, 1000, 5000],
    "stream_top_n":       8,     # top-N message types in the stream chart
    "heatmap_sfn_window": 256,   # max SFN window before downsampling
    "slot_count":         20,    # NR µ=1 slot count per frame
    "default_buckets":    240,   # /api/timeseries default
    "diff_buckets":       180,   # /api/diff default
}

def _rebuild_slot_index():
    """Rebuild the SLOT_INDICATION timestamp index after message_data changes.

    Stores tuples of (original_index, timestamp_ns) in monotonically increasing
    original_index order so we can binary-search for "most recent SLOT_INDICATION
    before idx" in O(log n) instead of scanning back through message_data.
    """
    global _slot_index
    _slot_index = []
    for idx, m in enumerate(message_data):
        t = m.get('enhanced_message_type') or m.get('message_type') or ''
        if t.startswith('SLOT_INDICATION'):
            _slot_index.append((idx, m.get('timestamp_ns', 0)))


def _last_slot_ts_before(original_index: int):
    """Return the timestamp_ns of the most recent SLOT_INDICATION before
    original_index, or None if none exists. O(log n) via bisect."""
    if not _slot_index:
        return None
    import bisect
    # We want the largest idx in _slot_index strictly less than original_index.
    pos = bisect.bisect_left(_slot_index, (original_index, 0))
    if pos == 0:
        return None
    return _slot_index[pos - 1][1]


def _populate_time_diff(messages: List[Dict[str, Any]]) -> None:
    """Annotate each message in-place with `time_diff_us` (µs since the last
    SLOT_INDICATION). Idempotent — re-runs cleanly after data reloads.
    Must be called AFTER _rebuild_slot_index()."""
    for idx, m in enumerate(messages):
        ts = m.get('timestamp_ns', 0)
        last = _last_slot_ts_before(idx)
        m['time_diff_us'] = (ts - last) // 1000 if last is not None else None


def _extract_mode_topology(file_path):
    """Read the top-level "mode"/"topology" fields without full JSON parsing
    (used when the messages array itself is malformed)."""
    import re
    mode = topology = None
    try:
        with open(file_path, 'r', encoding='utf-8', errors='replace') as file:
            head = file.read(512)
        m = re.search(r'"mode"\s*:\s*"([^"]*)"', head)
        t = re.search(r'"topology"\s*:\s*"([^"]*)"', head)
        if m:
            mode = m.group(1)
        if t:
            topology = t.group(1)
    except Exception:
        pass
    return mode, topology


def parse_malformed_json(file_path):
    """Parse JSON file with unescaped newlines in message content"""
    messages = []
    
    try:
        with open(file_path, 'r', encoding='utf-8', errors='replace') as file:
            content = file.read()
        
        # Find the messages array start and end
        messages_start = content.find('"messages": [')
        if messages_start == -1:
            return []
        
        # Extract just the messages section
        messages_section = content[messages_start + len('"messages": ['):]
        messages_end = messages_section.rfind(']')
        if messages_end == -1:
            return []
        
        messages_content = messages_section[:messages_end].strip()
        
        # Parse messages one by one using regex
        import re

        # Pattern to match individual message objects. "ipc_latency_ns" is
        # optional so both older dumps (without it) and current dumps (which
        # write it between num_pdus and message_content) parse.
        message_pattern = (
            r'\{\s*"timestamp_ns":\s*(\d+),'
            r'\s*"message_type":\s*"([^"]+)",'
            r'\s*"sfn":\s*(-?\d+),'
            r'\s*"slot":\s*(-?\d+),'
            r'\s*"pdu_size":\s*(-?\d+),'
            r'\s*"num_pdus":\s*(-?\d+),'
            r'(?:\s*"ipc_latency_ns":\s*(\d+),)?'
            r'\s*"message_content":\s*"([^"]*(?:\\.[^"]*)*)"'
        )

        matches = re.finditer(message_pattern, messages_content, re.DOTALL)

        for match in matches:
            timestamp_ns = int(match.group(1))
            message_type = match.group(2)
            sfn = int(match.group(3))
            slot = int(match.group(4))
            pdu_size = int(match.group(5))
            num_pdus = int(match.group(6))
            ipc_latency_ns = int(match.group(7)) if match.group(7) else 0
            message_content = match.group(8)

            # Unescape the message content
            message_content = message_content.replace('\\n', '\n').replace('\\"', '"').replace('\\\\', '\\')

            messages.append({
                'timestamp_ns': timestamp_ns,
                'message_type': message_type,
                'sfn': sfn,
                'slot': slot,
                'pdu_size': pdu_size,
                'num_pdus': num_pdus,
                'ipc_latency_ns': ipc_latency_ns,
                'message_content': message_content
            })

        return messages
        
    except Exception as e:
        log.error("parse_malformed_json failed", extra={"error": str(e)})
        return []

def load_json_data(custom_path=None):
    """Load and parse the message stats JSON file"""
    global message_data, stats_summary, json_file_path, current_data_source
    
    log.debug("load_json_data invoked", extra={"custom_path": custom_path, "current_data_source": current_data_source})
    
    # Determine the file path to use. Order of interpretation:
    #   1. Already points at .../generated_logs/message_stats.json → use as-is.
    #   2. Starts with ~/                                        → expand ~ + append.
    #   3. Absolute path (starts with /)                          → append directly.
    #   4. Relative path (no ~ or leading /)                      → treat as $HOME/<path>.
    if custom_path:
        expanded = os.path.expanduser(custom_path)
        if expanded.endswith('/generated_logs/message_stats.json'):
            file_path = expanded
        elif os.path.isabs(expanded):
            file_path = os.path.join(expanded, "generated_logs", "message_stats.json")
        else:
            # Relative path → resolve against $HOME for parity with the old behavior.
            file_path = os.path.join(os.path.expanduser("~"), expanded,
                                     "generated_logs", "message_stats.json")
    else:
        file_path = json_file_path
    
    try:
        # Check if file exists
        if not os.path.exists(file_path):
            log.warning("JSON file not found", extra={"file_path": file_path})
            message_data = []
            stats_summary = {"error": f"JSON file not found: {file_path}", "total_messages": 0, "file_path": file_path}
            return False
        
        # First try standard JSON parsing
        xfapi_mode = None
        xfapi_topology = None
        try:
            with open(file_path, 'r', encoding='utf-8', errors='replace') as file:
                data = json.load(file)
            message_data = data.get('messages', [])
            xfapi_mode = data.get('mode')
            xfapi_topology = data.get('topology')
            log.info("loaded messages (standard parser)", extra={"count": len(message_data), "file_path": file_path})
        except json.JSONDecodeError as json_error:
            log.warning("standard JSON parse failed, trying custom parser",
                        extra={"file_path": file_path, "error": str(json_error)})
            message_data = parse_malformed_json(file_path)
            xfapi_mode, xfapi_topology = _extract_mode_topology(file_path)
            log.info("loaded messages (custom parser)", extra={"count": len(message_data), "file_path": file_path})

        # Generate summary statistics with enhanced message types
        stats_summary = generate_stats_summary(message_data)
        stats_summary["file_path"] = file_path
        stats_summary["data_source"] = "directory"
        stats_summary["mode"] = xfapi_mode or "UNKNOWN"
        stats_summary["topology"] = xfapi_topology or ""

        # Build SLOT_INDICATION lookup index once, then annotate every message
        # with time_diff_us. This makes the field available for both filtering
        # and sorting without per-request scans.
        _rebuild_slot_index()
        _populate_time_diff(message_data)
        
        log.info("processed messages", extra={"count": len(message_data), "file_path": file_path})
        
        # Only mark that we're using directory data if we're not currently using vault data
        if current_data_source != "vault":
            current_data_source = "directory"
        return True
        
    except Exception as e:
        log.error("load_json_data failed", extra={"error": str(e), "file_path": file_path if 'file_path' in locals() else None})
        message_data = []
        stats_summary = {"error": str(e), "total_messages": 0, "file_path": file_path if 'file_path' in locals() else "unknown"}
        return False
        

# L1 = PHY, L2 = MAC. FAPI direction table — keyed on the BASE message_type
# (before PDU enhancement). Anything not listed defaults to "unknown".
L2_TO_L1 = {
    "PARAM_REQUEST", "CONFIG_REQUEST", "START_REQUEST", "STOP_REQUEST",
    "DL_TTI_REQUEST", "UL_TTI_REQUEST", "UL_DCI_REQUEST", "TX_DATA_REQUEST",
}
L1_TO_L2 = {
    "PARAM_RESPONSE", "CONFIG_RESPONSE", "STOP_INDICATION",
    "SLOT_INDICATION", "RACH_INDICATION", "CRC_INDICATION",
    "RX_DATA_INDICATION", "UCI_INDICATION", "SRS_INDICATION",
    "ERROR_INDICATION", "P7_LAST_MESSAGE",
}

def message_direction(base_type: str) -> str:
    if base_type in L2_TO_L1:
        return "L2_TO_L1"
    if base_type in L1_TO_L2:
        return "L1_TO_L2"
    if base_type.endswith("_REQUEST"):
        return "L2_TO_L1"
    if base_type.endswith("_INDICATION") or base_type.endswith("_RESPONSE"):
        return "L1_TO_L2"
    return "unknown"

def _group_for(enhanced_type: str) -> str:
    """Map an enhanced message type to a semantic color group used by the UI."""
    base = enhanced_type.split(":", 1)[0].strip()
    if base in ("SLOT_INDICATION", "P7_LAST_MESSAGE"):
        return "sync"
    if base.startswith("DL_"):
        return "dl"
    if base.startswith("UL_"):
        return "ul"
    if base in ("TX_DATA_REQUEST", "RX_DATA_INDICATION"):
        return "data"
    if base == "ERROR_INDICATION":
        return "error"
    if (base.startswith("CONFIG_") or base.startswith("START_") or
        base.startswith("STOP_")   or base.startswith("PARAM_")):
        return "config"
    return "sync"


def extract_pdu_names(message_content: str) -> List[str]:
    """Extract PDU names from message content"""
    pdu_names = []
    
    # Common PDU types to look for with different patterns
    pdu_patterns = {
        'SSB': ['-----ssb-----', '----ssb pdu----', 'ssb_pdu', 'ssbblockindex'],
        'PDCCH': ['-----pdcch-----', '----pdcch----', '----pdcch pdu----', 'pdcch_pdu', 'dci_pdu'],
        'PDSCH': ['-----pdsch-----', '----pdsch----', '----pdsch pdu----', 'pdsch_pdu'],
        'CSI-RS': ['-----csi-rs-----', '----csi-rs----', '----csi-rs pdu----', 'csirs_pdu', 'csi_rs'],
        'PRACH': ['-----prach-----', '----prach pdu----', 'prach_pdu'],
        'PUSCH': ['-----pusch-----', '----pusch----', '----pusch pdu----', 'pusch_pdu'],
        'PUCCH': ['-----pucch-----', '----pucch----', '----pucch pdu----', 'pucch_pdu'],
        'SRS': ['-----srs-----', '----srs----', '----srs pdu----', 'srs_pdu']
    }
    
    content_lower = message_content.lower()
    
    for pdu_name, patterns in pdu_patterns.items():
        for pattern in patterns:
            if pattern in content_lower:
                pdu_names.append(pdu_name)
                break  # Found this PDU type, no need to check other patterns
    
    return sorted(list(set(pdu_names)))  # Remove duplicates and sort

def generate_enhanced_message_type(base_type: str, message_content: str) -> str:
    """Generate enhanced message type with PDU names appended"""
    pdu_names = extract_pdu_names(message_content)
    if pdu_names:
        return f"{base_type}: {'; '.join(pdu_names)}"
    return base_type

def generate_stats_summary(messages: List[Dict[str, Any]]) -> Dict[str, Any]:
    """Generate summary statistics from message data"""
    if not messages:
        return {"total_messages": 0}
    
    message_type_counts = {}
    
    for msg in messages:
        # Enhanced message types with PDU names
        base_type = msg.get('message_type', 'Unknown')
        content = msg.get('message_content', '')
        enhanced_type = generate_enhanced_message_type(base_type, content)
        
        # Count each message type occurrence
        message_type_counts[enhanced_type] = message_type_counts.get(enhanced_type, 0) + 1
        
        # Also store enhanced type in the message for filtering
        msg['enhanced_message_type'] = enhanced_type
    
    # Collect unique SFN and slot values
    sfn_values = set(msg.get('sfn', 0) for msg in messages)
    slot_values = set(msg.get('slot', 0) for msg in messages)
    
    # Find the earliest timestamp to determine capture date
    earliest_timestamp = None
    latest_timestamp = None
    if messages:
        timestamps = [msg.get('timestamp_ns', 0) for msg in messages if msg.get('timestamp_ns', 0) > 0]
        if timestamps:
            earliest_timestamp = min(timestamps)
            latest_timestamp = max(timestamps)

    # DL IPC latency stats (only messages with non-zero latency) — converted to µs.
    dl_latencies_us = sorted(
        m.get('ipc_latency_ns', 0) / 1000.0
        for m in messages
        if m.get('ipc_latency_ns', 0) > 0
    )
    latency_stats = {}
    if dl_latencies_us:
        n = len(dl_latencies_us)
        def pct(p):
            i = min(n - 1, max(0, int(round(p * (n - 1)))))
            return round(dl_latencies_us[i], 2)
        latency_stats = {
            "count":     n,
            "min_us":    round(dl_latencies_us[0], 2),
            "max_us":    round(dl_latencies_us[-1], 2),
            "mean_us":   round(sum(dl_latencies_us) / n, 2),
            "median_us": pct(0.50),
            "p50_us":    pct(0.50),
            "p95_us":    pct(0.95),
            "p99_us":    pct(0.99),
            "p999_us":   pct(0.999),
            "outliers":  sum(1 for v in dl_latencies_us if v > THRESHOLDS["outlier_us"]),
        }

    duration_ms = None
    if earliest_timestamp and latest_timestamp:
        duration_ms = round((latest_timestamp - earliest_timestamp) / 1e6, 2)

    error_count = sum(
        1 for m in messages if (m.get('message_type') or '').startswith('ERROR_INDICATION')
    )

    return {
        "total_messages": len(messages),
        "message_types": sorted(list(message_type_counts.keys())),
        "message_type_counts": message_type_counts,
        "sfn_values": sorted(list(sfn_values)),
        "slot_values": sorted(list(slot_values)),
        "capture_date_timestamp": earliest_timestamp,
        "duration_ms": duration_ms,
        "error_count": error_count,
        "ipc_latency_stats": latency_stats,
    }

def apply_filters(messages: List[Dict[str, Any]], filters: Dict[str, Any]) -> List[Dict[str, Any]]:
    """Apply various filters to the message list"""
    filtered_messages = messages.copy()
    
    # Message type filter (use enhanced message type if available)
    if filters.get('message_type'):
        message_types = filters['message_type']
        if isinstance(message_types, str):
            message_types = [message_types]
        
        log.debug("filtering by message types", extra={"types": message_types})
        
        # Debug: Show first few message types in data for comparison
        if filtered_messages:
            sample_types = [msg.get('enhanced_message_type', msg.get('message_type')) 
                          for msg in filtered_messages[:5]]
            log.debug("sample enhanced types", extra={"sample": sample_types})
        
        filtered_messages = [msg for msg in filtered_messages 
                           if msg.get('enhanced_message_type', msg.get('message_type')) in message_types]
        
        log.debug("after message type filtering", extra={"remaining": len(filtered_messages)})
    
    # SFN range filter
    if filters.get('sfn_min') is not None:
        filtered_messages = [msg for msg in filtered_messages if msg.get('sfn', 0) >= filters['sfn_min']]
    if filters.get('sfn_max') is not None:
        filtered_messages = [msg for msg in filtered_messages if msg.get('sfn', 0) <= filters['sfn_max']]
    
    # Slot range filter
    if filters.get('slot_min') is not None:
        filtered_messages = [msg for msg in filtered_messages if msg.get('slot', 0) >= filters['slot_min']]
    if filters.get('slot_max') is not None:
        filtered_messages = [msg for msg in filtered_messages if msg.get('slot', 0) <= filters['slot_max']]
    
    
    # Message size filter
    if filters.get('size_min') is not None:
        filtered_messages = [msg for msg in filtered_messages if msg.get('pdu_size', 0) >= filters['size_min']]
    if filters.get('size_max') is not None:
        filtered_messages = [msg for msg in filtered_messages if msg.get('pdu_size', 0) <= filters['size_max']]
    
    # Text search filter
    if filters.get('search'):
        search_term = filters['search'].lower()
        filtered_messages = [msg for msg in filtered_messages 
                           if search_term in msg.get('message_type', '').lower() 
                           or search_term in msg.get('message_content', '').lower()]
    
    return filtered_messages

def generate_custom_content_preview(message: Dict[str, Any]) -> str:
    """Generate custom content preview based on message type"""
    try:
        message_type = message.get('enhanced_message_type', message.get('message_type', ''))
        message_content = message.get('message_content', '')
        
        if message_type == 'UCI_INDICATION':
            # Extract: pdu_type, sr_ind, sr_con, hrq_con, hrq_val (abbreviated)
            preview_parts = []
            
            # Extract pdu_type
            if 'pdu_type=' in message_content:
                start = message_content.find('pdu_type=')
                if start != -1:
                    end = message_content.find('\n', start)
                    if end != -1:
                        pdu_type_line = message_content[start:end].strip()
                        value = pdu_type_line.split("=")[1] if "=" in pdu_type_line else ""
                        preview_parts.append(f'type:{_e(value)}')
            
            # Extract sr_indication
            if 'sr_indication=' in message_content:
                start = message_content.find('sr_indication=')
                if start != -1:
                    end = message_content.find('\n', start)
                    if end != -1:
                        sr_indication = message_content[start:end].strip()
                        value = sr_indication.split("=")[1] if "=" in sr_indication else ""
                        color_class = 'uci-value-0' if value == '0' else 'uci-value-1'
                        preview_parts.append(f'sr_ind=<span class="{_e(color_class)}">{_e(value)}</span>')
            
            # Extract sr_confidence_level
            if 'sr_confidence_level=' in message_content:
                start = message_content.find('sr_confidence_level=')
                if start != -1:
                    end = message_content.find('\n', start)
                    if end != -1:
                        sr_confidence = message_content[start:end].strip()
                        value = sr_confidence.split("=")[1] if "=" in sr_confidence else ""
                        color_class = 'uci-value-0' if value == '0' else 'uci-value-1'
                        preview_parts.append(f'sr_con=<span class="{_e(color_class)}">{_e(value)}</span>')
            
            # Extract harq[0].harq_value (new field) - display before harq_confidence
            if 'harq[0].harq_value=' in message_content:
                start = message_content.find('harq[0].harq_value=')
                if start != -1:
                    end = message_content.find('\n', start)
                    if end != -1:
                        harq_value = message_content[start:end].strip()
                        value = harq_value.split("=")[1] if "=" in harq_value else ""
                        color_class = 'uci-value-0' if value == '0' else 'uci-value-1'
                        preview_parts.append(f'hrq_val=<span class="{_e(color_class)}">{_e(value)}</span>')
            
            # Extract harq_confidence_level
            if 'harq_confidence_level=' in message_content:
                start = message_content.find('harq_confidence_level=')
                if start != -1:
                    end = message_content.find('\n', start)
                    if end != -1:
                        harq_confidence = message_content[start:end].strip()
                        value = harq_confidence.split("=")[1] if "=" in harq_confidence else ""
                        color_class = 'uci-value-0' if value == '0' else 'uci-value-1'
                        preview_parts.append(f'hrq_con=<span class="{_e(color_class)}">{_e(value)}</span>')
            
            return ', '.join(preview_parts) if preview_parts else (_e(message_content[:100]) + "..." if len(message_content) > 100 else _e(message_content))
        
        elif message_type == 'RX_DATA_INDICATION':
            # Extract some bytes of 0th rx_pdu->pdu if data_hex is present
            preview_parts = []
            
            # Check if data_hex is present (regardless of pdu_length)
            if 'data_hex=' in message_content:
                hex_start = message_content.find('data_hex=')
                if hex_start != -1:
                    hex_end = message_content.find('\n', hex_start)
                    if hex_end != -1:
                        hex_data = message_content[hex_start:hex_end].strip()
                        # Get first few bytes
                        hex_bytes = hex_data.replace('data_hex=', '').strip()
                        if hex_bytes:  # Only if there's actual hex data
                            first_bytes = ' '.join(hex_bytes.split()[:16])  # First 16 bytes
                            preview_parts.append(f'{_e(first_bytes)}...')
            
            # If no hex data found, show pdu_length info
            if not preview_parts and 'pdu_length=' in message_content:
                start = message_content.find('pdu_length=')
                if start != -1:
                    end = message_content.find('\n', start)
                    if end != -1:
                        pdu_length_line = message_content[start:end].strip()
                        preview_parts.append(_e(pdu_length_line))
            
            return ', '.join(preview_parts) if preview_parts else (_e(message_content[:100]) + "..." if len(message_content) > 100 else _e(message_content))
        
        elif message_type.startswith('UL_TTI_REQUEST') and 'PUSCH' in message_type:
            # Extract MCS index, MCS table, and TB size for UL TTI messages with PUSCH
            preview_parts = []
            
            # Extract mcs_index
            if 'mcs_index=' in message_content:
                start = message_content.find('mcs_index=')
                if start != -1:
                    end = message_content.find('\n', start)
                    if end != -1:
                        mcs_index_line = message_content[start:end].strip()
                        value = mcs_index_line.split("=")[1] if "=" in mcs_index_line else ""
                        preview_parts.append(f'mcs_idx={_e(value)}')
            
            # Extract mcs_table
            if 'mcs_table=' in message_content:
                start = message_content.find('mcs_table=')
                if start != -1:
                    end = message_content.find('\n', start)
                    if end != -1:
                        mcs_table_line = message_content[start:end].strip()
                        value = mcs_table_line.split("=")[1] if "=" in mcs_table_line else ""
                        preview_parts.append(f'mcs_tbl={_e(value)}')
            
            # Extract tb_size
            if 'tb_size=' in message_content:
                start = message_content.find('tb_size=')
                if start != -1:
                    end = message_content.find('\n', start)
                    if end != -1:
                        tb_size_line = message_content[start:end].strip()
                        value = tb_size_line.split("=")[1] if "=" in tb_size_line else ""
                        preview_parts.append(f'tb_size={_e(value)}')
            
            return ', '.join(preview_parts) if preview_parts else (_e(message_content[:100]) + "..." if len(message_content) > 100 else _e(message_content))
        
        elif message_type.startswith('UL_DCI_REQUEST') and 'PDCCH' in message_type:
            # Extract CceIndex, AggregationLevel, RegBundleSize, and StartSymbolIndex for UL DCI messages with PDCCH
            preview_parts = []
            
            # Extract CceIndex
            if 'CceIndex=' in message_content:
                start = message_content.find('CceIndex=')
                if start != -1:
                    end = message_content.find('\n', start)
                    if end != -1:
                        cce_index_line = message_content[start:end].strip()
                        value = cce_index_line.split("=")[1] if "=" in cce_index_line else ""
                        preview_parts.append(f'CceIdx={_e(value)}')
            
            # Extract AggregationLevel
            if 'AggregationLevel=' in message_content:
                start = message_content.find('AggregationLevel=')
                if start != -1:
                    end = message_content.find('\n', start)
                    if end != -1:
                        aggr_level_line = message_content[start:end].strip()
                        value = aggr_level_line.split("=")[1] if "=" in aggr_level_line else ""
                        preview_parts.append(f'Aggr.Lvl={_e(value)}')
            
            # Extract RegBundleSize
            if 'RegBundleSize=' in message_content:
                start = message_content.find('RegBundleSize=')
                if start != -1:
                    end = message_content.find('\n', start)
                    if end != -1:
                        reg_bundle_line = message_content[start:end].strip()
                        value = reg_bundle_line.split("=")[1] if "=" in reg_bundle_line else ""
                        preview_parts.append(f'RegBndlSz={_e(value)}')
            
            # Extract StartSymbolIndex
            if 'StartSymbolIndex=' in message_content:
                start = message_content.find('StartSymbolIndex=')
                if start != -1:
                    end = message_content.find('\n', start)
                    if end != -1:
                        start_symbol_line = message_content[start:end].strip()
                        value = start_symbol_line.split("=")[1] if "=" in start_symbol_line else ""
                        preview_parts.append(f'StartSymIdx={_e(value)}')
            
            return ', '.join(preview_parts) if preview_parts else (_e(message_content[:100]) + "..." if len(message_content) > 100 else _e(message_content))
        
        elif message_type == 'TX_DATA_REQUEST':
            # Extract hex bytes from pdu_list[0].TLVs[0].value.direct
            preview_parts = []
            
            # Look for TLV data (hex bytes) directly
            if 'TLV[0].data_hex=' in message_content:
                hex_start = message_content.find('TLV[0].data_hex=')
                if hex_start != -1:
                    hex_end = message_content.find('\n', hex_start)
                    if hex_end != -1:
                        hex_data = message_content[hex_start:hex_end].strip()
                        # Get first few bytes
                        hex_bytes = hex_data.replace('TLV[0].data_hex=', '').strip()
                        if hex_bytes:  # Only if there's actual hex data
                            first_bytes = ' '.join(hex_bytes.split()[:16])  # First 16 bytes
                            preview_parts.append(f'{_e(first_bytes)}...')
            
            return ', '.join(preview_parts) if preview_parts else (_e(message_content[:100]) + "..." if len(message_content) > 100 else _e(message_content))
    
        else:
            # For all other message types, keep existing content preview
            return _e(message_content[:100]) + "..." if len(message_content) > 100 else _e(message_content)
    
    except Exception as e:
        # In case of any error, fall back to default preview
        message_content = message.get('message_content', '')
        return message_content[:100] + "..." if len(message_content) > 100 else message_content

def apply_single_message_filter(msg: Dict[str, Any], filters: Dict[str, Any]) -> bool:
    """Apply filters to a single message and return True if it passes"""
    
    # Message type filter (use enhanced message type if available)
    if filters.get('message_type'):
        message_types = filters['message_type']
        if isinstance(message_types, str):
            message_types = [message_types]
        
        msg_type = msg.get('enhanced_message_type', msg.get('message_type'))
        if msg_type not in message_types:
            return False
    
    # SFN — discrete set wins over range.
    if filters.get('sfn_in') is not None:
        if msg.get('sfn', 0) not in filters['sfn_in']:
            return False
    else:
        if filters.get('sfn_min') is not None and msg.get('sfn', 0) < filters['sfn_min']:
            return False
        if filters.get('sfn_max') is not None and msg.get('sfn', 0) > filters['sfn_max']:
            return False

    # Slot — discrete set wins over range.
    if filters.get('slot_in') is not None:
        if msg.get('slot', 0) not in filters['slot_in']:
            return False
    else:
        if filters.get('slot_min') is not None and msg.get('slot', 0) < filters['slot_min']:
            return False
        if filters.get('slot_max') is not None and msg.get('slot', 0) > filters['slot_max']:
            return False
    
    # Size range filter
    if filters.get('size_min') is not None:
        if msg.get('pdu_size', 0) < filters['size_min']:
            return False
    if filters.get('size_max') is not None:
        if msg.get('pdu_size', 0) > filters['size_max']:
            return False
    
    # Search filter
    if filters.get('search'):
        search_term = filters['search'].lower()
        message_type = msg.get('message_type', '').lower()
        enhanced_type = msg.get('enhanced_message_type', '').lower()
        content = msg.get('message_content', '').lower()
        
        if (search_term not in message_type and 
            search_term not in enhanced_type and 
            search_term not in content):
            return False
    
    return True

@app.get("/", response_class=HTMLResponse)
async def root():
    """Serve the React dashboard's index.html (built by Vite)."""
    if not os.path.exists(_CLIENT_INDEX):
        return HTMLResponse(
            "<h1>XFAPI Dashboard — client not built</h1>"
            "<p>Run <code>cd src/dashboard/client &amp;&amp; npm install &amp;&amp; npm run build</code>.</p>",
            status_code=503,
        )
    return FileResponse(_CLIENT_INDEX)



# ── Vault envelope helpers ───────────────────────────────────────────────
# Format written by /api/vault/save (xfapi-vault-v1):
#   {"format": "xfapi-vault-v1",
#    "metadata": {"name", "tag", "note", "build_version", "captured_at_ns", "saved_at"},
#    "messages": [...]}
# Older formats (raw list / {"messages": [...]}) are still readable.
# Vault directory. Defaults to `./stats_vault` (relative to server CWD) for
# in-repo dev. In Docker, override with $XFAPI_VAULT_DIR=/data/vault and
# bind-mount that path.
VAULT_DIR = os.environ.get("XFAPI_VAULT_DIR", "stats_vault")
VAULT_TAGS = {"live", "baseline", "flagged", "archived"}

def _vault_path(filename: str) -> str:
    if "/" in filename or "\\" in filename or filename.startswith("."):
        raise HTTPException(status_code=400, detail="Invalid filename")
    if not filename.endswith(".json"):
        filename = filename + ".json"
    return os.path.join(VAULT_DIR, filename)

def _read_vault(path: str) -> Dict[str, Any]:
    with open(path, 'r') as f:
        data = json.load(f)
    if isinstance(data, list):
        messages, metadata = data, {}
    elif isinstance(data, dict) and data.get("format") == "xfapi-vault-v1":
        messages = data.get("messages", [])
        metadata = data.get("metadata", {})
    elif isinstance(data, dict) and "messages" in data:
        messages = data["messages"]
        metadata = {}
    else:
        raise ValueError("Unrecognised vault file format")
    captured_at_ns = metadata.get("captured_at_ns")
    if not captured_at_ns and messages:
        captured_at_ns = messages[0].get("timestamp_ns") or messages[0].get("timestamp") or 0
    metadata.setdefault("tag", "archived")
    metadata.setdefault("note", "")
    metadata.setdefault("build_version", "")
    metadata["captured_at_ns"] = captured_at_ns or 0
    return {"messages": messages, "metadata": metadata}

def _write_vault(path: str, messages: List[Dict[str, Any]], metadata: Dict[str, Any]) -> None:
    envelope = {"format": "xfapi-vault-v1", "metadata": metadata, "messages": messages}
    with open(path, 'w') as f:
        json.dump(envelope, f, indent=2)


@app.get("/api/timeseries")
async def get_timeseries(buckets: int = Query(default=THRESHOLDS["default_buckets"], ge=10, le=2000)):
    """Downsampled charts: DL latency trace, per-type rate stream, SFN/slot heatmap."""
    if not message_data:
        return {
            "buckets": buckets,
            "t_min_ns": 0, "t_max_ns": 0,
            "latency_trace": [],
            "rate_stream": {"types": [], "buckets": [], "labels": []},
            "heatmap": {"cells": [], "sfn_min": 0, "sfn_max": 0, "sfn_step": 1, "slot_count": 20},
        }

    timestamps = [m.get('timestamp_ns', 0) for m in message_data if m.get('timestamp_ns', 0) > 0]
    if not timestamps:
        return {
            "buckets": buckets,
            "t_min_ns": 0, "t_max_ns": 0,
            "latency_trace": [],
            "rate_stream": {"types": [], "buckets": [], "labels": []},
            "heatmap": {"cells": [], "sfn_min": 0, "sfn_max": 0, "sfn_step": 1, "slot_count": 20},
        }
    t_min, t_max = min(timestamps), max(timestamps)
    span = max(1, t_max - t_min)

    # 1. Latency trace — downsample DL ipc_latency_ns into <buckets> points (max per bucket).
    bucket_max = [0.0] * buckets
    bucket_has = [False] * buckets
    for m in message_data:
        lat = m.get('ipc_latency_ns', 0)
        ts = m.get('timestamp_ns', 0)
        if lat <= 0 or ts <= 0:
            continue
        idx = min(buckets - 1, int((ts - t_min) * buckets / span))
        v_us = lat / 1000.0
        if v_us > bucket_max[idx]:
            bucket_max[idx] = v_us
            bucket_has[idx] = True
    last = 0.0
    latency_trace = []
    for i in range(buckets):
        if bucket_has[i]:
            last = bucket_max[i]
        latency_trace.append(round(last, 2))

    # 2. Per-type rate stream — top 8 enhanced types by count.
    counts = {}
    for m in message_data:
        t = m.get('enhanced_message_type', m.get('message_type', 'Unknown'))
        counts[t] = counts.get(t, 0) + 1
    top_types = sorted(counts.items(), key=lambda x: -x[1])[:THRESHOLDS["stream_top_n"]]
    type_index = {t: i for i, (t, _) in enumerate(top_types)}
    stream_buckets = [[0] * buckets for _ in top_types]
    for m in message_data:
        t = m.get('enhanced_message_type', m.get('message_type', 'Unknown'))
        if t not in type_index:
            continue
        ts = m.get('timestamp_ns', 0)
        if ts <= 0:
            continue
        idx = min(buckets - 1, int((ts - t_min) * buckets / span))
        stream_buckets[type_index[t]][idx] += 1

    # 3. SFN/slot heatmap (max sampled SFN window = 256).
    sfns = [m.get('sfn', 0) for m in message_data]
    sfn_min, sfn_max = min(sfns), max(sfns)
    sfn_span = sfn_max - sfn_min + 1
    sfn_window = min(sfn_span, THRESHOLDS["heatmap_sfn_window"])
    sfn_step = max(1, sfn_span // sfn_window)
    sfn_axis = list(range(sfn_min, sfn_min + sfn_window * sfn_step, sfn_step))
    slot_count = THRESHOLDS["slot_count"]
    heat = [[0] * slot_count for _ in range(sfn_window)]
    for m in message_data:
        rel = (m.get('sfn', 0) - sfn_min) // sfn_step
        if 0 <= rel < sfn_window:
            sl = m.get('slot', 0)
            if 0 <= sl < slot_count:
                heat[rel][sl] += 1
    cells = []
    flat_max = max((heat[i][j] for i in range(sfn_window) for j in range(slot_count)), default=1) or 1
    for i, s in enumerate(sfn_axis):
        for j in range(slot_count):
            v = heat[i][j]
            if v > 0:
                cells.append({"sfn": s, "slot": j, "density": v, "norm": round(v / flat_max, 3)})

    labels = [
        {"i": 0,            "t_ms": 0},
        {"i": buckets // 2, "t_ms": round((t_max - t_min) / 2 / 1e6)},
        {"i": buckets - 1,  "t_ms": round((t_max - t_min) / 1e6)},
    ]

    # 4. Latency histogram — log-binned counts of every DL latency value.
    bins_us = THRESHOLDS["histogram_bins_us"]
    histogram = [0] * (len(bins_us) - 1)
    for m in message_data:
        lat = m.get('ipc_latency_ns', 0)
        if lat <= 0:
            continue
        v = lat / 1000.0
        if v < bins_us[0]:
            continue
        for i in range(len(bins_us) - 1):
            if bins_us[i] <= v < bins_us[i + 1]:
                histogram[i] += 1
                break

    return {
        "buckets": buckets,
        "t_min_ns": t_min, "t_max_ns": t_max,
        "latency_trace": latency_trace,
        "rate_stream": {
            "types": [{"type": t, "count": c, "color_group": _group_for(t)} for t, c in top_types],
            "buckets": stream_buckets,
            "labels": labels,
        },
        "heatmap": {
            "cells": cells,
            "sfn_min": sfn_min,
            "sfn_max": sfn_axis[-1] if sfn_axis else sfn_min,
            "sfn_step": sfn_step,
            "slot_count": slot_count,
        },
        "histogram": {
            "bins_us": bins_us,
            "counts": histogram,
        },
    }


@app.get("/api/sequence")
async def get_sequence(
    limit: int = Query(60, ge=1, le=500),
    offset: int = Query(0, ge=0),
    sfn_min: Optional[int] = Query(None),
    sfn_max: Optional[int] = Query(None),
    slot_min: Optional[int] = Query(None),
    slot_max: Optional[int] = Query(None),
    sla_us: float = Query(100.0, ge=0.0),
):
    """Ordered events for the sequence-diagram view (L1 ↔ L2 lifelines)."""
    events = []
    for idx, m in enumerate(message_data):
        s, sl = m.get('sfn', 0), m.get('slot', 0)
        if sfn_min is not None and s < sfn_min: continue
        if sfn_max is not None and s > sfn_max: continue
        if slot_min is not None and sl < slot_min: continue
        if slot_max is not None and sl > slot_max: continue
        base = m.get('message_type', 'Unknown')
        direction = message_direction(base)
        if direction == "unknown":
            continue
        lat_ns = m.get('ipc_latency_ns', 0)
        lat_us = round(lat_ns / 1000.0, 2) if lat_ns > 0 else None
        events.append({
            "id": idx,
            "ts": m.get('timestamp_ns', 0),
            "type": m.get('enhanced_message_type', base),
            "base_type": base,
            "direction": direction,
            "from": "L2" if direction == "L2_TO_L1" else "L1",
            "to":   "L1" if direction == "L2_TO_L1" else "L2",
            "sfn": s, "slot": sl,
            "latency_us": lat_us,
            "anomaly": (lat_us is not None and lat_us > sla_us),
            "color_group": _group_for(m.get('enhanced_message_type', base)),
        })
    total = len(events)
    sliced = events[offset:offset + limit]
    return {
        "events": sliced,
        "pagination": {
            "offset": offset, "limit": limit,
            "total_count": total,
            "has_next": offset + limit < total,
            "has_prev": offset > 0,
        },
        "sla_us": sla_us,
    }


@app.get("/api/diff")
async def diff_captures(a: str = Query(...), b: str = Query(...),
                        buckets: int = Query(default=THRESHOLDS["diff_buckets"], ge=10, le=1000)):
    """Compare two vault captures: KPI deltas, per-type counts, and overlaid latency traces."""
    path_a, path_b = _vault_path(a), _vault_path(b)
    if not os.path.exists(path_a):
        raise HTTPException(status_code=404, detail=f"Vault file not found: {a}")
    if not os.path.exists(path_b):
        raise HTTPException(status_code=404, detail=f"Vault file not found: {b}")

    def summarise(messages: List[Dict[str, Any]]):
        total = len(messages)
        if total == 0:
            return {"total": 0, "errors": 0, "duration_ms": 0,
                    "p50": 0, "p95": 0, "p99": 0, "p999": 0, "outliers": 0,
                    "type_counts": {}, "trace": [0] * buckets,
                    "t_min_ns": 0, "t_max_ns": 0}
        for m in messages:
            base = m.get('message_type', 'Unknown')
            if 'enhanced_message_type' not in m:
                m['enhanced_message_type'] = generate_enhanced_message_type(base, m.get('message_content', ''))
        timestamps = [m.get('timestamp_ns', 0) for m in messages if m.get('timestamp_ns', 0) > 0]
        t_min = min(timestamps) if timestamps else 0
        t_max = max(timestamps) if timestamps else 0
        duration_ms = (t_max - t_min) / 1e6 if t_max > t_min else 0
        latencies_us = sorted(m.get('ipc_latency_ns', 0) / 1000.0 for m in messages if m.get('ipc_latency_ns', 0) > 0)
        def pct(vs, p):
            if not vs: return 0
            i = min(len(vs) - 1, max(0, int(round(p * (len(vs) - 1)))))
            return round(vs[i], 2)
        outliers = sum(1 for v in latencies_us if v > THRESHOLDS["outlier_us"])
        errors = sum(1 for m in messages if (m.get('message_type') or '').startswith('ERROR_INDICATION'))
        type_counts: Dict[str, int] = {}
        for m in messages:
            t = m.get('enhanced_message_type', m.get('message_type', 'Unknown'))
            type_counts[t] = type_counts.get(t, 0) + 1
        trace = [0.0] * buckets
        if t_max > t_min:
            span = t_max - t_min
            for m in messages:
                lat = m.get('ipc_latency_ns', 0)
                ts = m.get('timestamp_ns', 0)
                if lat <= 0 or ts <= 0: continue
                idx = min(buckets - 1, int((ts - t_min) * buckets / span))
                v = lat / 1000.0
                if v > trace[idx]:
                    trace[idx] = v
            last = 0.0
            for i in range(buckets):
                if trace[i] > 0: last = trace[i]
                else: trace[i] = last
        return {
            "total": total, "errors": errors,
            "duration_ms": round(duration_ms, 2),
            "p50": pct(latencies_us, 0.50),
            "p95": pct(latencies_us, 0.95),
            "p99": pct(latencies_us, 0.99),
            "p999": pct(latencies_us, 0.999),
            "outliers": outliers,
            "type_counts": type_counts,
            "trace": [round(x, 2) for x in trace],
            "t_min_ns": t_min, "t_max_ns": t_max,
        }

    try:
        va, vb = _read_vault(path_a), _read_vault(path_b)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Failed to read vault file: {e}")
    sa, sb = summarise(va["messages"]), summarise(vb["messages"])

    all_types = sorted(set(sa["type_counts"]) | set(sb["type_counts"]))
    type_deltas = []
    for t in all_types:
        ca, cb = sa["type_counts"].get(t, 0), sb["type_counts"].get(t, 0)
        delta_pct = None
        if ca > 0:
            delta_pct = round((cb - ca) / ca * 100.0, 1)
        elif cb > 0:
            delta_pct = "inf"
        type_deltas.append({
            "type": t, "a": ca, "b": cb, "delta": cb - ca,
            "delta_pct": delta_pct, "color_group": _group_for(t),
        })
    type_deltas.sort(key=lambda x: -abs(x["delta"]))

    return {
        "a": {"filename": a, "metadata": va["metadata"], **{k: v for k, v in sa.items() if k != "type_counts"}},
        "b": {"filename": b, "metadata": vb["metadata"], **{k: v for k, v in sb.items() if k != "type_counts"}},
        "deltas": {
            "total":       {"a": sa["total"],       "b": sb["total"],       "delta": sb["total"] - sa["total"]},
            "errors":      {"a": sa["errors"],      "b": sb["errors"],      "delta": sb["errors"] - sa["errors"]},
            "duration_ms": {"a": sa["duration_ms"], "b": sb["duration_ms"], "delta": round(sb["duration_ms"] - sa["duration_ms"], 2)},
            "p99":         {"a": sa["p99"],         "b": sb["p99"],         "delta": round(sb["p99"] - sa["p99"], 2)},
            "outliers":    {"a": sa["outliers"],    "b": sb["outliers"],    "delta": sb["outliers"] - sa["outliers"]},
        },
        "type_deltas": type_deltas,
        "buckets": buckets,
    }


@app.get("/api/refresh")
async def refresh_data():
    """Reload JSON data from file"""
    global current_data_source
    
    # Only reload from directory if we're currently using directory data
    if current_data_source == "directory":
        load_json_data()
        return {
            "status": "success",
            "message": f"Reloaded {len(message_data)} messages from directory",
            "data_source": "directory",
            "timestamp": datetime.now().isoformat()
        }
    else:
        # If using vault data, don't reload - vault data is static
        return {
            "status": "info", 
            "message": f"Using vault data ({len(message_data)} messages) - no refresh needed",
            "data_source": "vault",
            "timestamp": datetime.now().isoformat()
        }

@app.get("/api/health")
async def health():
    """Liveness + readiness probe.

    Returns 200 OK with a status payload as long as the process is alive.
    `data_loaded=False` means the configured message_stats.json was missing
    or empty — the dashboard still runs (vault loads work), but a startup-
    time miss is worth surfacing to monitoring.
    """
    vault_count = 0
    try:
        if os.path.isdir(VAULT_DIR):
            vault_count = sum(1 for f in os.listdir(VAULT_DIR) if f.endswith('.json'))
    except Exception:
        vault_count = -1
    return {
        "status": "ok",
        "uptime_seconds": round(time.time() - _PROCESS_STARTED_AT, 1),
        "data_loaded": bool(message_data),
        "messages_loaded": len(message_data),
        "vault_files": vault_count,
        "data_source": current_data_source,
        "xfapi_path": current_xfapi_path,
        "xfapi_path_source": _xfapi_path_source,
    }


@app.get("/api/stats")
async def get_stats():
    """Get summary statistics about loaded messages"""
    return stats_summary


@app.get("/api/thresholds")
async def get_thresholds():
    """Single source of truth for latency / chart thresholds.
    Read by the client at startup so badges, row tints, and chart props stay
    in sync with the values used to compute stats."""
    return THRESHOLDS

@app.get("/api/messages")
async def get_messages(
    page: int = Query(1, ge=1, description="Page number (1-based)"),
    limit: int = Query(100, ge=1, le=100000, description="Messages per page"),
    message_type: Optional[str] = Query(None, description="Comma-separated list of enhanced message types"),
    # Discrete-set filters (preferred). Comma-separated list of allowed values.
    sfn_in: Optional[str] = Query(None, description="Comma-separated SFNs to include"),
    slot_in: Optional[str] = Query(None, description="Comma-separated slots to include"),
    # Range filters (kept for backwards compatibility / contiguous selections).
    sfn_min: Optional[int] = Query(None),
    sfn_max: Optional[int] = Query(None),
    slot_min: Optional[int] = Query(None),
    slot_max: Optional[int] = Query(None),
    size_min: Optional[int] = Query(None),
    size_max: Optional[int] = Query(None),
    search: Optional[str] = Query(None, description="Text search in message type or content"),
    sort_by: Optional[str] = Query("timestamp", description="Sort by field"),
    sort_order: Optional[str] = Query("desc", description="Sort order: asc or desc"),
):
    """Get paginated and filtered list of messages."""

    filters: Dict[str, Any] = {}
    if message_type:
        filters['message_type'] = [mt.strip() for mt in message_type.split(',')]
    if sfn_in:
        try:
            filters['sfn_in'] = {int(x) for x in sfn_in.split(',') if x.strip() != ''}
        except ValueError:
            raise HTTPException(status_code=400, detail="sfn_in must be comma-separated integers")
    if slot_in:
        try:
            filters['slot_in'] = {int(x) for x in slot_in.split(',') if x.strip() != ''}
        except ValueError:
            raise HTTPException(status_code=400, detail="slot_in must be comma-separated integers")
    if sfn_min is not None:  filters['sfn_min']  = sfn_min
    if sfn_max is not None:  filters['sfn_max']  = sfn_max
    if slot_min is not None: filters['slot_min'] = slot_min
    if slot_max is not None: filters['slot_max'] = slot_max
    if size_min is not None: filters['size_min'] = size_min
    if size_max is not None: filters['size_max'] = size_max
    if search:               filters['search']   = search

    filtered_messages_with_indices = []
    for original_idx, msg in enumerate(message_data):
        if apply_single_message_filter(msg, filters):
            msg_with_idx = msg.copy()
            msg_with_idx['original_index'] = original_idx
            filtered_messages_with_indices.append(msg_with_idx)

    # Sort. time_diff_us, ipc_latency_ns, message_type are all populated on
    # message_data at load time (or by load itself) so sorting on them works.
    SORTABLE_NUMERIC = {'timestamp', 'timestamp_ns', 'time_diff_us', 'sfn', 'slot',
                        'pdu_size', 'num_pdus', 'ipc_latency_ns'}
    reverse = (sort_order or '').lower() == 'desc'
    if sort_by in SORTABLE_NUMERIC:
        key = 'timestamp_ns' if sort_by == 'timestamp' else sort_by
        # None values sort last on either order — push them to the extreme.
        sentinel = float('-inf') if reverse else float('inf')
        filtered_messages_with_indices.sort(
            key=lambda x: (x.get(key) if x.get(key) is not None else sentinel),
            reverse=reverse,
        )
    elif sort_by == 'id':
        filtered_messages_with_indices.sort(key=lambda x: x.get('original_index', 0), reverse=reverse)
    elif sort_by == 'message_type':
        filtered_messages_with_indices.sort(
            key=lambda x: (x.get('enhanced_message_type') or x.get('message_type') or ''),
            reverse=reverse,
        )

    total_count = len(filtered_messages_with_indices)
    start_idx = (page - 1) * limit
    end_idx = start_idx + limit
    paginated_messages = filtered_messages_with_indices[start_idx:end_idx]

    # time_diff_us is already populated on every message at load time
    # via _populate_time_diff() — no per-request scan needed.


    # Create simplified message list (without full content for performance)
    message_list = []
    for i, msg in enumerate(paginated_messages):
        base_type = msg.get('message_type', 'Unknown')
        message_list.append({
            "id": msg['original_index'],  # Use the original dataset index
            "timestamp": msg.get('timestamp_ns', 0),
            "message_type": msg.get('enhanced_message_type', base_type),
            "base_message_type": base_type,
            "direction": message_direction(base_type),
            "sfn": msg.get('sfn', 0),
            "slot": msg.get('slot', 0),
            "pdu_size": msg.get('pdu_size', 0),
            "num_pdus": msg.get('num_pdus', 0),
            "ipc_latency_ns": msg.get('ipc_latency_ns', 0),
            "time_diff_us": msg.get('time_diff_us'),
            "content_preview": generate_custom_content_preview(msg)
        })
    
    return {
        "messages": message_list,
        "pagination": {
            "page": page,
            "limit": limit,
            "total_count": total_count,
            "total_pages": (total_count + limit - 1) // limit,
            "has_next": end_idx < total_count,
            "has_prev": page > 1
        },
        "filters_applied": filters
    }

@app.get("/api/message/{message_id}")
async def get_message_detail(message_id: int):
    """Get detailed content of a specific message"""
    log.debug("get_message_detail", extra={"message_id": message_id, "loaded": len(message_data)})
    if message_id < 0 or message_id >= len(message_data):
        raise HTTPException(status_code=404, detail="Message not found")
    message = message_data[message_id]
    base_type = message.get('message_type', 'Unknown')
    return {
        "id": message_id,
        "timestamp": message.get('timestamp', message.get('timestamp_ns', 0)),
        "message_type": message.get('enhanced_message_type', base_type),
        "base_message_type": base_type,
        "direction": message_direction(base_type),
        "sfn": message.get('sfn', 0),
        "slot": message.get('slot', 0),
        "pdu_size": message.get('pdu_size', 0),
        "num_pdus": message.get('num_pdus', 0),
        "ipc_latency_ns": message.get('ipc_latency_ns', 0),
        "full_content": message.get('full_content', message.get('message_content', message.get('content', '')))
    }

@app.get("/api/search-payload")
async def search_payload(query: str = ""):
    """Search for payload bytes in TX_DATA and RX_DATA messages"""
    global message_data
    
    if not query:
        return {"matches": []}
    
    # Clean and validate hex input
    clean_query = query.upper().replace(' ', '').replace('0X', '')
    if not all(c in '0123456789ABCDEF' for c in clean_query):
        raise HTTPException(status_code=400, detail="Invalid hexadecimal input")
    
    matches = []
    
    for idx, msg in enumerate(message_data):
        # Only search in TX_DATA and RX_DATA messages
        if (msg.get('message_type', '').startswith('TX_DATA_REQUEST') or 
            msg.get('message_type', '') == 'RX_DATA_INDICATION'):
            
            content = str(msg.get('message_content', '')).upper()
            # Remove spaces from content to match the cleaned query format
            clean_content = content.replace(' ', '')
            if clean_query in clean_content:
                matches.append({
                    "id": idx,
                    "global_index": idx,
                    "message_type": msg.get('message_type', ''),
                    "timestamp": msg.get('timestamp_ns', 0),
                    "sfn": msg.get('sfn', 0),
                    "slot": msg.get('slot', 0)
                })
    
    return {"matches": matches}

@app.get("/api/vault/list")
async def list_vault_files():
    """List all saved stats files in the vault."""
    if not os.path.exists(VAULT_DIR):
        os.makedirs(VAULT_DIR)
        return {"files": []}

    SPARK_BUCKETS = 60
    files = []
    for filename in os.listdir(VAULT_DIR):
        if not filename.endswith('.json'):
            continue
        filepath = os.path.join(VAULT_DIR, filename)
        st = os.stat(filepath)
        message_count = 0
        captured_date = None
        metadata: Dict[str, Any] = {}
        spark = []
        duration_ms = None
        p99_us = None
        error_count = 0
        try:
            v = _read_vault(filepath)
            messages = v["messages"]
            message_count = len(messages)
            metadata = v["metadata"]
            captured_at_ns = metadata.get("captured_at_ns") or 0
            if captured_at_ns:
                captured_date = captured_at_ns / 1_000_000_000

            # Sparkline + summary stats. Computed once at list time, not on
            # every render. Cheap (single pass) and lets the client render
            # the Vault grid without a follow-up fetch per card.
            ts = [m.get('timestamp_ns', 0) for m in messages if m.get('timestamp_ns', 0) > 0]
            if ts:
                t_min, t_max = min(ts), max(ts)
                duration_ms = round((t_max - t_min) / 1e6, 2) if t_max > t_min else 0
                # Latency sparkline: max-per-bucket, log-scale-friendly.
                buckets = [0.0] * SPARK_BUCKETS
                if t_max > t_min:
                    span = t_max - t_min
                    for m in messages:
                        lat = m.get('ipc_latency_ns', 0)
                        if lat <= 0:
                            continue
                        ts_i = m.get('timestamp_ns', 0)
                        if ts_i <= 0:
                            continue
                        idx = min(SPARK_BUCKETS - 1, int((ts_i - t_min) * SPARK_BUCKETS / span))
                        v_us = lat / 1000.0
                        if v_us > buckets[idx]:
                            buckets[idx] = v_us
                    last = 0.0
                    for i in range(SPARK_BUCKETS):
                        if buckets[i] > 0:
                            last = buckets[i]
                        else:
                            buckets[i] = last
                    spark = [round(v, 2) for v in buckets]
                # P99 latency for the card stat row.
                lats = sorted(m.get('ipc_latency_ns', 0) / 1000.0 for m in messages
                              if m.get('ipc_latency_ns', 0) > 0)
                if lats:
                    p99_us = round(lats[min(len(lats) - 1, int(round(0.99 * (len(lats) - 1))))], 2)

            error_count = sum(
                1 for m in messages
                if (m.get('message_type') or '').startswith('ERROR_INDICATION')
            )
        except Exception as e:
            log.warning("vault read failed", extra={"filename": filename, "error": str(e)})

        files.append({
            "name": filename[:-5],
            "filename": filename,
            "size": st.st_size,
            "saved_date": st.st_mtime,
            "captured_date": captured_date,
            "message_count": message_count,
            "tag": metadata.get("tag", "archived"),
            "note": metadata.get("note", ""),
            "build_version": metadata.get("build_version", ""),
            "sparkline": spark,
            "duration_ms": duration_ms,
            "p99_us": p99_us,
            "error_count": error_count,
        })

    files.sort(key=lambda x: x['saved_date'], reverse=True)
    return {"files": files}


@app.post("/api/vault/save")
async def save_to_vault(request_data: dict):
    """Save current message stats to vault with metadata envelope."""
    vault_name = (request_data.get("name") or "").strip()
    if not vault_name:
        raise HTTPException(status_code=400, detail="Vault name is required")
    import re
    safe_name = re.sub(r'[^a-zA-Z0-9_\-\.]', '_', vault_name)
    if not os.path.exists(VAULT_DIR):
        os.makedirs(VAULT_DIR)
    vault_path = os.path.join(VAULT_DIR, f"{safe_name}.json")
    if os.path.exists(vault_path):
        raise HTTPException(status_code=409, detail="A file with this name already exists")

    tag = (request_data.get("tag") or "archived").strip()
    if tag not in VAULT_TAGS:
        tag = "archived"
    note = (request_data.get("note") or "").strip()
    build_version = (request_data.get("build_version") or "").strip()

    captured_at_ns = 0
    if message_data:
        captured_at_ns = message_data[0].get("timestamp_ns") or message_data[0].get("timestamp") or 0

    metadata = {
        "name": safe_name,
        "tag": tag,
        "note": note,
        "build_version": build_version,
        "captured_at_ns": captured_at_ns,
        "saved_at": datetime.now().timestamp(),
    }
    try:
        _write_vault(vault_path, message_data, metadata)
        return {
            "success": True,
            "message": f"Stats saved to vault as '{safe_name}'",
            "filename": f"{safe_name}.json",
            "message_count": len(message_data),
            "metadata": metadata,
        }
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Failed to save to vault: {e}")


@app.patch("/api/vault/{filename}")
async def update_vault_metadata(filename: str, request_data: dict):
    """Update tag/note/build_version on an existing vault file."""
    path = _vault_path(filename)
    if not os.path.exists(path):
        raise HTTPException(status_code=404, detail="Vault file not found")
    try:
        v = _read_vault(path)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Failed to read vault file: {e}")
    metadata = v["metadata"]
    if "tag" in request_data:
        tag = (request_data.get("tag") or "archived").strip()
        if tag not in VAULT_TAGS:
            raise HTTPException(status_code=400, detail=f"Invalid tag (allowed: {sorted(VAULT_TAGS)})")
        metadata["tag"] = tag
    if "note" in request_data:
        metadata["note"] = (request_data.get("note") or "").strip()
    if "build_version" in request_data:
        metadata["build_version"] = (request_data.get("build_version") or "").strip()
    try:
        _write_vault(path, v["messages"], metadata)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Failed to write vault file: {e}")
    return {"success": True, "metadata": metadata}


@app.post("/api/vault/load")
async def load_from_vault(request_data: dict):
    """Load stats data from vault into the active dataset."""
    filename = (request_data.get("filename") or "").strip()
    if not filename:
        raise HTTPException(status_code=400, detail="Filename is required")
    path = _vault_path(filename)
    if not os.path.exists(path):
        raise HTTPException(status_code=404, detail="Vault file not found")
    try:
        v = _read_vault(path)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Failed to load from vault: {e}")
    global message_data, stats_summary, current_data_source
    message_data = v["messages"]
    stats_summary = generate_stats_summary(message_data)
    stats_summary["file_path"] = path
    stats_summary["data_source"] = "vault"
    stats_summary["vault_session_name"] = filename.replace('.json', '')
    stats_summary["vault_metadata"] = v["metadata"]
    current_data_source = "vault"
    _rebuild_slot_index()
    _populate_time_diff(message_data)
    return {
        "success": True,
        "message": f"Loaded {len(message_data)} messages from vault: '{filename.replace('.json', '')}'",
        "message_count": len(message_data),
        "data_source": "vault",
        "metadata": v["metadata"],
    }


@app.delete("/api/vault/delete/{filename}")
async def delete_from_vault(filename: str):
    """Delete a stats file from vault."""
    path = _vault_path(filename)
    if not os.path.exists(path):
        raise HTTPException(status_code=404, detail="Vault file not found")
    try:
        os.remove(path)
        return {"success": True, "message": f"Deleted '{filename}' from vault"}
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Failed to delete from vault: {e}")

@app.post("/api/load-path")
async def load_xfapi_path(request_data: dict):
    """Load data from a specific XFAPI directory path (manual override).

    Disabled by default. Operators that need this functionality must set
    XFAPI_ALLOW_PATH_OVERRIDE=1 in the server's environment and ensure the
    target path is inside XFAPI_PATH_ROOT (defaults to the auto-detected
    XFAPI repo root).
    """
    global current_xfapi_path, current_data_source, _xfapi_path_source

    if not _path_override_allowed():
        raise HTTPException(
            status_code=403,
            detail="Path override is disabled (set XFAPI_ALLOW_PATH_OVERRIDE=1).",
        )

    xfapi_path = (request_data.get("path") or "").strip()
    if not xfapi_path:
        raise HTTPException(status_code=400, detail="Path is required")

    # Resolve + check against allowed root before doing anything else.
    resolved = _resolve_user_path(xfapi_path)

    current_xfapi_path = xfapi_path
    _xfapi_path_source = "manual"

    log.info("/api/load-path", extra={"path": xfapi_path, "resolved": resolved})
    current_data_source = "directory"
    success = load_json_data(xfapi_path)

    if not success:
        return {
            "success": False,
            "message": "Failed to load data from the requested path.",
            "error": stats_summary.get("error", "Unknown error"),
        }

    return {
        "success": True,
        "message": "Successfully loaded data from the requested path.",
        "stats": stats_summary,
    }

@app.get("/api/current-path")
async def get_current_path():
    """Get the currently configured XFAPI path.

    `source` is one of:
      - "env"        — resolved from $XFAPI_HOME at startup
      - "autodetect" — resolved by walking up from server/main.py
      - "fallback"   — startup couldn't resolve; using ~/XFAPI
      - "manual"     — user overrode via /api/load-path

    The client uses this to decide whether to surface the path as auto-
    detected (read-only on the Dashboard, editable only on Settings).
    """
    return {
        "current_path": current_xfapi_path,
        "full_file_path": stats_summary.get("file_path", "Not loaded"),
        "source": _xfapi_path_source,
    }

@app.post("/api/validate-path")
async def validate_xfapi_path(request_data: dict):
    """Check whether an XFAPI directory contains a usable message_stats.json.

    Same gating as /api/load-path: disabled unless
    XFAPI_ALLOW_PATH_OVERRIDE=1 is set, and paths are restricted to the
    configured root. Returns only a boolean — never the resolved filesystem
    path — to avoid being used as an existence oracle.
    """
    if not _path_override_allowed():
        raise HTTPException(
            status_code=403,
            detail="Path override is disabled (set XFAPI_ALLOW_PATH_OVERRIDE=1).",
        )

    xfapi_path = (request_data.get("path") or "").strip()
    if not xfapi_path:
        raise HTTPException(status_code=400, detail="Path is required")

    target = _resolve_user_path(xfapi_path)
    exists = os.path.exists(target)
    return {
        "valid": exists,
        "message": "Path is usable" if exists else "Path is not usable",
    }



@app.on_event("startup")
async def startup_event():
    """Load JSON data when server starts, using the auto-detected XFAPI path."""
    log.info("server starting", extra={"xfapi_path": current_xfapi_path,
                                       "source": _xfapi_path_source,
                                       "client_dist": _CLIENT_DIST})
    # Pass the resolved path so load_json_data() builds the full
    # {repo}/generated_logs/message_stats.json target — works regardless of
    # the process's current working directory.
    load_json_data(current_xfapi_path)

if __name__ == "__main__":
    # Run server accessible from all network interfaces
    uvicorn.run(
        app, 
        host="0.0.0.0",  # Allow access from other machines
        port=8080,
        reload=False,
        log_level="info"
    )