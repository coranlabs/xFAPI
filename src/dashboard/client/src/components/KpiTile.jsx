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
import { COLORS, MONO } from "../lib/theme";

export default function KpiTile({ label, value, sub, accentValue }) {
  return (
    <div style={{
      flex: 1, minWidth: 120,
      padding: "12px 14px",
      background: COLORS.bgDeep,
      border: `1px solid ${COLORS.border}`,
      borderRadius: 6,
      display: "flex", flexDirection: "column", gap: 4,
    }}>
      <div style={{
        fontSize: 11, fontWeight: 600, letterSpacing: "0.08em",
        color: COLORS.textFaint, textTransform: "uppercase",
      }}>{label}</div>
      <div style={{
        fontFamily: MONO, fontVariantNumeric: "tabular-nums",
        fontSize: 26, fontWeight: 500,
        color: accentValue || COLORS.text, lineHeight: 1.1,
      }}>{value}</div>
      {sub && (
        <div style={{ fontSize: 12, color: COLORS.textMuted, fontFamily: MONO }}>{sub}</div>
      )}
    </div>
  );
}
