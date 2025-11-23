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

import React, { useEffect, useState } from "react";
import { COLORS, MONO, ACCENT } from "../lib/theme";
import { api } from "../lib/api";
import { useToast } from "../lib/toast.jsx";
import { useThresholds } from "../lib/thresholds.jsx";

export default function Settings() {
  const toast = useToast();
  const TH = useThresholds();
  const [pathInfo, setPathInfo] = useState(null);
  const [stats, setStats] = useState(null);
  const [pathDraft, setPathDraft] = useState("");
  const [validating, setValidating] = useState(false);
  const [valid, setValid] = useState(null);
  const [loading, setLoading] = useState(false);

  const refreshPath = async () => {
    try {
      const p = await api.path.current();
      setPathInfo(p);
      setPathDraft((p?.current_path || "").replace(/^~\//, ""));
    } catch (e) {
      toast.error("Path lookup failed: " + e.message);
    }
  };

  useEffect(() => {
    refreshPath();
    api.stats().then(setStats).catch(() => {});
  }, []);

  // Validate the path draft on every change (debounced).
  useEffect(() => {
    if (!pathDraft) { setValid(null); return; }
    setValidating(true);
    const t = setTimeout(async () => {
      try {
        const r = await api.path.validate(pathDraft);
        setValid(!!r.valid);
      } catch {
        setValid(false);
      } finally {
        setValidating(false);
      }
    }, 400);
    return () => clearTimeout(t);
  }, [pathDraft]);

  const onLoad = async () => {
    if (valid !== true) return;
    setLoading(true);
    try {
      const r = await api.path.load(pathDraft);
      if (!r.success) throw new Error(r.error || r.message || "load failed");
      toast.success(`Loaded ${r.stats?.total_messages?.toLocaleString() || "?"} messages`);
      await refreshPath();
      const s = await api.stats();
      setStats(s);
    } catch (e) {
      toast.error("Load failed: " + e.message);
    } finally {
      setLoading(false);
    }
  };

  return (
    <div style={{ padding: "18px 18px 24px", color: COLORS.text, maxWidth: 980 }}>
      <div style={{ display: "flex", alignItems: "baseline", gap: 14, marginBottom: 18 }}>
        <h2 style={{ margin: 0, fontSize: 20, fontWeight: 500 }}>Settings</h2>
        <span style={{ color: COLORS.textFaint, fontSize: 13 }}>capture source · thresholds · about</span>
      </div>

      {/* Capture source — editable */}
      <Card title="CAPTURE SOURCE">
        <KV k="Source kind" v="JSON file (refresh on demand)" />
        <KV k="Resolved file" v={pathInfo?.full_file_path || "—"} mono />
        <KV k="Source"        v={(() => {
          const s = pathInfo?.source;
          if (s === "env")        return "$XFAPI_HOME";
          if (s === "autodetect") return "auto-detected from server location";
          if (s === "manual")     return "manual override";
          if (s === "fallback")   return "fallback (~/XFAPI — neither env nor autodetect resolved)";
          return s || "—";
        })()} />
        <div style={{ marginTop: 14 }}>
          <div style={{ fontSize: 11, fontWeight: 600, letterSpacing: "0.08em", color: COLORS.textFaint, marginBottom: 6 }}>XFAPI DIRECTORY</div>
          <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
            <span style={{ color: COLORS.textMuted, fontFamily: MONO, fontSize: 14 }}>~/</span>
            <input
              value={pathDraft}
              onChange={(e) => setPathDraft(e.target.value)}
              placeholder="path/to/XFAPI"
              style={inputStyle}
              onKeyDown={(e) => { if (e.key === "Enter" && valid) onLoad(); }}
            />
            <span style={{ color: COLORS.textMuted, fontFamily: MONO, fontSize: 14, whiteSpace: "nowrap" }}>/generated_logs/message_stats.json</span>
            <span style={{ width: 18, textAlign: "center" }}>
              {validating && "⏳"}
              {!validating && valid === true  && <span style={{ color: COLORS.green }}>●</span>}
              {!validating && valid === false && <span style={{ color: COLORS.red }}>●</span>}
            </span>
            <button onClick={onLoad} disabled={valid !== true || loading}
              style={{
                padding: "7px 14px", borderRadius: 4,
                background: valid === true ? "oklch(from var(--accent) l c h / 0.18)" : COLORS.surface,
                border: `1px solid ${valid === true ? ACCENT : COLORS.border}`,
                color: valid === true ? COLORS.text : COLORS.textMuted,
                cursor: valid === true && !loading ? "pointer" : "not-allowed",
                fontSize: 13, fontWeight: 500,
              }}>{loading ? "Loading…" : "Load this path"}</button>
          </div>
          <p style={{ fontSize: 12, color: COLORS.textFaint, marginTop: 8 }}>
            Set <code style={{ color: COLORS.textMuted }}>$XFAPI_HOME</code> in the server's environment to change this default at startup.
          </p>
        </div>
      </Card>

      {/* SLA thresholds — read-only but live values */}
      <Card title="SLA THRESHOLDS">
        <KV k="SLA (green ↔ amber)" v={`${TH.sla_us} µs`} />
        <KV k="Outlier (amber ↔ red)" v={`${TH.outlier_us} µs`} />
        <KV k="Hard outlier (red dot)" v={`${TH.hard_outlier_us} µs`} />
        <KV k="Histogram bins" v={(TH.histogram_bins_us || []).join(" · ") + " µs"} mono />
        <p style={{ fontSize: 12, color: COLORS.textFaint, marginTop: 10, lineHeight: 1.5 }}>
          Edit <code style={{ color: COLORS.textMuted }}>THRESHOLDS</code> in
          {" "}<code style={{ color: COLORS.textMuted }}>server/main.py</code> and restart to change these.
          Stats and chart tints will recompute on next request.
        </p>
      </Card>

      {/* Vault */}
      <Card title="VAULT">
        <KV k="Storage path" v="src/dashboard/server/stats_vault/" mono />
        <KV k="Format" v="xfapi-vault-v1 envelope (with metadata)" />
        <KV k="Tags" v="live · baseline · flagged · archived" />
      </Card>

      {/* Display */}
      <Card title="DISPLAY">
        <KV k="Theme" v="dark · cyan accent · comfortable density" />
        <KV k="Mono font" v="Geist Mono" />
        <KV k="Latency unit" v="µs" />
      </Card>

      {/* Current capture summary */}
      {stats && (
        <Card title="CURRENT CAPTURE">
          <KV k="Total messages" v={(stats.total_messages || 0).toLocaleString()} />
          <KV k="Message types"  v={(stats.message_types || []).length} />
          <KV k="Duration"        v={stats.duration_ms != null ? `${stats.duration_ms} ms` : "—"} />
          <KV k="Errors"          v={(stats.error_count ?? 0).toLocaleString()} />
          <KV k="Data source"     v={stats.data_source || "—"} />
          {stats.vault_session_name && <KV k="Vault session" v={stats.vault_session_name} />}
        </Card>
      )}

      {/* About */}
      <Card title="ABOUT">
        <KV k="App"         v="XFAPI Dashboard" />
        <KV k="Build"       v="React + Vite (5.4) bundle served by FastAPI" />
        <KV k="Endpoints"   v="GET /api/{stats,thresholds,messages,timeseries,sequence,diff,vault/list,…}" mono />
        <KV k="Keyboard"    v="⌘1 Dashboard · ⌘2 Sequence · ⌘3 Diff · ⌘4 Vault · ⌘, Settings" />
        <KV k="Server env"  v="XFAPI_HOME · XFAPI_CORS_ORIGINS" mono />
      </Card>
    </div>
  );
}

function Card({ title, children }) {
  return (
    <div style={{
      background: COLORS.bgDeep, border: `1px solid ${COLORS.border}`,
      borderRadius: 6, padding: "14px 16px", marginBottom: 12,
    }}>
      <div style={{ fontSize: 11, fontWeight: 600, letterSpacing: "0.08em", color: COLORS.textFaint, marginBottom: 10 }}>{title}</div>
      {children}
    </div>
  );
}
function KV({ k, v, mono }) {
  return (
    <div style={{ display: "grid", gridTemplateColumns: "200px 1fr", gap: 10, padding: "5px 0", borderBottom: `1px solid ${COLORS.surface}` }}>
      <div style={{ color: COLORS.textMuted, fontSize: 13 }}>{k}</div>
      <div style={{ color: COLORS.text, fontSize: 13, fontFamily: mono ? MONO : "inherit", wordBreak: "break-all" }}>{v}</div>
    </div>
  );
}
const inputStyle = {
  flex: 1, minWidth: 200,
  padding: "7px 10px",
  background: COLORS.surface, color: COLORS.text,
  border: `1px solid ${COLORS.border}`, borderRadius: 4,
  fontFamily: MONO, fontSize: 14, outline: "none",
};
