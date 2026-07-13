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

import React, { useEffect, useMemo, useState, useCallback } from "react";
import { COLORS, MONO, ACCENT, groupColor, inferGroup, messageTypeStyle, fmtNum, fmtMicro, fmtBytes, fmtDuration } from "../lib/theme";
import { api } from "../lib/api";
import { useToast } from "../lib/toast.jsx";
import { useThresholds } from "../lib/thresholds.jsx";
import KpiTile from "../components/KpiTile.jsx";
import SaveToVaultModal from "../components/SaveToVaultModal.jsx";
import PayloadHexSearch from "../components/PayloadHexSearch.jsx";
import LatencyChart from "../components/charts/LatencyChart.jsx";
import SfnSlotHeatmap from "../components/charts/SfnSlotHeatmap.jsx";
import MessageStream from "../components/charts/MessageStream.jsx";
import LatencyHistogram from "../components/charts/LatencyHistogram.jsx";

// Legacy preset (matches the old dashboard exactly):
//   - All message types EXCEPT "SLOT_INDICATION" and "UL_TTI_REQUEST: PRACH"
//   - All slot values EXCEPT 0
const PRESET_EXCLUDED_TYPES = new Set(["SLOT_INDICATION", "UL_TTI_REQUEST: PRACH"]);
const PRESET_EXCLUDED_SLOTS = new Set(["0"]);

// ── Path bar ───────────────────────────────────────────────────────────────
// Dashboard path bar.
// When `source === "manual"` we render an editable input (the user previously
// overrode the path, so we keep the same affordance). Otherwise the path was
// auto-detected by the server and we render it read-only with an "Edit in
// Settings" link — Settings is the source of truth for changing it.
function PathBar({ fullFilePath, source, onEditInSettings }) {
  const readOnly = source !== "manual";

  if (readOnly) {
    return (
      <div style={{
        display: "flex", alignItems: "center", gap: 12,
        padding: "8px 14px",
        background: COLORS.bgDeep,
        border: `1px solid ${COLORS.border}`, borderRadius: 6,
        marginBottom: 12,
      }}>
        <span style={{ fontSize: 12, fontWeight: 600, letterSpacing: "0.08em", color: COLORS.textFaint }}>DATA SOURCE</span>
        <span style={{
          padding: "1px 7px", borderRadius: 99,
          background: "oklch(from var(--accent) l c h / 0.14)",
          border: `1px solid ${ACCENT}`,
          color: COLORS.text,
          fontSize: 10, fontWeight: 600, letterSpacing: "0.06em", textTransform: "uppercase",
        }}>{source === "env" ? "env" : source === "fallback" ? "fallback" : "auto"}</span>
        <span style={{
          flex: 1, minWidth: 0,
          fontFamily: MONO, fontSize: 13, color: COLORS.textMuted,
          overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap",
        }} title={fullFilePath || ""}>
          {fullFilePath || "—"}
        </span>
        <button onClick={onEditInSettings} style={{
          padding: "5px 12px", borderRadius: 4,
          background: COLORS.surface, border: `1px solid ${COLORS.border}`,
          color: COLORS.textMuted, cursor: "pointer",
          fontSize: 12, fontWeight: 500,
        }}>Edit in Settings</button>
      </div>
    );
  }
  return null;  // manual mode: handled by ManualPathEditor below
}

// Editable variant — only shown when the user has explicitly overridden
// the path via /api/load-path or Settings. We keep it on the Dashboard so
// the manual-override workflow doesn't disappear into Settings.
function ManualPathEditor({ initialPath, onLoad }) {
  const [val, setVal] = useState(initialPath || "");
  const [valid, setValid] = useState(null);
  const [hint, setHint] = useState("");

  useEffect(() => { setVal(initialPath || ""); }, [initialPath]);
  useEffect(() => {
    if (!val) { setValid(null); setHint(""); return; }
    setValid("checking");
    const t = setTimeout(async () => {
      try {
        const r = await api.path.validate(val);
        setValid(!!r.valid);
        setHint(r.message || (r.valid ? "" : "not found"));
      } catch (e) {
        setValid(false); setHint(String(e.message || e));
      }
    }, 500);
    return () => clearTimeout(t);
  }, [val]);

  return (
    <div style={{
      display: "flex", alignItems: "center", gap: 10,
      padding: "10px 14px",
      background: COLORS.bgDeep,
      border: `1px solid ${COLORS.border}`, borderRadius: 6,
      marginBottom: 12,
    }}>
      <span style={{ fontSize: 12, fontWeight: 600, letterSpacing: "0.08em", color: COLORS.textFaint }}>DATA SOURCE</span>
      <span style={{ color: COLORS.textMuted, fontFamily: MONO, fontSize: 14 }}>~/</span>
      <input value={val} onChange={(e) => setVal(e.target.value)}
        placeholder="path/to/XFAPI"
        style={{
          flex: 1, minWidth: 200,
          padding: "6px 10px",
          background: COLORS.surface, color: COLORS.text,
          border: `1px solid ${COLORS.border}`, borderRadius: 4,
          fontFamily: MONO, fontSize: 14, outline: "none",
        }} />
      <span style={{ color: COLORS.textMuted, fontFamily: MONO, fontSize: 14 }}>/generated_logs/message_stats.json</span>
      <span style={{ width: 18, textAlign: "center" }}>
        {valid === "checking" && "⏳"}
        {valid === true && <span style={{ color: COLORS.green }}>●</span>}
        {valid === false && <span style={{ color: COLORS.red }}>●</span>}
      </span>
      <button onClick={() => onLoad(val)} disabled={valid !== true}
        style={{
          padding: "6px 12px", borderRadius: 4,
          background: valid === true ? "oklch(from var(--accent) l c h / 0.18)" : COLORS.surface,
          border: `1px solid ${valid === true ? ACCENT : COLORS.border}`,
          color: valid === true ? COLORS.text : COLORS.textMuted,
          cursor: valid === true ? "pointer" : "not-allowed",
          fontSize: 14, fontWeight: 500,
        }}>Load</button>
      {hint && valid !== true && (
        <span style={{ color: COLORS.textFaint, fontSize: 13 }}>{hint}</span>
      )}
    </div>
  );
}

