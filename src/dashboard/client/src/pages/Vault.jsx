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
import { COLORS, MONO, ACCENT, fmtNum, fmtMicro, fmtBytes, fmtDuration, fmtDate } from "../lib/theme";
import { api } from "../lib/api";
import { useToast } from "../lib/toast.jsx";
import Sparkline from "../components/charts/Sparkline.jsx";

const TAGS = ["all", "live", "baseline", "flagged", "archived"];
const TAG_COLORS = {
  live: COLORS.cyan,
  baseline: COLORS.green,
  flagged: COLORS.amber,
  archived: COLORS.textMuted,
};

export default function Vault({ onOpenDashboard }) {
  const toast = useToast();
  const [files, setFiles] = useState([]);
  const [tagFilter, setTagFilter] = useState("all");
  const [sortBy, setSortBy] = useState("saved");
  const [editingFile, setEditingFile] = useState(null);
  const [saveOpen, setSaveOpen] = useState(false);

  const reload = () =>
    api.vault.list()
      .then((r) => setFiles(r.files || []))
      .catch((e) => toast.error("Vault list failed: " + e.message));

  useEffect(() => { reload(); }, []);

  const visible = useMemo(() => {
    let out = files;
    if (tagFilter !== "all") out = out.filter((f) => f.tag === tagFilter);
    if (sortBy === "saved") out = [...out].sort((a, b) => b.saved_date - a.saved_date);
    if (sortBy === "captured") out = [...out].sort((a, b) => (b.captured_date || 0) - (a.captured_date || 0));
    if (sortBy === "messages") out = [...out].sort((a, b) => b.message_count - a.message_count);
    if (sortBy === "name") out = [...out].sort((a, b) => a.name.localeCompare(b.name));
    return out;
  }, [files, tagFilter, sortBy]);

  const onLoad = async (f) => {
    try {
      const r = await api.vault.load(f.filename);
      toast.success(`Loaded "${f.name}" (${fmtNum(r.message_count)})`);
      onOpenDashboard?.();
    } catch (e) {
      toast.error("Load failed: " + e.message);
    }
  };

  const onDelete = async (f) => {
    if (!confirm(`Delete vault file "${f.name}"?`)) return;
    try {
      await api.vault.remove(f.filename);
      toast.success(`Deleted "${f.name}"`);
      reload();
    } catch (e) {
      toast.error("Delete failed: " + e.message);
    }
  };

  return (
    <div style={{ padding: "18px 18px 24px", color: COLORS.text }}>
      <div style={{ display: "flex", alignItems: "baseline", gap: 14, marginBottom: 14 }}>
        <h2 style={{ margin: 0, fontSize: 18, fontWeight: 500 }}>Vault</h2>
        <span style={{ color: COLORS.textFaint, fontSize: 12 }}>{files.length} captures</span>
        <div style={{ flex: 1 }} />
        <button onClick={() => setSaveOpen(true)} style={{
          padding: "6px 14px", borderRadius: 4,
          background: "oklch(from var(--accent) l c h / 0.18)",
          border: `1px solid ${ACCENT}`,
          color: COLORS.text, cursor: "pointer", fontSize: 12, fontWeight: 500,
        }}>+ Save Current Session</button>
      </div>

      <div style={{ display: "flex", gap: 14, alignItems: "center", marginBottom: 14, flexWrap: "wrap" }}>
        <div style={{ display: "flex", gap: 4 }}>
          {TAGS.map((t) => (
            <button key={t} onClick={() => setTagFilter(t)} style={{
              padding: "4px 12px", borderRadius: 4,
              background: tagFilter === t ? "oklch(from var(--accent) l c h / 0.18)" : COLORS.surface,
              border: `1px solid ${tagFilter === t ? ACCENT : COLORS.border}`,
              color: tagFilter === t ? COLORS.text : COLORS.textMuted,
              fontSize: 11, textTransform: "capitalize", cursor: "pointer",
            }}>{t}</button>
          ))}
        </div>
        <div style={{ flex: 1 }} />
        <span style={{ fontSize: 11, color: COLORS.textFaint }}>SORT BY</span>
        <select value={sortBy} onChange={(e) => setSortBy(e.target.value)} style={{
          background: COLORS.surface, color: COLORS.text,
          border: `1px solid ${COLORS.border}`, borderRadius: 4,
          fontSize: 11, padding: "4px 6px",
        }}>
          <option value="saved">Save date</option>
          <option value="captured">Capture date</option>
          <option value="messages">Message count</option>
          <option value="name">Name</option>
        </select>
      </div>

      {visible.length === 0 ? (
        <div style={{ padding: 40, textAlign: "center", color: COLORS.textFaint, border: `1px dashed ${COLORS.border}`, borderRadius: 6 }}>
          No vault captures match this filter.
        </div>
      ) : (
        <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>
          {visible.map((f) => (
            <VaultCard key={f.filename} f={f}
              onLoad={() => onLoad(f)}
              onDelete={() => onDelete(f)}
              onEdit={() => setEditingFile(f)}
            />
          ))}
        </div>
      )}

      {editingFile && (
        <EditMetaModal file={editingFile}
          onClose={() => setEditingFile(null)}
          onSaved={() => { setEditingFile(null); reload(); }}
        />
      )}
      {saveOpen && (
        <SaveSessionModal onClose={() => setSaveOpen(false)}
          onSaved={() => { setSaveOpen(false); reload(); }}
        />
      )}
    </div>
  );
}

