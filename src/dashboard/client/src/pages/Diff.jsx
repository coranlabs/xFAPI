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

import React, { useEffect, useMemo, useState } from "react";
import { COLORS, MONO, ACCENT, groupColor, inferGroup, fmtNum, fmtMicro, fmtDuration, fmtDate } from "../lib/theme";
import { api } from "../lib/api";
import { useToast } from "../lib/toast.jsx";

export default function Diff() {
  const toast = useToast();
  const [vaultFiles, setVaultFiles] = useState([]);
  const [a, setA] = useState("");
  const [b, setB] = useState("");
  const [diff, setDiff] = useState(null);
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    api.vault.list()
      .then((r) => setVaultFiles(r.files || []))
      .catch((e) => toast.error("Vault list failed: " + e.message));
  }, [toast]);

  const runDiff = async () => {
    if (!a || !b) return;
    setLoading(true);
    try {
      const r = await api.diff(a, b, 180);
      setDiff(r);
    } catch (e) {
      toast.error("Diff failed: " + e.message);
    } finally {
      setLoading(false);
    }
  };

  return (
    <div style={{ padding: "18px 18px 24px", color: COLORS.text }}>
      <div style={{ display: "flex", alignItems: "baseline", gap: 14, marginBottom: 14 }}>
        <h2 style={{ margin: 0, fontSize: 18, fontWeight: 500 }}>Diff</h2>
        <span style={{ color: COLORS.textFaint, fontSize: 12 }}>compare two vault captures</span>
      </div>

      <div style={{ display: "grid", gridTemplateColumns: "1fr 60px 1fr 110px", gap: 10, alignItems: "center", marginBottom: 14 }}>
        <CapturePicker label="A (baseline)" value={a} setValue={setA} files={vaultFiles} />
        <button onClick={() => { const t = a; setA(b); setB(t); }} style={{
          padding: "10px", borderRadius: 6,
          background: COLORS.surface, border: `1px solid ${COLORS.border}`,
          color: COLORS.textMuted, cursor: "pointer", fontSize: 14,
        }} title="Swap A / B">⇄</button>
        <CapturePicker label="B (current)" value={b} setValue={setB} files={vaultFiles} />
        <button onClick={runDiff} disabled={!a || !b || loading} style={{
          padding: "10px 16px", borderRadius: 6,
          background: !a || !b ? COLORS.surface : "oklch(from var(--accent) l c h / 0.18)",
          border: `1px solid ${!a || !b ? COLORS.border : ACCENT}`,
          color: !a || !b ? COLORS.textMuted : COLORS.text,
          cursor: !a || !b ? "not-allowed" : "pointer",
          fontSize: 12, fontWeight: 500,
        }}>{loading ? "Computing…" : "Compute Diff"}</button>
      </div>

      {!diff && !loading && (
        <div style={{ color: COLORS.textFaint, fontSize: 13, padding: 24, textAlign: "center", border: `1px dashed ${COLORS.border}`, borderRadius: 6 }}>
          Pick two vault captures and press Compute Diff.
        </div>
      )}

      {diff && <DiffResults diff={diff} />}
    </div>
  );
}

function CapturePicker({ label, value, setValue, files }) {
  const f = files.find((x) => x.filename === value);
  return (
    <div style={{ background: COLORS.bgDeep, border: `1px solid ${COLORS.border}`, borderRadius: 6, padding: 10 }}>
      <div style={{ fontSize: 9.5, fontWeight: 600, letterSpacing: "0.08em", color: COLORS.textFaint, marginBottom: 6 }}>{label}</div>
      <select value={value} onChange={(e) => setValue(e.target.value)} style={{
        width: "100%", padding: "7px 10px",
        background: COLORS.surface, color: COLORS.text,
        border: `1px solid ${COLORS.border}`, borderRadius: 4,
        fontFamily: MONO, fontSize: 12, outline: "none",
      }}>
        <option value="">— select —</option>
        {files.map((f) => (
          <option key={f.filename} value={f.filename}>{f.name} ({fmtNum(f.message_count)})</option>
        ))}
      </select>
      {f && (
        <div style={{ display: "flex", gap: 12, fontSize: 11, color: COLORS.textMuted, marginTop: 6, fontFamily: MONO }}>
          <span><span style={{ color: COLORS.textFaint }}>tag</span> {f.tag}</span>
          <span><span style={{ color: COLORS.textFaint }}>captured</span> {fmtDate(f.captured_date)}</span>
          {f.build_version && <span><span style={{ color: COLORS.textFaint }}>build</span> {f.build_version}</span>}
        </div>
      )}
    </div>
  );
}