// ── Top toolbar ────────────────────────────────────────────────────────────
function Toolbar({ stats, onRefresh, refreshing, onSaveToVault }) {
  const captureDate = stats?.capture_date_timestamp
    ? new Date(stats.capture_date_timestamp / 1e6).toLocaleString()
    : "—";
  const filePath = stats?.file_path || "—";
  const dataSource = stats?.data_source === "vault"
    ? `vault: ${stats?.vault_session_name || "?"}`
    : "directory";
  const total = stats?.total_messages || 0;
  return (
    <div style={{
      display: "flex", alignItems: "center", gap: 10,
      marginBottom: 12,
    }}>
      <button onClick={onRefresh} disabled={refreshing}
        style={{
          padding: "8px 16px", borderRadius: 6,
          background: "oklch(from var(--accent) l c h / 0.18)",
          border: `1px solid ${ACCENT}`,
          color: COLORS.text, cursor: refreshing ? "wait" : "pointer",
          fontSize: 15, fontWeight: 500,
          display: "flex", alignItems: "center", gap: 6,
        }}>
        <span style={{ display: "inline-block", transform: refreshing ? "rotate(180deg)" : "none", transition: "transform 0.4s" }}>↻</span>
        {refreshing ? "Refreshing…" : "Refresh"}
      </button>
      <button onClick={onSaveToVault} disabled={!total}
        title={total ? "Save current session to vault" : "Load data first"}
        style={{
          padding: "8px 16px", borderRadius: 6,
          background: total ? COLORS.surface : COLORS.bgDeep,
          border: `1px solid ${total ? COLORS.borderHi : COLORS.border}`,
          color: total ? COLORS.text : COLORS.textFaint,
          cursor: total ? "pointer" : "not-allowed",
          fontSize: 15, fontWeight: 500,
          display: "flex", alignItems: "center", gap: 6,
        }}>
        <span>💾</span> Save to Vault
      </button>
      {stats?.mode && (
        <div title={stats?.topology || "xFAPI bridge mode"}
          style={{
            display: "flex", alignItems: "center", gap: 8,
            padding: "6px 14px", borderRadius: 6,
            background: "oklch(from var(--accent) l c h / 0.12)",
            border: `1px solid ${ACCENT}`,
            fontFamily: MONO, fontSize: 14, fontWeight: 600,
            color: COLORS.text, letterSpacing: "0.02em",
          }}>
          <span style={{ fontSize: 12, fontWeight: 500, color: COLORS.textFaint, letterSpacing: "0.08em" }}>xFAPI</span>
          <span style={{ color: ACCENT }}>{stats.mode}</span>
        </div>
      )}
      <div style={{ flex: 1 }} />
      <div style={{ fontSize: 13, color: COLORS.textMuted, fontFamily: MONO }}>
        <span style={{ color: COLORS.textFaint }}>SOURCE</span> {dataSource}
      </div>
      <div style={{ fontSize: 13, color: COLORS.textMuted, fontFamily: MONO, maxWidth: 380, overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }} title={filePath}>
        <span style={{ color: COLORS.textFaint }}>FILE</span> {filePath}
      </div>
      <div style={{ fontSize: 13, color: COLORS.textMuted, fontFamily: MONO }}>
        <span style={{ color: COLORS.textFaint }}>CAPTURED</span> {captureDate}
      </div>
    </div>
  );
}

