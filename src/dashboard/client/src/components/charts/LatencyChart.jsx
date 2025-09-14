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

// Log-scale latency time-series with gradient area, grid lines, SLA threshold,
// and outlier dots. Ported from xfapi_dashboard_reference/charts.jsx.
export default function LatencyChart({
  data = [],
  width = 700,
  height = 180,
  accent = COLORS.cyan,
  unit = "us",
  outlierThreshold = 100,
  hardOutlier = 500,
  slaThreshold = 50,
}) {
  if (!data.length) return <div style={{ color: COLORS.textFaint, fontSize: 12 }}>no data</div>;
  const padX = 28, padTop = 10, padBot = 18;
  const w = width - padX, h = height - padTop - padBot;

  const log = (v) => Math.log10(Math.max(0.5, v));
  const yMax = log(Math.max(2000, Math.max(...data)));
  const yMin = log(0.5);
  const Y = (v) => padTop + h - (h * (log(v) - yMin)) / (yMax - yMin);
  const X = (i) => padX + (w * i) / Math.max(1, data.length - 1);

  const dPath = data.map((v, i) => `${i === 0 ? "M" : "L"} ${X(i).toFixed(1)} ${Y(v).toFixed(1)}`).join(" ");
  const aPath = `${dPath} L ${X(data.length - 1).toFixed(1)} ${padTop + h} L ${X(0).toFixed(1)} ${padTop + h} Z`;

  const grids = [1, 10, 100, 1000];
  const outliers = data.map((v, i) => ({ v, i })).filter((p) => p.v > outlierThreshold);

  return (
    <svg width={width} height={height} style={{ display: "block" }}>
      <defs>
        <linearGradient id="lat-grad" x1="0" x2="0" y1="0" y2="1">
          <stop offset="0%" stopColor={accent} stopOpacity={0.35} />
          <stop offset="100%" stopColor={accent} stopOpacity={0} />
        </linearGradient>
      </defs>
      {grids.map((g) =>
        g <= Math.pow(10, yMax) ? (
          <g key={g}>
            <line x1={padX} x2={width} y1={Y(g)} y2={Y(g)} stroke={COLORS.border} strokeDasharray="2 4" strokeOpacity={0.6} />
            <text x={4} y={Y(g) + 3} fontSize={9} fill={COLORS.textFaint}
                  fontFamily='"Geist Mono", monospace'>{g}{unit === "ms" ? "" : ""}</text>
          </g>
        ) : null
      )}
      <line x1={padX} x2={width} y1={Y(slaThreshold)} y2={Y(slaThreshold)}
            stroke={COLORS.amber} strokeDasharray="3 3" strokeOpacity={0.7} />
      <text x={width - 6} y={Y(slaThreshold) - 2} textAnchor="end" fontSize={9}
            fill={COLORS.amber} fontFamily='"Geist Mono", monospace'>SLA {slaThreshold}{unit === "ms" ? "" : "µs"}</text>
      <path d={aPath} fill="url(#lat-grad)" />
      <path d={dPath} fill="none" stroke={accent} strokeWidth={1.4} />
      {outliers.map((p, k) => (
        <circle key={k} cx={X(p.i)} cy={Y(p.v)} r={2.6}
                fill={p.v > hardOutlier ? COLORS.red : COLORS.amber} />
      ))}
    </svg>
  );
}
