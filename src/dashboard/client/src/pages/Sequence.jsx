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

import React, { useEffect, useState, useMemo } from "react";
import { COLORS, MONO, ACCENT, groupColor, inferGroup, fmtMicro } from "../lib/theme";
import { api } from "../lib/api";
import { useToast } from "../lib/toast.jsx";

export default function Sequence() {
  const toast = useToast();
  const [events, setEvents] = useState([]);
  const [pagination, setPagination] = useState({ offset: 0, limit: 60, total_count: 0, has_next: false, has_prev: false });
  const [offset, setOffset] = useState(0);
  const [limit, setLimit] = useState(60);
  const [slaUs, setSlaUs] = useState(100);
  const [hover, setHover] = useState(null);

  useEffect(() => {
    api.sequence({ offset, limit, sla_us: slaUs })
      .then((r) => { setEvents(r.events); setPagination(r.pagination); })
      .catch((e) => toast.error("Sequence load failed: " + e.message));
  }, [offset, limit, slaUs, toast]);

  const ROW = 30;
  const PAD_TOP = 40;
  const PAD_LEFT = 110;     // SFN gutter
  const LIFE_L1 = 220;
  const LIFE_L2 = 720;
  const SVG_W = 880;
  const SVG_H = PAD_TOP + 30 + events.length * ROW;

  const lastSfnSlot = useMemo(() => {
    const out = [];
    let prevSfn = null, prevSlot = null;
    for (const e of events) {
      if (e.sfn !== prevSfn || e.slot !== prevSlot) {
        out.push({ id: e.id, sfn: e.sfn, slot: e.slot, show: true });
        prevSfn = e.sfn; prevSlot = e.slot;
      } else {
        out.push({ id: e.id, sfn: e.sfn, slot: e.slot, show: false });
      }
    }
    return out;
  }, [events]);

  return (
    <div style={{ padding: "18px 18px 24px", color: COLORS.text }}>
      <div style={{ display: "flex", alignItems: "baseline", gap: 16, marginBottom: 12 }}>
        <h2 style={{ margin: 0, fontSize: 18, fontWeight: 500 }}>Sequence</h2>
        <span style={{ color: COLORS.textFaint, fontSize: 12 }}>L1 ↔ L2 lifelines, time flows down</span>
        <div style={{ flex: 1 }} />
        <label style={{ fontSize: 11, color: COLORS.textMuted, fontFamily: MONO }}>
          SLA µs:
          <input type="number" value={slaUs} onChange={(e) => setSlaUs(parseFloat(e.target.value) || 100)}
            style={{ marginLeft: 6, width: 70, padding: "3px 6px",
                     background: COLORS.surface, color: COLORS.text,
                     border: `1px solid ${COLORS.border}`, borderRadius: 3,
                     fontFamily: MONO, fontSize: 11, outline: "none" }} />
        </label>
        <label style={{ fontSize: 11, color: COLORS.textMuted, fontFamily: MONO }}>
          Window:
          <select value={limit} onChange={(e) => { setLimit(parseInt(e.target.value)); setOffset(0); }}
            style={{ marginLeft: 6, padding: "3px 6px",
                     background: COLORS.surface, color: COLORS.text,
                     border: `1px solid ${COLORS.border}`, borderRadius: 3, fontSize: 11 }}>
            {[20, 40, 60, 100, 200].map((n) => <option key={n} value={n}>{n}</option>)}
          </select>
        </label>
        <button disabled={!pagination.has_prev} onClick={() => setOffset(Math.max(0, offset - limit))} style={navBtn}>‹ Older</button>
        <span style={{ fontSize: 11, color: COLORS.textMuted, fontFamily: MONO }}>
          {pagination.offset + 1}–{Math.min(pagination.offset + events.length, pagination.total_count)} / {pagination.total_count.toLocaleString()}
        </span>
        <button disabled={!pagination.has_next} onClick={() => setOffset(offset + limit)} style={navBtn}>Newer ›</button>
      </div>

      <div style={{
        background: COLORS.bgDeep,
        border: `1px solid ${COLORS.border}`, borderRadius: 6,
        padding: 12, overflow: "auto",
      }}>
        <svg width={SVG_W} height={SVG_H} style={{ display: "block" }}>
          <text x={LIFE_L1} y={20} textAnchor="middle" fontSize={12} fontWeight={600} fill={COLORS.text} fontFamily={MONO}>L1 (PHY)</text>
          <text x={LIFE_L2} y={20} textAnchor="middle" fontSize={12} fontWeight={600} fill={COLORS.text} fontFamily={MONO}>L2 (MAC)</text>
          <line x1={LIFE_L1} x2={LIFE_L1} y1={28} y2={SVG_H - 10} stroke={COLORS.border} />
          <line x1={LIFE_L2} x2={LIFE_L2} y1={28} y2={SVG_H - 10} stroke={COLORS.border} />

          {events.map((e, i) => {
            const y = PAD_TOP + i * ROW;
            const c = groupColor(inferGroup(e.type));
            const fromX = e.from === "L1" ? LIFE_L1 : LIFE_L2;
            const toX   = e.to   === "L1" ? LIFE_L1 : LIFE_L2;
            const isHover = hover === e.id;
            const opacity = hover == null ? 1 : (isHover ? 1 : 0.25);
            const strokeColor = e.anomaly ? COLORS.red : (isHover ? ACCENT : c);
            const dash = e.anomaly ? "4 3" : "0";
            return (
              <g key={e.id} opacity={opacity}
                onMouseEnter={() => setHover(e.id)}
                onMouseLeave={() => setHover(null)}>
                {lastSfnSlot[i].show && (
                  <text x={PAD_LEFT - 6} y={y + 4} textAnchor="end" fontSize={10}
                        fontFamily={MONO} fill={COLORS.textFaint}>
                    SFN {e.sfn}.{e.slot}
                  </text>
                )}
                <line x1={fromX} x2={toX} y1={y} y2={y}
                      stroke={strokeColor} strokeWidth={isHover ? 2 : 1.4}
                      strokeDasharray={dash} />
                {/* arrowhead */}
                <polygon points={`${toX},${y} ${toX + (e.from === "L1" ? -7 : 7)},${y - 4} ${toX + (e.from === "L1" ? -7 : 7)},${y + 4}`}
                         fill={strokeColor} />
                <text x={(fromX + toX) / 2} y={y - 4} textAnchor="middle"
                      fontSize={10.5} fontFamily={MONO}
                      fill={isHover ? COLORS.text : COLORS.textMuted}>
                  {e.type}
                </text>
                {e.latency_us != null && (
                  <text x={(fromX + toX) / 2} y={y + 12} textAnchor="middle"
                        fontSize={9.5} fontFamily={MONO}
                        fill={e.anomaly ? COLORS.red : (e.latency_us > 50 ? COLORS.amber : COLORS.green)}>
                    {fmtMicro(e.latency_us)} µs
                  </text>
                )}
              </g>
            );
          })}
        </svg>
      </div>
    </div>
  );
}

const navBtn = {
  padding: "5px 10px", borderRadius: 4,
  background: COLORS.surface,
  border: `1px solid ${COLORS.border}`,
  color: COLORS.textMuted,
  fontSize: 11, cursor: "pointer",
};