// ── Filter group (checkbox grid) ──────────────────────────────────────────
function CheckboxGrid({ label, items, selected, onChange, columns = 1 }) {
  const allOn = items.length > 0 && selected.size === items.length;
  const setAll = (on) => onChange(on ? new Set(items.map(String)) : new Set());
  return (
    <div style={{ flex: 1, minWidth: 180 }}>
      <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", marginBottom: 6 }}>
        <span style={{ fontSize: 12, fontWeight: 600, letterSpacing: "0.08em", color: COLORS.textFaint }}>{label}</span>
        <div style={{ display: "flex", gap: 4 }}>
          <button onClick={() => setAll(true)} style={miniBtn(allOn)}>ALL</button>
          <button onClick={() => setAll(false)} style={miniBtn(!selected.size)}>NONE</button>
        </div>
      </div>
      <div style={{
        maxHeight: 180, overflow: "auto",
        display: "grid", gridTemplateColumns: `repeat(${columns}, 1fr)`, gap: 4,
        padding: 6,
        background: COLORS.surface, border: `1px solid ${COLORS.border}`, borderRadius: 4,
      }}>
        {items.map((it) => {
          const sk = String(it);
          const on = selected.has(sk);
          return (
            <label key={sk} style={{
              display: "flex", alignItems: "center", gap: 6,
              padding: "3px 6px", borderRadius: 3,
              fontSize: 13, color: on ? COLORS.text : COLORS.textMuted,
              background: on ? "oklch(from var(--accent) l c h / 0.12)" : "transparent",
              cursor: "pointer", fontFamily: MONO,
              overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap",
            }}>
              <input type="checkbox" checked={on}
                onChange={() => {
                  const next = new Set(selected);
                  if (on) next.delete(sk); else next.add(sk);
                  onChange(next);
                }}
                style={{ accentColor: ACCENT, cursor: "pointer" }} />
              <span style={{ overflow: "hidden", textOverflow: "ellipsis" }}>{sk}</span>
            </label>
          );
        })}
      </div>
    </div>
  );
}
const miniBtn = (active) => ({
  padding: "2px 6px", borderRadius: 3,
  background: active ? "oklch(from var(--accent) l c h / 0.18)" : COLORS.surface2,
  border: `1px solid ${active ? ACCENT : COLORS.border}`,
  color: active ? COLORS.text : COLORS.textMuted,
  fontSize: 11, fontWeight: 600, letterSpacing: "0.06em",
  cursor: "pointer",
});

// ── Latency cell colorizer ────────────────────────────────────────────────
function LatencyCell({ ns }) {
  if (!ns || ns <= 0) return <span style={{ color: COLORS.textFaint }}>—</span>;
  const us = ns / 1000;
  const c = us < 10 ? COLORS.green : us < 50 ? COLORS.amber : COLORS.red;
  return <span style={{ color: c, fontFamily: MONO }}>{fmtMicro(us)}</span>;
}