function DeltaTile({ label, a, b, delta, fmt = fmtNum, badIfPositive = true, suffix = "" }) {
  const sign = delta > 0 ? "+" : "";
  const isBad = (delta > 0 && badIfPositive) || (delta < 0 && !badIfPositive);
  const isGood = delta !== 0 && !isBad;
  const color = delta === 0 ? COLORS.textMuted : isBad ? COLORS.red : COLORS.green;
  return (
    <div style={{ background: COLORS.bgDeep, border: `1px solid ${COLORS.border}`, borderRadius: 6, padding: "10px 12px" }}>
      <div style={{ fontSize: 9.5, fontWeight: 600, letterSpacing: "0.08em", color: COLORS.textFaint }}>{label}</div>
      <div style={{ display: "flex", alignItems: "baseline", gap: 6, marginTop: 4, fontFamily: MONO }}>
        <span style={{ fontSize: 13, color: COLORS.textMuted }}>{fmt(a)}{suffix}</span>
        <span style={{ color: COLORS.textFaint }}>→</span>
        <span style={{ fontSize: 18, fontWeight: 500, color: COLORS.text }}>{fmt(b)}{suffix}</span>
      </div>
      <div style={{ fontFamily: MONO, fontSize: 11, color, marginTop: 2 }}>
        {delta === 0 ? "no change" : `${sign}${fmt(delta)}${suffix}`}
      </div>
    </div>
  );
}