function VaultCard({ f, onLoad, onDelete, onEdit }) {
  const tagColor = TAG_COLORS[f.tag] || COLORS.textMuted;
  return (
    <div style={{
      background: COLORS.bgDeep, border: `1px solid ${COLORS.border}`,
      borderRadius: 6, padding: "10px 14px",
      display: "flex", alignItems: "center", gap: 14,
    }}>
      {/* Identity column — tag + name + (optional) note. Fixed-ish width so
          stat columns line up across rows. */}
      <div style={{ width: 280, minWidth: 220, display: "flex", flexDirection: "column", gap: 4 }}>
        <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
          <span style={{
            padding: "2px 8px", borderRadius: 99,
            background: `oklch(from ${tagColor} l c h / 0.16)`,
            border: `1px solid ${tagColor}`, color: tagColor,
            fontSize: 9.5, fontWeight: 600, letterSpacing: "0.08em", textTransform: "uppercase",
            whiteSpace: "nowrap", flexShrink: 0,
          }}>{f.tag}</span>
          <div style={{
            flex: 1, fontFamily: MONO, fontWeight: 500,
            color: COLORS.text, fontSize: 14,
            overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap",
          }} title={f.name}>{f.name}</div>
        </div>
        {f.note && (
          <div title={f.note} style={{
            fontSize: 11.5, color: COLORS.textMuted, lineHeight: 1.3,
            overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap",
          }}>{f.note}</div>
        )}
      </div>

      {/* Sparkline — fixed width so rows align vertically. */}
      <div style={{ flexShrink: 0 }}>
        {f.sparkline && f.sparkline.length > 0 ? (
          <Sparkline data={f.sparkline} width={140} height={32} color={ACCENT} />
        ) : (
          <div style={{ width: 140, height: 32 }} />
        )}
      </div>

      {/* Inline stats. Flex-grow so they spread out evenly. */}
      <div style={{
        flex: 1, minWidth: 0,
        display: "flex", alignItems: "center", gap: 16,
        fontFamily: MONO, fontSize: 12, flexWrap: "wrap",
      }}>
        <Stat k="Msgs"     v={fmtNum(f.message_count)} />
        <Stat k="Size"     v={fmtBytes(f.size)} />
        <Stat k="Duration" v={f.duration_ms != null ? fmtDuration(f.duration_ms) : "—"} />
        <Stat k="P99 µs"   v={f.p99_us != null ? fmtMicro(f.p99_us) : "—"} />
        <Stat k="Errors"   v={f.error_count != null ? fmtNum(f.error_count) : "—"} />
        <Stat k="Captured" v={fmtDate(f.captured_date)} />
        {f.build_version && <Stat k="Build" v={f.build_version} />}
      </div>

      {/* Actions, right-aligned. */}
      <div style={{ display: "flex", gap: 6, flexShrink: 0 }}>
        <button onClick={onEdit} title="Edit metadata" style={iconBtn}>✎</button>
        <button onClick={onLoad} style={{
          padding: "6px 14px", borderRadius: 4,
          background: "oklch(from var(--accent) l c h / 0.18)",
          border: `1px solid ${ACCENT}`,
          color: COLORS.text, cursor: "pointer",
          fontSize: 12, fontWeight: 500,
        }}>Load</button>
        <button onClick={onDelete} style={{
          padding: "6px 10px", borderRadius: 4,
          background: COLORS.surface,
          border: `1px solid ${COLORS.border}`,
          color: COLORS.textMuted, cursor: "pointer", fontSize: 12,
        }}>Delete</button>
      </div>
    </div>
  );
}