// ── Message detail modal ──────────────────────────────────────────────────
function MessageDetailModal({ msg, onClose }) {
  const TH = useThresholds();
  const [copied, setCopied] = useState(false);
  // ESC closes
  useEffect(() => {
    if (!msg) return;
    const onKey = (e) => { if (e.key === "Escape") onClose(); };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [msg, onClose]);
  if (!msg) return null;

  const lat = msg.ipc_latency_ns / 1000;
  const mts = messageTypeStyle(msg.base_message_type || msg.message_type);
  const direction = msg.direction === "L2_TO_L1" ? "L2 → L1"
                  : msg.direction === "L1_TO_L2" ? "L1 → L2" : "—";

  const copy = async () => {
    try {
      await navigator.clipboard.writeText(msg.full_content || "");
      setCopied(true);
      setTimeout(() => setCopied(false), 1500);
    } catch {}
  };

  return (
    <div onClick={onClose} style={{
      position: "fixed", inset: 0, zIndex: 9998,
      background: "rgba(0,0,0,0.6)", backdropFilter: "blur(4px)",
      display: "flex", alignItems: "center", justifyContent: "center",
      padding: 24,
    }}>
      <div onClick={(e) => e.stopPropagation()} style={{
        width: "min(960px, 96vw)", maxHeight: "90vh",
        display: "flex", flexDirection: "column",
        background: COLORS.bg, border: `1px solid ${COLORS.borderHi}`,
        borderRadius: 8, overflow: "hidden",
        boxShadow: "0 20px 60px rgba(0,0,0,0.5)",
      }}>
        {/* Header — colored bar matching the message type. flexShrink: 0
            keeps it pinned at the top even when the dialog hits maxHeight. */}
        <div style={{
          padding: "14px 18px",
          borderBottom: `1px solid ${COLORS.border}`,
          display: "flex", alignItems: "center", gap: 14,
          background: mts ? mts.bg : COLORS.surface,
          color: mts ? mts.fg : COLORS.text,
          flexShrink: 0,
        }}>
          <div style={{ flex: 1, minWidth: 0 }}>
            <div style={{ fontSize: 11, letterSpacing: "0.08em", fontWeight: 600, opacity: 0.7 }}>MESSAGE DETAILS</div>
            <div style={{ fontFamily: MONO, fontSize: 18, fontWeight: 600, marginTop: 2,
                          overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }}>
              {msg.message_type}
            </div>
          </div>
          <div style={{ fontFamily: MONO, fontSize: 13, opacity: 0.85, whiteSpace: "nowrap" }}>
            <span style={{ opacity: 0.7 }}>id</span> #{msg.id}
            <span style={{ marginLeft: 14, opacity: 0.7 }}>dir</span> {direction}
          </div>
          <button onClick={onClose} title="Close (Esc)"
            style={{
              width: 32, height: 32, borderRadius: 4,
              background: "rgba(0,0,0,0.18)", border: "none",
              color: "inherit", cursor: "pointer",
              fontSize: 22, lineHeight: 1,
              display: "flex", alignItems: "center", justifyContent: "center",
            }}>×</button>
        </div>

        {/* Body — flex column. The body itself does NOT scroll; only the
            FULL CONTENT <pre> does. That keeps the stat tiles + timestamp
            row pinned regardless of message length.
            `minHeight: 0` is required to let the inner <pre> shrink below
            its natural content height inside a flex parent. */}
        <div style={{
          padding: 18, display: "flex", flexDirection: "column", gap: 14,
          flex: 1, minHeight: 0, overflow: "hidden",
        }}>
          {/* Stat grid — 6 fields. flexShrink: 0 keeps it pinned. */}
          <div style={{
            display: "grid", gridTemplateColumns: "repeat(6, 1fr)", gap: 1,
            background: COLORS.border, border: `1px solid ${COLORS.border}`, borderRadius: 4,
            overflow: "hidden", flexShrink: 0,
          }}>
            <Stat k="SFN" v={msg.sfn} />
            <Stat k="Slot" v={msg.slot} />
            <Stat k="PDUs" v={msg.num_pdus ?? "—"} />
            <Stat k="Size" v={msg.pdu_size ? fmtBytes(msg.pdu_size) : "—"} />
            <Stat k="IPC Latency"
                  v={lat > 0 ? `${fmtMicro(lat)} µs` : "—"}
                  accent={lat > 0
                    ? (lat > TH.outlier_us ? COLORS.red
                      : lat > TH.sla_us   ? COLORS.amber
                      : COLORS.green)
                    : null} />
            <Stat k="Time Diff" v={msg.time_diff_us != null ? `${fmtNum(msg.time_diff_us)} µs` : "—"} />
          </div>

          {/* Timestamp row — also pinned */}
          <div style={{ display: "flex", gap: 24, fontSize: 13, color: COLORS.textMuted, fontFamily: MONO, flexShrink: 0 }}>
            <div><span style={{ color: COLORS.textFaint }}>TIMESTAMP_NS</span> {msg.timestamp}</div>
            <div><span style={{ color: COLORS.textFaint }}>BASE_TYPE</span> {msg.base_message_type || "—"}</div>
          </div>

          {/* Full content — only this section scrolls. */}
          <div style={{ display: "flex", flexDirection: "column", flex: 1, minHeight: 0 }}>
            <div style={{ display: "flex", alignItems: "center", marginBottom: 6, flexShrink: 0 }}>
              <span style={{ fontSize: 12, fontWeight: 600, letterSpacing: "0.08em", color: COLORS.textFaint }}>FULL CONTENT</span>
              <span style={{ marginLeft: 10, fontSize: 12, color: COLORS.textFaint, fontFamily: MONO }}>
                {(msg.full_content || "").length.toLocaleString()} chars
              </span>
              <div style={{ flex: 1 }} />
              <button onClick={copy}
                disabled={!msg.full_content}
                style={{
                  padding: "4px 12px", borderRadius: 4,
                  background: copied ? "oklch(from var(--accent) l c h / 0.22)" : COLORS.surface,
                  border: `1px solid ${copied ? ACCENT : COLORS.border}`,
                  color: copied ? COLORS.text : COLORS.textMuted,
                  fontSize: 12, fontWeight: 500, cursor: "pointer",
                }}>{copied ? "Copied ✓" : "Copy"}</button>
            </div>
            <pre style={{
              margin: 0, padding: 12, flex: 1, minHeight: 0,
              background: COLORS.bgDeep,
              border: `1px solid ${COLORS.border}`, borderRadius: 4,
              color: COLORS.text, fontFamily: MONO, fontSize: 13, lineHeight: 1.5,
              overflow: "auto", whiteSpace: "pre-wrap", wordBreak: "break-all",
            }}>{msg.full_content || "(no content)"}</pre>
          </div>
        </div>
      </div>
    </div>
  );
}

const Stat = ({ k, v, accent }) => (
  <div style={{ background: COLORS.bgDeep, padding: "10px 12px" }}>
    <div style={{ fontSize: 11, color: COLORS.textFaint, letterSpacing: "0.08em", fontWeight: 600 }}>{k.toUpperCase()}</div>
    <div style={{ fontFamily: MONO, fontSize: 16, color: accent || COLORS.text, marginTop: 4 }}>{v}</div>
  </div>
);

// ── Right-rail message-type summary ───────────────────────────────────────
// `fillHeight` makes the panel stretch to fill its grid cell instead of
// being capped at maxHeight. Used in the new top-right placement so it
// lines up with the KPI strip + chart rows.
function TypeSummary({ stats, fillHeight = false }) {
  const items = useMemo(() => {
    const c = stats?.message_type_counts || {};
    // No more slice(0, 12) — show every type, scrollable inside the panel.
    return Object.entries(c).sort((a, b) => b[1] - a[1]);
  }, [stats]);
  const max = items.length ? items[0][1] : 1;
  return (
    <div style={{
      flex: 1, alignSelf: "stretch",
      display: "flex", flexDirection: "column", gap: 4,
      padding: "10px 12px",
      background: COLORS.bgDeep,
      border: `1px solid ${COLORS.border}`, borderRadius: 6,
      ...(fillHeight ? { maxHeight: "100%", overflow: "auto" }
                     : { maxHeight: 480, overflow: "auto" }),
    }}>
      <div style={{
        fontSize: 12, fontWeight: 600, letterSpacing: "0.08em",
        color: COLORS.textFaint, marginBottom: 6,
        position: "sticky", top: 0, background: COLORS.bgDeep, zIndex: 1,
        paddingBottom: 4, borderBottom: `1px solid ${COLORS.border}`,
      }}>MESSAGE TYPE COUNTS <span style={{ color: COLORS.text, marginLeft: 6 }}>({items.length})</span></div>
      {items.map(([t, n]) => {
        const mts = messageTypeStyle(t);
        const c = mts ? mts.bg : groupColor(inferGroup(t));
        return (
          <div key={t} style={{ display: "flex", alignItems: "center", gap: 6, fontSize: 13 }}>
            <span style={{ width: 8, height: 8, background: c, borderRadius: 2, flexShrink: 0, border: `1px solid ${COLORS.border}` }} />
            <span style={{
              flex: 1, fontFamily: MONO, color: COLORS.textMuted,
              overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap",
            }} title={t}>{t}</span>
            <span style={{ position: "relative", width: 70, height: 4, background: COLORS.surface2, borderRadius: 2, overflow: "hidden", flexShrink: 0 }}>
              <span style={{ position: "absolute", left: 0, top: 0, bottom: 0, width: `${(n / max) * 100}%`, background: c, opacity: 0.8 }} />
            </span>
            <span style={{ fontFamily: MONO, color: COLORS.text, minWidth: 56, textAlign: "right" }}>{fmtNum(n)}</span>
          </div>
        );
      })}
    </div>
  );
}
// ── The Dashboard page itself ────────────────────────────────────────────
export default function Dashboard({ onOpenSettings }) {
  const toast = useToast();
  const TH = useThresholds();
  const [stats, setStats] = useState(null);
  const [series, setSeries] = useState(null);
  const [messages, setMessages] = useState([]);
  const [pagination, setPagination] = useState({ page: 1, limit: 100, total_count: 0, total_pages: 1, has_next: false, has_prev: false });
  const [pageSize, setPageSize] = useState(100);
  const [page, setPage] = useState(1);
  const [sortBy, setSortBy] = useState("timestamp");
  const [sortOrder, setSortOrder] = useState("desc");
  const [selTypes, setSelTypes] = useState(new Set());
  const [selSfn, setSelSfn] = useState(new Set());
  const [selSlot, setSelSlot] = useState(new Set());
  const [search, setSearch] = useState("");
  const [appliedSearch, setAppliedSearch] = useState("");
  const [selected, setSelected] = useState(null);
  const [refreshing, setRefreshing] = useState(false);
  // Full path info: {current_path, full_file_path, source}. Source decides
  // whether the bar is read-only (auto/env/fallback) or editable (manual).
  const [pathInfo, setPathInfo] = useState({ current_path: "", full_file_path: "", source: "autodetect" });
  const [saveOpen, setSaveOpen] = useState(false);

  const loadAll = useCallback(async () => {
    try {
      const [s, t] = await Promise.all([api.stats(), api.timeseries(240)]);
      setStats(s);
      setSeries(t);
    } catch (e) {
      toast.error("Failed to load stats: " + e.message);
    }
  }, [toast]);

  // Initial load. /api/current-path tells us the resolved path + how it
  // was found (autodetect / env / fallback / manual).
  const refreshPathInfo = useCallback(() =>
    api.path.current()
      .then((p) => setPathInfo({ current_path: p?.current_path || "", full_file_path: p?.full_file_path || "", source: p?.source || "autodetect" }))
      .catch(() => {})
  , []);
  useEffect(() => {
    refreshPathInfo();
    loadAll();
  }, [refreshPathInfo, loadAll]);

  // Load messages whenever query changes
  const reloadMessages = useCallback(async () => {
    const params = {
      page, limit: pageSize, sort_by: sortBy, sort_order: sortOrder,
    };
    if (selTypes.size) params.message_type = Array.from(selTypes).join(",");
    // Discrete-set filters: send the exact selected values, not min/max,
    // so unchecked items between min and max are actually excluded.
    if (selSfn.size)  params.sfn_in  = Array.from(selSfn).join(",");
    if (selSlot.size) params.slot_in = Array.from(selSlot).join(",");
    if (appliedSearch) params.search = appliedSearch;
    try {
      const r = await api.messages(params);
      setMessages(r.messages);
      setPagination(r.pagination);
    } catch (e) {
      toast.error("Failed to load messages: " + e.message);
    }
  }, [page, pageSize, sortBy, sortOrder, selTypes, selSfn, selSlot, appliedSearch, toast]);

  useEffect(() => { reloadMessages(); }, [reloadMessages]);

  const onRefresh = async () => {
    setRefreshing(true);
    try {
      const r = await api.refresh();
      await loadAll();
      await reloadMessages();
      toast.success(r.message || "Refreshed");
    } catch (e) {
      toast.error("Refresh failed: " + e.message);
    } finally {
      setRefreshing(false);
    }
  };

  const onLoadPath = async (path) => {
    setRefreshing(true);
    try {
      const r = await api.path.load(path);
      if (!r.success) throw new Error(r.error || r.message || "load failed");
      await loadAll();
      setPage(1);
      await reloadMessages();
      await refreshPathInfo();  // source flips to "manual"
      toast.success(`Loaded ${r.stats?.total_messages?.toLocaleString() || "?"} messages`);
    } catch (e) {
      toast.error("Load failed: " + e.message);
    } finally {
      setRefreshing(false);
    }
  };

  const onSelect = async (id) => {
    try {
      const m = await api.message(id);
      setSelected(m);
    } catch (e) {
      toast.error("Failed to load message: " + e.message);
    }
  };

  // Jump to a hex-search match. We clear the filters that would hide the
  // target row, jump to the right page, then open the detail modal so the
  // user immediately sees the matching payload.
  const onJumpToMatch = ({ messageId, page: targetPage }) => {
    setSelTypes(new Set());
    setSelSfn(new Set());
    setSelSlot(new Set());
    setAppliedSearch("");
    setSearch("");
    setPage(targetPage);
    onSelect(messageId);
  };

  const applyPreset = () => {
    if (!stats?.message_types?.length) {
      toast.info("Load data first");
      return;
    }
    const types = new Set(stats.message_types.filter((t) => !PRESET_EXCLUDED_TYPES.has(t)));
    const slots = new Set((stats.slot_values || []).map(String).filter((s) => !PRESET_EXCLUDED_SLOTS.has(s)));
    setSelTypes(types);
    setSelSfn(new Set());
    setSelSlot(slots);
    setSearch("");
    setAppliedSearch("");
    setPage(1);
    toast.success(`Preset applied (${types.size} types, ${slots.size} slots)`);
  };

  const headerCell = (key, label, w) => (
    <th key={key} onClick={() => {
      if (sortBy === key) setSortOrder(sortOrder === "asc" ? "desc" : "asc");
      else { setSortBy(key); setSortOrder("desc"); }
    }} style={{
      textAlign: "left", padding: "8px 10px",
      borderBottom: `1px solid ${COLORS.border}`,
      fontSize: 11, fontWeight: 600, letterSpacing: "0.08em",
      color: COLORS.textFaint, cursor: "pointer", userSelect: "none",
      whiteSpace: "nowrap", width: w,
    }}>
      {label.toUpperCase()}
      {sortBy === key && <span style={{ marginLeft: 4, color: ACCENT }}>{sortOrder === "asc" ? "↑" : "↓"}</span>}
    </th>
  );

  const lat = stats?.ipc_latency_stats || {};
  return (
    <div style={{ padding: "18px 18px 24px", minHeight: "100%" }}>
      <Toolbar stats={stats} onRefresh={onRefresh} refreshing={refreshing} onSaveToVault={() => setSaveOpen(true)} />
      {/* Auto-detected path → read-only bar with link to Settings.
          Manual override (set via Settings or /api/load-path) → editable bar. */}
      {pathInfo.source === "manual" ? (
        <ManualPathEditor
          initialPath={(pathInfo.current_path || "").replace(/^~\//, "")}
          onLoad={onLoadPath}
        />
      ) : (
        <PathBar
          fullFilePath={pathInfo.full_file_path}
          source={pathInfo.source}
          onEditInSettings={() => onOpenSettings?.()}
        />
      )}

      {/* Top section: stats + charts on the left, message-type counts on the
          right. Lets the messages table below claim full page width. */}
      <div style={{ display: "grid", gridTemplateColumns: "minmax(0, 1fr) 320px", gap: 12, marginBottom: 14 }}>
        <div style={{ display: "flex", flexDirection: "column", gap: 8, minWidth: 0 }}>
          {/* KPI strip */}
          <div style={{ display: "flex", gap: 8, flexWrap: "wrap" }}>
            <KpiTile label="MESSAGES" value={fmtNum(stats?.total_messages)} sub={`${stats?.message_types?.length || 0} types`} />
            <KpiTile label="ERRORS" value={fmtNum(stats?.error_count)} accentValue={stats?.error_count > 0 ? COLORS.red : COLORS.text} />
            <KpiTile label="DURATION" value={fmtDuration(stats?.duration_ms)} />
            <KpiTile label="P50 / P95 µs" value={`${fmtMicro(lat.p50_us)} / ${fmtMicro(lat.p95_us)}`} accentValue={COLORS.green} />
            <KpiTile label="P99 / P99.9 µs" value={`${fmtMicro(lat.p99_us)} / ${fmtMicro(lat.p999_us)}`} accentValue={COLORS.amber} />
            <KpiTile label="MIN / MEAN / MAX µs" value={`${fmtMicro(lat.min_us)} / ${fmtMicro(lat.mean_us)} / ${fmtMicro(lat.max_us)}`} />
            <KpiTile label="OUTLIERS" value={fmtNum(lat.outliers)} sub={`>${TH.outlier_us}µs`} accentValue={lat.outliers > 0 ? COLORS.amber : COLORS.text} />
          </div>

          {/* Chart row 1 — latency time-series + log-binned histogram */}
          <div style={{ display: "grid", gridTemplateColumns: "1.4fr 1fr", gap: 8 }}>
            <ChartCard title="DL IPC LATENCY (LOG µs)">
              <LatencyChart data={series?.latency_trace || []} width={620} height={200}
                            slaThreshold={TH.sla_us}
                            outlierThreshold={TH.outlier_us}
                            hardOutlier={TH.hard_outlier_us} />
            </ChartCard>
            <ChartCard title="LATENCY HISTOGRAM (µs)">
              <LatencyHistogram
                bins={series?.histogram?.bins_us || TH.histogram_bins_us}
                counts={series?.histogram?.counts || []}
                width={460} height={200}
                slaUs={TH.sla_us} outlierUs={TH.outlier_us} />
            </ChartCard>
          </div>

          {/* Chart row 2 — frame heatmap + message-type stream */}
          <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 8 }}>
            <ChartCard title="SFN × SLOT HEATMAP">
              <SfnSlotHeatmap
                cells={series?.heatmap?.cells || []}
                sfnMin={series?.heatmap?.sfn_min || 0}
                sfnMax={series?.heatmap?.sfn_max || 0}
                sfnStep={series?.heatmap?.sfn_step || 1}
                slotCount={series?.heatmap?.slot_count || TH.slot_count}
                width={540} height={180}
              />
            </ChartCard>
            <ChartCard title="MESSAGE TYPE STREAM">
              <MessageStream
                types={series?.rate_stream?.types || []}
                buckets={series?.rate_stream?.buckets || []}
                width={540} height={160}
              />
            </ChartCard>
          </div>
        </div>

        {/* Right rail — message type counts. Same height as the left column. */}
        <div style={{ display: "flex", minWidth: 0 }}>
          <TypeSummary stats={stats} fillHeight />
        </div>
      </div>

      {/* Hex payload search (jumps to match in TX_DATA / RX_DATA only) */}
      <PayloadHexSearch onJump={onJumpToMatch} pageSize={pageSize} />

      {/* Filters */}
      <div style={{
        background: COLORS.bgDeep, border: `1px solid ${COLORS.border}`, borderRadius: 6,
        padding: 12, marginBottom: 12,
      }}>
        <div style={{ display: "flex", alignItems: "center", justifyContent: "space-between", marginBottom: 8 }}>
          <div style={{ fontSize: 13, fontWeight: 600, letterSpacing: "0.08em", color: COLORS.textFaint }}>FILTERS &amp; SEARCH</div>
          <div style={{ display: "flex", gap: 6 }}>
            <button onClick={applyPreset}
              title="Select all message types except SLOT_INDICATION & UL_TTI_REQUEST: PRACH, and all slots except 0"
              style={presetBtnStyle}>Apply Preset</button>
            <button onClick={() => { setSelTypes(new Set()); setSelSfn(new Set()); setSelSlot(new Set()); setSearch(""); setAppliedSearch(""); setPage(1); }}
              style={btnStyle(false)}>Clear All</button>
            <button onClick={() => { setAppliedSearch(search); setPage(1); }} style={btnStyle(true)}>Apply</button>
          </div>
        </div>
        <input
          placeholder="Search payload bytes (hex) or content…"
          value={search}
          onChange={(e) => setSearch(e.target.value)}
          onKeyDown={(e) => { if (e.key === "Enter") { setAppliedSearch(search); setPage(1); } }}
          style={{
            width: "100%", padding: "8px 10px",
            background: COLORS.surface, color: COLORS.text,
            border: `1px solid ${COLORS.border}`, borderRadius: 4,
            fontFamily: MONO, fontSize: 14, outline: "none",
            marginBottom: 10,
          }}
        />
        <div style={{ display: "flex", gap: 12, flexWrap: "wrap" }}>
          <CheckboxGrid label="MESSAGE TYPES"
            items={(stats?.message_types || [])} selected={selTypes}
            onChange={(s) => { setSelTypes(s); setPage(1); }} />
          <CheckboxGrid label="SFN VALUES"
            items={(stats?.sfn_values || []).map(String)} selected={selSfn}
            onChange={(s) => { setSelSfn(s); setPage(1); }} columns={3} />
          <CheckboxGrid label="SLOT VALUES"
            items={(stats?.slot_values || []).map(String)} selected={selSlot}
            onChange={(s) => { setSelSlot(s); setPage(1); }} columns={3} />
        </div>
      </div>

      {/* Messages table — full width */}
      <div>
        <div style={{ background: COLORS.bgDeep, border: `1px solid ${COLORS.border}`, borderRadius: 6, overflow: "hidden" }}>
          <div style={{ display: "flex", alignItems: "center", padding: "8px 12px", borderBottom: `1px solid ${COLORS.border}`, gap: 12 }}>
            <span style={{ fontSize: 13, fontWeight: 600, letterSpacing: "0.08em", color: COLORS.textFaint }}>
              MESSAGES <span style={{ color: COLORS.text, fontWeight: 500 }}>({fmtNum(pagination.total_count)})</span>
            </span>
            <div style={{ flex: 1 }} />
            <button disabled={!pagination.has_prev} onClick={() => setPage((p) => Math.max(1, p - 1))} style={btnStyle(false)}>‹ Prev</button>
            <span style={{ fontSize: 13, color: COLORS.textMuted, fontFamily: MONO }}>
              {pagination.page} / {pagination.total_pages}
            </span>
            <button disabled={!pagination.has_next} onClick={() => setPage((p) => p + 1)} style={btnStyle(false)}>Next ›</button>
            <select value={pageSize} onChange={(e) => { setPageSize(parseInt(e.target.value)); setPage(1); }}
              style={{ background: COLORS.surface, color: COLORS.text, border: `1px solid ${COLORS.border}`, borderRadius: 4, fontSize: 13, padding: "4px 6px" }}>
              {[50, 100, 200].map((n) => <option key={n} value={n}>{n}/page</option>)}
            </select>
          </div>
          <div style={{ overflow: "auto", maxHeight: 600 }}>
            <table style={{ width: "100%", borderCollapse: "collapse", fontFamily: MONO, fontSize: 13 }}>
              <thead style={{ position: "sticky", top: 0, background: COLORS.bgDeep, zIndex: 1 }}>
                <tr>
                  {headerCell("id", "S.No", 70)}
                  {headerCell("timestamp", "Timestamp", 130)}
                  {headerCell("time_diff_us", "ΔTime µs", 90)}
                  {headerCell("ipc_latency_ns", "IPC Lat µs", 100)}
                  {headerCell("sfn", "SFN", 60)}
                  {headerCell("slot", "Slot", 50)}
                  {headerCell("message_type", "Type", null)}
                  {headerCell("num_pdus", "PDUs", 60)}
                  {headerCell("pdu_size", "Size", 80)}
                  <th style={thStatic}>Preview</th>
                </tr>
              </thead>
              <tbody>
                {messages.map((m) => {
                  const isSelected = selected?.id === m.id;
                  const lat_us = m.ipc_latency_ns / 1000;
                  const mts = messageTypeStyle(m.base_message_type || m.message_type);
                  // Legacy color wins; if no match, fall back to latency-tinted theme row.
                  const rowBg = mts ? mts.bg :
                                lat_us > TH.outlier_us ? "oklch(0.32 0.18 25 / 0.08)" :
                                lat_us > TH.sla_us     ? "oklch(0.32 0.14 75 / 0.08)" : "transparent";
                  const rowFg = mts ? mts.fg : COLORS.text;
                  const softBg = mts ? mts.soft : rowBg;
                  const softFg = mts ? mts.fg : COLORS.textMuted;
                  const cellBase = { padding: "6px 10px", color: rowFg };
                  const softCell = { ...cellBase, background: softBg, color: softFg };
                  const selectedOverlay = isSelected ? { boxShadow: `inset 3px 0 0 ${ACCENT}` } : null;
                  return (
                    <tr key={m.id} onClick={() => onSelect(m.id)}
                      style={{
                        cursor: "pointer",
                        background: rowBg,
                        borderBottom: `1px solid ${COLORS.surface}`,
                        ...(selectedOverlay || {}),
                        opacity: isSelected ? 1 : 0.96,
                      }}>
                      <td style={cellBase}>{m.id}</td>
                      <td style={cellBase}>{String(m.timestamp).slice(-12)}</td>
                      <td style={cellBase}>{m.time_diff_us != null ? fmtNum(m.time_diff_us) : "—"}</td>
                      <td style={cellBase}>
                        {/* Latency cell preserves its own green/amber/red color over an opaque pill,
                            so it stays legible regardless of row tint. */}
                        {m.ipc_latency_ns > 0
                          ? <span style={{
                              padding: "1px 6px", borderRadius: 3,
                              background: "rgba(0,0,0,0.55)",
                              color: lat_us < TH.sla_us ? "#7CFFA9" : lat_us < TH.outlier_us ? "#FFD580" : "#FF8E8E",
                              fontFamily: MONO,
                            }}>{fmtMicro(lat_us)}</span>
                          : <span style={{ color: rowFg, opacity: 0.55 }}>—</span>}
                      </td>
                      <td style={cellBase}>{m.sfn}</td>
                      <td style={cellBase}>{m.slot}</td>
                      <td style={{ ...cellBase, fontWeight: 500, whiteSpace: "nowrap" }}>
                        {m.message_type}
                        {m.direction && (
                          <span style={{ marginLeft: 6, fontSize: 11, opacity: 0.65 }}>
                            {m.direction === "L2_TO_L1" ? "L2→L1" : m.direction === "L1_TO_L2" ? "L1→L2" : ""}
                          </span>
                        )}
                      </td>
                      <td style={cellBase}>{m.num_pdus || "—"}</td>
                      <td style={cellBase}>{m.pdu_size ? fmtBytes(m.pdu_size) : "—"}</td>
                      <td style={{ ...softCell, maxWidth: 260, overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }}
                          dangerouslySetInnerHTML={{ __html: m.content_preview || "" }} />
                    </tr>
                  );
                })}
                {messages.length === 0 && (
                  <tr><td colSpan={10} style={{ padding: 24, textAlign: "center", color: COLORS.textFaint }}>No messages match the filters.</td></tr>
                )}
              </tbody>
            </table>
          </div>
        </div>

      </div>

      {saveOpen && (
        <SaveToVaultModal
          onClose={() => setSaveOpen(false)}
          onSaved={() => setSaveOpen(false)}
        />
      )}

      <MessageDetailModal msg={selected} onClose={() => setSelected(null)} />
    </div>
  );
}

const presetBtnStyle = {
  padding: "5px 10px", borderRadius: 4,
  background: "oklch(from var(--accent) l c h / 0.08)",
  border: `1px dashed ${ACCENT}`,
  color: COLORS.text,
  fontSize: 13, fontWeight: 500,
  cursor: "pointer",
};

const thStatic = {
  textAlign: "left", padding: "8px 10px",
  borderBottom: `1px solid ${COLORS.border}`,
  fontSize: 11, fontWeight: 600, letterSpacing: "0.08em",
  color: COLORS.textFaint, whiteSpace: "nowrap",
};
const ChartCard = ({ title, children }) => (
  <div style={{
    background: COLORS.bgDeep, border: `1px solid ${COLORS.border}`,
    borderRadius: 6, padding: "10px 12px", overflow: "hidden",
  }}>
    <div style={{ fontSize: 11, fontWeight: 600, letterSpacing: "0.08em", color: COLORS.textFaint, marginBottom: 6 }}>{title}</div>
    {children}
  </div>
);
const btnStyle = (primary) => ({
  padding: "5px 10px", borderRadius: 4,
  background: primary ? "oklch(from var(--accent) l c h / 0.18)" : COLORS.surface,
  border: `1px solid ${primary ? ACCENT : COLORS.border}`,
  color: primary ? COLORS.text : COLORS.textMuted,
  fontSize: 13, fontWeight: 500,
  cursor: "pointer",
});