function DiffResults({ diff }) {
  const buckets = diff.buckets || diff.a.trace.length;
  const W = 760, H = 200, padX = 36, padTop = 10, padBot = 24;
  const w = W - padX, h = H - padTop - padBot;
  const yMax = Math.max(2000, ...diff.a.trace, ...diff.b.trace);
  const log = (v) => Math.log10(Math.max(0.5, v));
  const lMax = log(yMax), lMin = log(0.5);
  const Y = (v) => padTop + h - (h * (log(v) - lMin)) / (lMax - lMin);
  const X = (i) => padX + (w * i) / Math.max(1, buckets - 1);
  const path = (arr) => arr.map((v, i) => `${i === 0 ? "M" : "L"} ${X(i).toFixed(1)} ${Y(v).toFixed(1)}`).join(" ");
  const grids = [1, 10, 100, 1000];

  const maxAbsDelta = Math.max(1, ...diff.type_deltas.map((d) => Math.abs(d.delta)));

  return (
    <>
      {/* Delta KPIs */}
      <div style={{ display: "grid", gridTemplateColumns: "repeat(5, 1fr)", gap: 8, marginBottom: 14 }}>
        <DeltaTile label="MESSAGES" a={diff.deltas.total.a} b={diff.deltas.total.b} delta={diff.deltas.total.delta} badIfPositive={false} />
        <DeltaTile label="ERRORS" a={diff.deltas.errors.a} b={diff.deltas.errors.b} delta={diff.deltas.errors.delta} badIfPositive={true} />
        <DeltaTile label="DURATION" a={diff.deltas.duration_ms.a} b={diff.deltas.duration_ms.b} delta={diff.deltas.duration_ms.delta} fmt={fmtDuration} badIfPositive={false} />
        <DeltaTile label="P99 (µs)" a={diff.deltas.p99.a} b={diff.deltas.p99.b} delta={diff.deltas.p99.delta} fmt={(v) => fmtMicro(v)} badIfPositive={true} suffix="" />
        <DeltaTile label="OUTLIERS" a={diff.deltas.outliers.a} b={diff.deltas.outliers.b} delta={diff.deltas.outliers.delta} badIfPositive={true} />
      </div>

      {/* Latency overlay */}
      <div style={{ background: COLORS.bgDeep, border: `1px solid ${COLORS.border}`, borderRadius: 6, padding: 12, marginBottom: 14 }}>
        <div style={{ fontSize: 9.5, fontWeight: 600, letterSpacing: "0.08em", color: COLORS.textFaint, marginBottom: 6 }}>LATENCY OVERLAY (LOG µs)</div>
        <svg width={W} height={H}>
          {grids.map((g) => (
            <g key={g}>
              <line x1={padX} x2={W} y1={Y(g)} y2={Y(g)} stroke={COLORS.border} strokeDasharray="2 4" strokeOpacity={0.5} />
              <text x={4} y={Y(g) + 3} fontSize={9} fill={COLORS.textFaint} fontFamily={MONO}>{g}</text>
            </g>
          ))}
          <path d={path(diff.a.trace)} fill="none" stroke={COLORS.blue} strokeWidth={1.4} />
          <path d={path(diff.b.trace)} fill="none" stroke={COLORS.red} strokeWidth={1.4} />
        </svg>
        <div style={{ display: "flex", gap: 18, fontSize: 11, color: COLORS.textMuted, marginTop: 4, fontFamily: MONO }}>
          <span><span style={{ width: 10, height: 2, display: "inline-block", background: COLORS.blue, marginRight: 6 }} /> A: {diff.a.filename}</span>
          <span><span style={{ width: 10, height: 2, display: "inline-block", background: COLORS.red, marginRight: 6 }} /> B: {diff.b.filename}</span>
        </div>
      </div>

      {/* Per-type deltas */}
      <div style={{ background: COLORS.bgDeep, border: `1px solid ${COLORS.border}`, borderRadius: 6, padding: 12 }}>
        <div style={{ fontSize: 9.5, fontWeight: 600, letterSpacing: "0.08em", color: COLORS.textFaint, marginBottom: 6 }}>MESSAGE TYPE DELTAS (sorted by |Δ|)</div>
        <table style={{ width: "100%", borderCollapse: "collapse", fontFamily: MONO, fontSize: 11.5 }}>
          <thead>
            <tr>
              {["TYPE", "A", "B", "Δ", "Δ%", ""].map((h, i) => (
                <th key={i} style={{ textAlign: i < 1 ? "left" : "right", padding: "6px 10px",
                  borderBottom: `1px solid ${COLORS.border}`, fontSize: 9, fontWeight: 600,
                  letterSpacing: "0.08em", color: COLORS.textFaint }}>{h}</th>
              ))}
            </tr>
          </thead>
          <tbody>
            {diff.type_deltas.map((d) => {
              const c = groupColor(d.color_group || inferGroup(d.type));
              const w = (Math.abs(d.delta) / maxAbsDelta) * 100;
              return (
                <tr key={d.type} style={{ borderBottom: `1px solid ${COLORS.surface}` }}>
                  <td style={{ padding: "5px 10px", color: COLORS.text, whiteSpace: "nowrap" }}>
                    <span style={{ display: "inline-block", width: 6, height: 6, background: c, borderRadius: 1, marginRight: 6, verticalAlign: "middle" }} />
                    {d.type}
                  </td>
                  <td style={tdR}>{fmtNum(d.a)}</td>
                  <td style={tdR}>{fmtNum(d.b)}</td>
                  <td style={{ ...tdR, color: d.delta === 0 ? COLORS.textMuted : (d.delta > 0 ? COLORS.green : COLORS.red) }}>
                    {d.delta > 0 ? "+" : ""}{fmtNum(d.delta)}
                  </td>
                  <td style={{ ...tdR, color: d.delta === 0 ? COLORS.textMuted : (d.delta > 0 ? COLORS.green : COLORS.red) }}>
                    {d.delta_pct === "inf" ? "∞" : d.delta_pct == null ? "—" : `${d.delta_pct > 0 ? "+" : ""}${d.delta_pct}%`}
                  </td>
                  <td style={{ padding: "5px 10px", width: 140 }}>
                    <div style={{ position: "relative", width: 120, height: 6, background: COLORS.surface, borderRadius: 2, overflow: "hidden" }}>
                      <div style={{
                        position: "absolute", top: 0, bottom: 0,
                        ...(d.delta >= 0
                          ? { left: "50%", width: `${w / 2}%`, background: COLORS.green }
                          : { right: "50%", width: `${w / 2}%`, background: COLORS.red }),
                      }} />
                      <div style={{ position: "absolute", top: 0, bottom: 0, left: "50%", width: 1, background: COLORS.borderHi }} />
                    </div>
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
    </>
  );
}

const tdR = { padding: "5px 10px", color: COLORS.text, textAlign: "right" };
