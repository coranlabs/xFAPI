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

// Modal: save the currently-loaded dataset into the vault with metadata.
// Used from both the Dashboard "Save to Vault" button and the Vault page's
// "+ Save Current Session" button.
export default function SaveToVaultModal({ onClose, onSaved, defaultName = "" }) {
  const toast = useToast();
  const [name, setName] = useState(defaultName);
  const [tag, setTag] = useState("archived");
  const [note, setNote] = useState("");
  const [build, setBuild] = useState("");
  const [saving, setSaving] = useState(false);

  const submit = async () => {
    if (!name.trim()) return;
    setSaving(true);
    try {
      await api.vault.save({ name: name.trim(), tag, note: note.trim(), build_version: build.trim() });
      toast.success(`Saved "${name.trim()}" to vault`);
      onSaved?.();
    } catch (e) {
      // Most common: 409 file exists
      const msg = String(e.message || "").includes("409")
        ? "A vault file with this name already exists."
        : "Save failed: " + e.message;
      toast.error(msg);
      setSaving(false);
    }
  };

  return (
    <div onClick={onClose} style={{
      position: "fixed", inset: 0, zIndex: 9999,
      background: "rgba(0,0,0,0.6)", backdropFilter: "blur(4px)",
      display: "flex", alignItems: "center", justifyContent: "center",
    }}>
      <div onClick={(e) => e.stopPropagation()} style={{
        width: 480, maxWidth: "92vw", maxHeight: "92vh", overflow: "auto",
        background: COLORS.bg, border: `1px solid ${COLORS.borderHi}`, borderRadius: 8,
        padding: 18,
      }}>
        <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center", marginBottom: 14 }}>
          <h3 style={{ margin: 0, fontSize: 14, fontWeight: 500 }}>Save current session to vault</h3>
          <button onClick={onClose} style={{ background: "transparent", border: "none", color: COLORS.textMuted, fontSize: 18, cursor: "pointer" }}>×</button>
        </div>

        <Field label="Name">
          <input value={name} onChange={(e) => setName(e.target.value)}
            placeholder="session_name" style={inputStyle} autoFocus
            onKeyDown={(e) => { if (e.key === "Enter" && name.trim() && !saving) submit(); }} />
        </Field>
        <Field label="Tag">
          <select value={tag} onChange={(e) => setTag(e.target.value)} style={inputStyle}>
            {["live", "baseline", "flagged", "archived"].map((t) => <option key={t} value={t}>{t}</option>)}
          </select>
        </Field>
        <Field label="Note (optional)">
          <textarea value={note} onChange={(e) => setNote(e.target.value)} rows={3}
            placeholder="What is interesting about this capture?"
            style={{ ...inputStyle, resize: "vertical" }} />
        </Field>
        <Field label="Build version (optional)">
          <input value={build} onChange={(e) => setBuild(e.target.value)}
            placeholder="e.g. v0.1.0" style={inputStyle} />
        </Field>

        <div style={{ display: "flex", gap: 8, marginTop: 14, justifyContent: "flex-end" }}>
          <button onClick={onClose} style={{ ...modalBtn, background: COLORS.surface, color: COLORS.textMuted, borderColor: COLORS.border }}>Cancel</button>
          <button onClick={submit} disabled={!name.trim() || saving} style={{
            ...modalBtn,
            opacity: !name.trim() || saving ? 0.5 : 1,
            cursor: !name.trim() || saving ? "not-allowed" : "pointer",
          }}>{saving ? "Saving…" : "Save to Vault"}</button>
        </div>
      </div>
    </div>
  );
}

const Field = ({ label, children }) => (
  <div style={{ marginBottom: 10 }}>
    <div style={{ fontSize: 9.5, fontWeight: 600, letterSpacing: "0.08em", color: COLORS.textFaint, marginBottom: 4 }}>{label.toUpperCase()}</div>
    {children}
  </div>
);
const inputStyle = {
  width: "100%", padding: "7px 10px",
  background: COLORS.surface, color: COLORS.text,
  border: `1px solid ${COLORS.border}`, borderRadius: 4,
  fontFamily: MONO, fontSize: 12, outline: "none",
  boxSizing: "border-box",
};
const modalBtn = {
  padding: "7px 14px", borderRadius: 4,
  background: "oklch(from var(--accent) l c h / 0.18)",
  border: `1px solid ${ACCENT}`,
  color: COLORS.text, cursor: "pointer",
  fontSize: 12, fontWeight: 500,
};
