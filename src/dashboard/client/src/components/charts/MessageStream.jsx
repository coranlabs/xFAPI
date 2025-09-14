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

import React from "react";
import { COLORS, groupColor } from "../../lib/theme";

// Stacked area chart showing per-type rate over time.
// types = [{type, count, color_group}]
// buckets = [[v0,v1,...], ...] same length as types
export default function MessageStream({
  types = [],
  buckets = [],
  width = 700,
  height = 180,
}) {
  if (!types.length || !buckets.length || !buckets[0].length)
    return <div style={{ color: COLORS.textFaint, fontSize: 12 }}>no data</div>;

  const n = buckets[0].length;
  const padX = 8, padTop = 8, padBot = 22, padR = 8;
  const w = width - padX - padR;
  const h = height - padTop - padBot;

  const stacks = Array.from({ length: n }, (_, i) =>
    types.reduce((s, _t, ti) => s + (buckets[ti][i] || 0), 0)
  );
  const yMax = Math.max(1, ...stacks);
  const X = (i) => padX + (w * i) / Math.max(1, n - 1);
  const Y = (v) => padTop + h - (h * v) / yMax;

  const layers = [];
  let stack = new Array(n).fill(0);
  for (let ti = 0; ti < types.length; ti++) {
    const top = stack.slice();
    const next = stack.map((s, i) => s + (buckets[ti][i] || 0));
    let path = "";
    for (let i = 0; i < n; i++) path += `${i === 0 ? "M" : "L"} ${X(i).toFixed(1)} ${Y(next[i]).toFixed(1)} `;
    for (let i = n - 1; i >= 0; i--) path += `L ${X(i).toFixed(1)} ${Y(top[i]).toFixed(1)} `;
    path += "Z";
    layers.push({ path, color: groupColor(types[ti].color_group), type: types[ti].type, count: types[ti].count });
    stack = next;
  }

  return (
    <div style={{ display: "flex", flexDirection: "column", gap: 6 }}>
      <svg width={width} height={height} style={{ display: "block" }}>
        {layers.map((L, k) => (
          <path key={k} d={L.path} fill={L.color} fillOpacity={0.55} stroke={L.color} strokeOpacity={0.9} strokeWidth={0.6} />
        ))}
        <line x1={padX} x2={width - padR} y1={padTop + h} y2={padTop + h} stroke={COLORS.border} />
      </svg>
      <div style={{ display: "flex", flexWrap: "wrap", gap: 8, paddingLeft: 4 }}>
        {layers.slice().reverse().map((L) => (
          <div key={L.type} title={`${L.type}: ${L.count.toLocaleString()}`} style={{
            display: "flex", alignItems: "center", gap: 6, fontSize: 10.5,
            color: COLORS.textMuted, maxWidth: 240,
          }}>
            <span style={{ width: 8, height: 8, background: L.color, borderRadius: 2, flexShrink: 0 }} />
            <span style={{ overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }}>
              {L.type}
            </span>
          </div>
        ))}
      </div>
    </div>
  );
}
