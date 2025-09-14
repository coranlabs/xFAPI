// Copyright 2024-2026 coRAN LABS Private Limited
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Theme tokens — ported from xfapi_dashboard_reference/data.jsx.
// Fixed defaults: cyan accent, comfortable density, Geist Mono, µs.

export const COLORS = {
  bg:        "oklch(0.16 0.012 250)",
  bgDeep:    "oklch(0.13 0.014 250)",
  surface:   "oklch(0.20 0.012 250)",
  surface2:  "oklch(0.23 0.013 250)",
  surface3:  "oklch(0.27 0.014 250)",
  border:    "oklch(0.28 0.012 250)",
  borderHi:  "oklch(0.36 0.013 250)",
  text:      "oklch(0.96 0.005 250)",
  textMuted: "oklch(0.72 0.012 250)",
  textDim:   "oklch(0.52 0.012 250)",
  textFaint: "oklch(0.40 0.012 250)",
  cyan:      "oklch(0.80 0.13 195)",
  green:     "oklch(0.78 0.14 145)",
  amber:     "oklch(0.80 0.14 75)",
  violet:    "oklch(0.74 0.14 290)",
  blue:      "oklch(0.74 0.14 245)",
  red:       "oklch(0.70 0.18 25)",
  gray:      "oklch(0.65 0.005 250)",
};

export const ACCENT = COLORS.cyan;
export const MONO = '"Geist Mono", ui-monospace, monospace';

export const groupColor = (g) =>
  ({
    sync:   COLORS.gray,
    dl:     COLORS.green,
    ul:     COLORS.cyan,
    data:   COLORS.violet,
    error:  COLORS.red,
    config: COLORS.amber,
    unknown: COLORS.gray,
  }[g] || COLORS.gray);

// Single source of truth for "what semantic group does this enhanced message
// type belong to?". Mirrors server-side _group_for() in main.py — keep in sync.
export function inferGroup(messageType) {
  if (!messageType) return "sync";
  const base = messageType.split(":", 1)[0].trim();
  if (base === "SLOT_INDICATION" || base === "P7_LAST_MESSAGE") return "sync";
  if (base.startsWith("DL_")) return "dl";
  if (base.startsWith("UL_")) return "ul";
  if (base === "TX_DATA_REQUEST" || base === "RX_DATA_INDICATION") return "data";
  if (base === "ERROR_INDICATION") return "error";
  if (base.startsWith("CONFIG_") || base.startsWith("START_") ||
      base.startsWith("STOP_")   || base.startsWith("PARAM_")) return "config";
  return "sync";
}

// ── Legacy row-color palette ─────────────────────────────────────────────
// These hex values are the standard naming convention from the previous
// dashboard — kept verbatim so saved screenshots and team muscle memory
// keep matching. Each rule maps a base message_type to {bg, fg}.
//   Row tint = bg (saturated)
//   Content-preview / actions columns use a lighter wash for legibility.
export function messageTypeStyle(messageType) {
  if (!messageType) return null;
  // Match against the BASE type (strip any "TYPE: PDU; …" suffix).
  const base = messageType.split(":", 1)[0].trim();
  if (base === "UL_TTI_REQUEST")     return { key: "UL_TTI",             bg: "#FF9999", fg: "#000000", soft: "#ffcccc" };
  if (base === "DL_TTI_REQUEST")     return { key: "DL_TTI",             bg: "#99FF99", fg: "#000000", soft: "#ccffcc" };
  if (base === "TX_DATA_REQUEST")    return { key: "TX_DATA",            bg: "#9999FF", fg: "#000000", soft: "#ccccff" };
  if (base === "UL_DCI_REQUEST")     return { key: "UL_DCI",             bg: "#FFFF99", fg: "#000000", soft: "#ffffcc" };
  if (base === "RACH_INDICATION")    return { key: "RACH_INDICATION",    bg: "#ff00ff", fg: "#000000", soft: "#ff99ff" };
  if (base === "CRC_INDICATION")     return { key: "CRC_INDICATION",     bg: "#9342e2", fg: "#ffffff", soft: "#c499f2" };
  if (base === "RX_DATA_INDICATION") return { key: "RX_DATA_INDICATION", bg: "#ebb45e", fg: "#000000", soft: "#f4d29b" };
  if (base === "UCI_INDICATION")     return { key: "UCI_INDICATION",     bg: "#6d0101", fg: "#ffffff", soft: "#a83d3d" };
  if (base === "SLOT_INDICATION")    return { key: "SLOT_INDICATION",    bg: "#CCCCFF", fg: "#000000", soft: "#e0e0ff" };
  return null;  // no legacy color → render with default theme
}

export const fmtNum = (n) =>
  n == null ? "—" : Number(n).toLocaleString("en-US");

export const fmtMicro = (us) => {
  if (us == null) return "—";
  return us < 10 ? us.toFixed(2) : us < 100 ? us.toFixed(1) : Math.round(us).toString();
};

export const fmtBytes = (b) => {
  if (b == null) return "—";
  if (b === 0) return "0 B";
  if (b <= 2048) return `${b} B`;
  const k = 1024;
  const units = ["B", "KB", "MB", "GB"];
  const i = Math.min(units.length - 1, Math.floor(Math.log(b) / Math.log(k)));
  return `${(b / Math.pow(k, i)).toFixed(1)} ${units[i]}`;
};

export const fmtDuration = (ms) => {
  if (ms == null) return "—";
  if (ms < 1000) return `${ms.toFixed(0)} ms`;
  const s = ms / 1000;
  if (s < 60) return `${s.toFixed(2)} s`;
  const m = Math.floor(s / 60);
  const rs = (s - m * 60).toFixed(0);
  return `${m}m ${rs}s`;
};

export const fmtDate = (epochSec) => {
  if (!epochSec) return "—";
  const d = new Date(epochSec * 1000);
  return d.toLocaleDateString() + " " + d.toLocaleTimeString();
};