const Stat = ({ k, v }) => (
  <div style={{ minWidth: 70 }}>
    <div style={{ fontSize: 9, color: COLORS.textFaint, letterSpacing: "0.08em" }}>{k.toUpperCase()}</div>
    <div style={{ color: COLORS.text }}>{v}</div>
  </div>
);

const iconBtn = {
  width: 24, height: 24, padding: 0,
  background: "transparent", border: `1px solid ${COLORS.border}`, borderRadius: 4,
  color: COLORS.textMuted, cursor: "pointer", fontSize: 13,
};

function EditMetaModal({ file, onClose, onSaved }) {
  const toast = useToast();
  const [tag, setTag] = useState(file.tag || "archived");
  const [note, setNote] = useState(file.note || "");
  const [build, setBuild] = useState(file.build_version || "");
  const [saving, setSaving] = useState(false);

  const save = async () => {
    setSaving(true);
    try {
      await api.vault.update(file.filename, { tag, note, build_version: build });
      toast.success("Updated metadata");
      onSaved();
    } catch (e) {
      toast.error("Update failed: " + e.message);
      setSaving(false);
    }
  };

  return <Modal title={`Edit "${file.name}"`} onClose={onClose}>
    <Field label="Tag">
      <select value={tag} onChange={(e) => setTag(e.target.value)} style={inputStyle}>
        {["live", "baseline", "flagged", "archived"].map((t) => <option key={t} value={t}>{t}</option>)}
      </select>
    </Field>
    <Field label="Note">
      <textarea value={note} onChange={(e) => setNote(e.target.value)} rows={3} style={{ ...inputStyle, resize: "vertical" }} />
    </Field>
    <Field label="Build version">
      <input value={build} onChange={(e) => setBuild(e.target.value)} placeholder="e.g. v0.1.0" style={inputStyle} />
    </Field>
    <div style={{ display: "flex", gap: 8, marginTop: 12, justifyContent: "flex-end" }}>
      <button onClick={onClose} style={{ ...modalBtn, background: COLORS.surface, color: COLORS.textMuted }}>Cancel</button>
      <button onClick={save} disabled={saving} style={modalBtn}>{saving ? "Saving…" : "Save"}</button>
    </div>
  </Modal>;
}

function SaveSessionModal({ onClose, onSaved }) {
  const toast = useToast();
  const [name, setName] = useState("");
  const [tag, setTag] = useState("archived");
  const [note, setNote] = useState("");
  const [build, setBuild] = useState("");
  const [saving, setSaving] = useState(false);
  const submit = async () => {
    if (!name.trim()) return;
    setSaving(true);
    try {
      await api.vault.save({ name, tag, note, build_version: build });
      toast.success(`Saved "${name}"`);
      onSaved();
    } catch (e) {
      toast.error("Save failed: " + e.message);
      setSaving(false);
    }
  };
  return <Modal title="Save current session" onClose={onClose}>
    <Field label="Name">
      <input value={name} onChange={(e) => setName(e.target.value)} placeholder="session_name" style={inputStyle} autoFocus />
    </Field>
    <Field label="Tag">
      <select value={tag} onChange={(e) => setTag(e.target.value)} style={inputStyle}>
        {["live", "baseline", "flagged", "archived"].map((t) => <option key={t} value={t}>{t}</option>)}
      </select>
    </Field>
    <Field label="Note">
      <textarea value={note} onChange={(e) => setNote(e.target.value)} rows={3} style={{ ...inputStyle, resize: "vertical" }} />
    </Field>
    <Field label="Build version">
      <input value={build} onChange={(e) => setBuild(e.target.value)} placeholder="optional" style={inputStyle} />
    </Field>
    <div style={{ display: "flex", gap: 8, marginTop: 12, justifyContent: "flex-end" }}>
      <button onClick={onClose} style={{ ...modalBtn, background: COLORS.surface, color: COLORS.textMuted }}>Cancel</button>
      <button onClick={submit} disabled={!name.trim() || saving} style={modalBtn}>{saving ? "Saving…" : "Save"}</button>
    </div>
  </Modal>;
}

function Modal({ title, onClose, children }) {
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
          <h3 style={{ margin: 0, fontSize: 14, fontWeight: 500 }}>{title}</h3>
          <button onClick={onClose} style={{ background: "transparent", border: "none", color: COLORS.textMuted, fontSize: 18, cursor: "pointer" }}>×</button>
        </div>
        {children}
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
