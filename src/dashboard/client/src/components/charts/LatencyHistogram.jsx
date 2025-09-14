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
import { COLORS } from "../../lib/theme";

// Log-binned histogram of DL latencies (µs). Counts are pre-binned by the
// server (see /api/timeseries.histogram); the component just renders bars.
//   bins  = [b0, b1, …, bn]   (n+1 boundaries)
//   counts = [c0, c1, …, c_{n-1}]   bar i covers [bi, b_{i+1})
// Color thresholds:
//   green for buckets ending ≤ slaUs
//   amber for buckets ending ≤ outlierUs
//   red   otherwise
export default function LatencyHistogram({
  bins,
  counts,
  width = 540,
  height = 200,
  slaUs = 50,
  outlierUs = 100,
}) {
  if (!bins || !counts || counts.length === 0)
    return <div style={{ color: COLORS.textFaint, fontSize: 12 }}>no data</div>;

  const max = Math.max(1, ...counts);
  const padL = 32, padB = 22, padT = 8, padR = 8;
  const w = width - padL - padR, h = height - padT - padB;
  const bw = w / counts.length;
  const colorFor = (top) => top <= slaUs ? COLORS.green : top <= outlierUs ? COLORS.amber : COLORS.red;
  return (
    <svg width={width} height={height} style={{ display: "block" }}>
      <line x1={padL} x2={width - padR} y1={padT + h} y2={padT + h} stroke={COLORS.border} />
      {counts.map((c, i) => {
        const bh = (h * c) / max;
        const lo = bins[i], hi = bins[i + 1];
        return (
          <g key={i}>
            <rect x={padL + i * bw + 1} y={padT + h - bh}
                  width={Math.max(1, bw - 2)} height={Math.max(0, bh)}
                  fill={colorFor(hi)} fillOpacity={c > 0 ? 0.85 : 0.18} />
            <text x={padL + i * bw + bw / 2} y={height - 6}
                  textAnchor="middle" fontSize={10} fill={COLORS.textFaint}
                  fontFamily='"Geist Mono", monospace'>{lo}</text>
          </g>
        );
      })}
      <text x={padL - 4} y={padT + 8} textAnchor="end" fontSize={10} fill={COLORS.textFaint}
            fontFamily='"Geist Mono", monospace'>{max.toLocaleString()}</text>
    </svg>
  );
}
