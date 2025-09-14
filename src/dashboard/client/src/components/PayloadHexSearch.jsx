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

import React, { useState } from "react";
import { COLORS, MONO, ACCENT } from "../lib/theme";
import { api } from "../lib/api";
import { useToast } from "../lib/toast.jsx";

// Hex byte search across TX_DATA / RX_DATA payloads (server-side via
// /api/search-payload). Different from the regular filter card's text
// search, which scans message_type + content as plain text.
//
// onJump is called with a match's global_index when the user navigates to
// it; the parent should resolve that to a page+row and scroll/highlight.
export default function PayloadHexSearch({ onJump, pageSize }) {
  const toast = useToast();
  const [q, setQ] = useState("");
  const [matches, setMatches] = useState([]);
  const [cursor, setCursor] = useState(0);
  const [busy, setBusy] = useState(false);

  const run = async () => {
    const cleaned = q.trim();
    if (!cleaned) { setMatches([]); return; }
    // Server requires hex chars only after stripping spaces / 0x.
    const test = cleaned.toUpperCase().replace(/\s+/g, "").replace(/^0X/, "");
    if (!/^[0-9A-F]+$/.test(test)) {
      toast.error("Hex search expects hex bytes (0–9, A–F). Spaces/0x ok.");
      return;
    }
    setBusy(true);
    try {
      const r = await api.searchPayload(cleaned);
      setMatches(r.matches || []);
      setCursor(0);
      if ((r.matches || []).length === 0) {
        toast.info("No matches in TX_DATA/RX_DATA payloads.");
      } else if (r.matches.length === 1) {
        toast.success("1 match");
        jumpTo(r.matches[0]);
      } else {
        toast.success(`${r.matches.length.toLocaleString()} matches`);
        jumpTo(r.matches[0]);
      }
    } catch (e) {
      toast.error("Search failed: " + e.message);
    } finally {
      setBusy(false);
    }
  };

  const jumpTo = (m) => {
    if (!m || !onJump) return;
    onJump({
      messageId: m.id,
      globalIndex: m.global_index,
      page: Math.floor(m.global_index / pageSize) + 1,
    });
  };

  const next = () => {
    if (!matches.length) return;
    const i = (cursor + 1) % matches.length;
    setCursor(i); jumpTo(matches[i]);
  };
  const prev = () => {
    if (!matches.length) return;
    const i = (cursor - 1 + matches.length) % matches.length;
    setCursor(i); jumpTo(matches[i]);
  };
  const clear = () => { setQ(""); setMatches([]); setCursor(0); };

  return (
    <div style={{
      background: COLORS.bgDeep, border: `1px solid ${COLORS.border}`, borderRadius: 6,
      padding: 10, marginBottom: 12,
      display: "flex", alignItems: "center", gap: 8, flexWrap: "wrap",
    }}>
      <span style={{ fontSize: 11, fontWeight: 600, letterSpacing: "0.08em", color: COLORS.textFaint, whiteSpace: "nowrap" }}>HEX PAYLOAD SEARCH</span>
      <input
        value={q}
        onChange={(e) => setQ(e.target.value)}
        onKeyDown={(e) => { if (e.key === "Enter") run(); }}
        placeholder="e.g. 00 1A 2B 3C or 0x001A — searches TX_DATA / RX_DATA only"
        style={{
          flex: 1, minWidth: 260,
          padding: "6px 10px",
          background: COLORS.surface, color: COLORS.text,
          border: `1px solid ${COLORS.border}`, borderRadius: 4,
          fontFamily: MONO, fontSize: 13, outline: "none",
        }}
      />
      <button onClick={run} disabled={busy || !q.trim()} style={{
        padding: "5px 12px", borderRadius: 4,
        background: q.trim() ? "oklch(from var(--accent) l c h / 0.18)" : COLORS.surface,
        border: `1px solid ${q.trim() ? ACCENT : COLORS.border}`,
        color: q.trim() ? COLORS.text : COLORS.textMuted,
        cursor: busy || !q.trim() ? "wait" : "pointer",
        fontSize: 13, fontWeight: 500,
      }}>{busy ? "Searching…" : "Search"}</button>

      {matches.length > 0 && (
        <>
          <div style={{ fontFamily: MONO, fontSize: 13, color: COLORS.textMuted }}>
            {cursor + 1} / {matches.length.toLocaleString()}
          </div>
          <button onClick={prev} title="Previous match" style={navBtn}>↑</button>
          <button onClick={next} title="Next match" style={navBtn}>↓</button>
          <button onClick={clear} title="Clear" style={navBtn}>×</button>
        </>
      )}
    </div>
  );
}

const navBtn = {
  width: 28, height: 28, borderRadius: 4,
  background: COLORS.surface, border: `1px solid ${COLORS.border}`,
  color: COLORS.textMuted, cursor: "pointer", fontSize: 14,
};
