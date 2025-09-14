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

// SFN/Slot heatmap. cells = [{sfn, slot, density, norm}]
export default function SfnSlotHeatmap({
  cells = [],
  sfnMin,
  sfnMax,
  sfnStep = 1,
  slotCount = 20,
  width = 700,
  height = 200,
  accent = COLORS.cyan,
}) {
  if (!cells.length) return <div style={{ color: COLORS.textFaint, fontSize: 12 }}>no data</div>;
  const padL = 36, padB = 22, padT = 8, padR = 6;
  const w = width - padL - padR;
  const h = height - padT - padB;
  const sfnSpan = Math.max(1, Math.ceil((sfnMax - sfnMin) / sfnStep) + 1);
  const cw = w / sfnSpan;
  const ch = h / slotCount;

  const lookup = new Map();
  for (const c of cells) lookup.set(`${c.sfn}|${c.slot}`, c);

  const yLabels = [0, 5, 10, 15, 19].filter((i) => i < slotCount);
  const xTicks = 5;

  return (
    <svg width={width} height={height} style={{ display: "block" }}>
      {Array.from({ length: sfnSpan }, (_, ix) => {
        const sfn = sfnMin + ix * sfnStep;
        return Array.from({ length: slotCount }, (_, sl) => {
          const cell = lookup.get(`${sfn}|${sl}`);
          const norm = cell ? cell.norm : 0;
          return (
            <rect
              key={`${sfn}-${sl}`}
              x={padL + ix * cw}
              y={padT + sl * ch}
              width={Math.max(1, cw - 0.5)}
              height={Math.max(1, ch - 0.5)}
              fill={accent}
              fillOpacity={norm > 0 ? 0.12 + norm * 0.78 : 0.04}
            />
          );
        });
      })}
      {yLabels.map((sl) => (
        <text key={sl} x={padL - 6} y={padT + sl * ch + ch / 2 + 3}
              textAnchor="end" fontSize={9} fill={COLORS.textFaint}
              fontFamily='"Geist Mono", monospace'>S{sl}</text>
      ))}
      {Array.from({ length: xTicks + 1 }, (_, k) => {
        const ix = Math.round((sfnSpan - 1) * (k / xTicks));
        const sfn = sfnMin + ix * sfnStep;
        return (
          <text key={k}
                x={padL + ix * cw + cw / 2}
                y={height - 6}
                textAnchor="middle" fontSize={9} fill={COLORS.textFaint}
                fontFamily='"Geist Mono", monospace'>{sfn}</text>
        );
      })}
      <text x={padL} y={padT - 1} fontSize={9} fill={COLORS.textFaint}
            fontFamily='"Geist Mono", monospace' opacity={0.7}>SLOT × SFN</text>
    </svg>
  );
}
