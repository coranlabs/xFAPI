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

export default function Sparkline({ data = [], width = 240, height = 36, color = COLORS.cyan, fill = true }) {
  if (!data.length) return null;
  const max = Math.max(1, ...data);
  const X = (i) => (width * i) / Math.max(1, data.length - 1);
  const Y = (v) => height - (height * v) / max;
  const dPath = data.map((v, i) => `${i === 0 ? "M" : "L"} ${X(i).toFixed(1)} ${Y(v).toFixed(1)}`).join(" ");
  const aPath = `${dPath} L ${width} ${height} L 0 ${height} Z`;
  return (
    <svg width={width} height={height} style={{ display: "block" }}>
      {fill && <path d={aPath} fill={color} fillOpacity={0.18} />}
      <path d={dPath} fill="none" stroke={color} strokeWidth={1.2} />
    </svg>
  );
}
