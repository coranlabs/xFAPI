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
import { COLORS, ACCENT, MONO } from "../lib/theme";

export const NAV = [
  { id: "dashboard", label: "Dashboard", kbd: "⌘1" },
  { id: "sequence",  label: "Sequence",  kbd: "⌘2" },
  { id: "diff",      label: "Diff",      kbd: "⌘3" },
  { id: "vault",     label: "Vault",     kbd: "⌘4" },
  { id: "settings",  label: "Settings",  kbd: "⌘," },
];

function NavIcon({ id, active }) {
  const c = active ? ACCENT : COLORS.textMuted;
  const p = { width: 20, height: 20, viewBox: "0 0 20 20", fill: "none", stroke: c, strokeWidth: "1.5", strokeLinecap: "round", strokeLinejoin: "round" };
  if (id === "dashboard") return (
    <svg {...p}>
      <rect x="3" y="3" width="6" height="9" rx="1" />
      <rect x="11" y="3" width="6" height="5" rx="1" />
      <rect x="3" y="14" width="6" height="3" rx="1" />
      <rect x="11" y="10" width="6" height="7" rx="1" />
    </svg>
  );
  if (id === "sequence") return (
    <svg {...p}>
      <circle cx="5" cy="4" r="1.5" /><path d="M5 5.5 V16" />
      <circle cx="15" cy="4" r="1.5" /><path d="M15 5.5 V16" />
      <path d="M5.5 8 H13.5 M13 7 L14 8 L13 9" />
      <path d="M14.5 12 H6.5 M7 11 L6 12 L7 13" />
    </svg>
  );
  if (id === "diff") return (
    <svg {...p}>
      <rect x="2.5" y="3.5" width="6.5" height="13" rx="1" />
      <rect x="11" y="3.5" width="6.5" height="13" rx="1" />
      <path d="M4.5 7 H7 M4.5 10 H6.5 M4.5 13 H7" />
      <path d="M13 7 H15.5 M13 10 H15 M13 13 H15.5" stroke={ACCENT} strokeOpacity={active ? 1 : 0.5} />
    </svg>
  );
  if (id === "vault") return (
    <svg {...p}>
      <rect x="3" y="4" width="14" height="4" rx="1" />
      <rect x="3.5" y="9" width="13" height="7.5" rx="1" />
      <path d="M8 12 H12" />
      <circle cx="10" cy="12" r="0.9" fill={c} stroke="none" />
    </svg>
  );
  if (id === "settings") return (
    <svg {...p}>
      <path d="M3 6 H9 M13 6 H17" />
      <circle cx="11" cy="6" r="1.6" fill={active ? c : "none"} />
      <path d="M3 10 H6 M10 10 H17" />
      <circle cx="8" cy="10" r="1.6" />
      <path d="M3 14 H12 M16 14 H17" />
      <circle cx="14" cy="14" r="1.6" fill={active ? c : "none"} />
    </svg>
  );
  return null;
}

export default function IconRail({ active, setActive, collapsed, setCollapsed }) {
  const W = collapsed ? 76 : 200;
  const railBg = "oklch(0.11 0.014 250)";

  const NavBtn = ({ n }) => {
    const isActive = active === n.id;
    return (
      <button onClick={() => setActive(n.id)}
        title={collapsed ? `${n.label}  ${n.kbd}` : undefined}
        style={{
          display: "flex", alignItems: "center",
          gap: 12, padding: collapsed ? "0" : "0 12px",
          height: 42, width: collapsed ? 44 : "calc(100% - 16px)",
          margin: collapsed ? "0 auto" : "0 8px",
          justifyContent: collapsed ? "center" : "flex-start",
          background: isActive ? "oklch(from var(--accent) l c h / 0.14)" : "transparent",
          border: "none", borderRadius: 8, cursor: "pointer",
          position: "relative",
          color: isActive ? COLORS.text : COLORS.textMuted,
          fontFamily: "inherit", fontSize: 13.5, fontWeight: isActive ? 500 : 400,
          textAlign: "left",
        }}
        onMouseEnter={(e) => { if (!isActive) e.currentTarget.style.background = "oklch(0.18 0.014 250 / 0.6)"; }}
        onMouseLeave={(e) => { if (!isActive) e.currentTarget.style.background = "transparent"; }}>
        {isActive && (
          <span style={{
            position: "absolute", left: collapsed ? -8 : 0, top: 9, bottom: 9, width: 2,
            background: ACCENT, borderRadius: 2,
          }} />
        )}
        <span style={{ width: 20, height: 20, display: "flex", alignItems: "center", justifyContent: "center", flexShrink: 0 }}>
          <NavIcon id={n.id} active={isActive} />
        </span>
        {!collapsed && <span style={{ flex: 1 }}>{n.label}</span>}
        {!collapsed && (
          <span style={{ fontFamily: MONO, fontSize: 10.5, color: COLORS.textFaint, letterSpacing: "0.04em" }}>{n.kbd}</span>
        )}
      </button>
    );
  };

  return (
    <div style={{
      width: W, flexShrink: 0,
      background: railBg,
      borderRight: `1px solid ${COLORS.border}`,
      display: "flex", flexDirection: "column",
      transition: "width .18s ease",
      overflow: "hidden",
    }}>
      <div style={{
        display: "flex", alignItems: "center",
        height: 104, padding: collapsed ? "0" : "0 14px 0 18px",
        justifyContent: collapsed ? "center" : "space-between",
        borderBottom: `1px solid ${COLORS.border}`,
      }}>
        <div title="xFAPI" style={{
          height: collapsed ? 72 : 80,
          display: "flex", alignItems: "center", justifyContent: "center",
        }}>
          <img src="/assets/xfapi_logo.png" alt="xFAPI"
               style={{ height: "100%", width: "auto", display: "block",
                        filter: "invert(1) hue-rotate(180deg)" }} />
        </div>
        {!collapsed && (
          <button onClick={() => setCollapsed(true)}
            title="Collapse sidebar"
            style={{
              width: 28, height: 28, borderRadius: 6,
              background: "transparent", border: `1px solid ${COLORS.border}`,
              color: COLORS.textMuted, cursor: "pointer",
              display: "flex", alignItems: "center", justifyContent: "center",
            }}>
            <svg width="12" height="12" viewBox="0 0 12 12" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round">
              <path d="M7 3 L4 6 L7 9" /><path d="M10 3 V9" />
            </svg>
          </button>
        )}
      </div>

      <div style={{ display: "flex", flexDirection: "column", gap: 2, padding: "12px 0" }}>
        {!collapsed && (
          <div style={{ padding: "0 20px 6px", fontSize: 10, fontWeight: 600, letterSpacing: "0.1em", color: COLORS.textFaint }}>NAVIGATE</div>
        )}
        {NAV.map((n) => <NavBtn key={n.id} n={n} />)}
      </div>

      <div style={{ flex: 1 }} />

      {collapsed && (
        <button onClick={() => setCollapsed(false)} title="Expand sidebar"
          style={{
            margin: "0 auto 8px", width: 36, height: 32, borderRadius: 7,
            background: "transparent", border: `1px solid ${COLORS.border}`,
            color: COLORS.textMuted, cursor: "pointer",
            display: "flex", alignItems: "center", justifyContent: "center",
          }}>
          <svg width="12" height="12" viewBox="0 0 12 12" fill="none" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" strokeLinejoin="round">
            <path d="M5 3 L8 6 L5 9" /><path d="M2 3 V9" />
          </svg>
        </button>
      )}

      <div style={{
        borderTop: `1px solid ${COLORS.border}`,
        padding: collapsed ? "12px 0" : "12px 12px 14px",
        display: "flex", flexDirection: "column", alignItems: "center", gap: 8,
      }}>
        <div title="Coran Labs" style={{
          width: collapsed ? 64 : "100%", display: "flex",
          alignItems: "center", justifyContent: "center",
          padding: collapsed ? 0 : "6px 4px",
          opacity: 0.9,
        }}>
          <img src="/assets/CoranLabs Logo.png" alt="Coran Labs"
               style={{ height: collapsed ? 56 : 72, width: "auto", objectFit: "contain", display: "block" }} />
        </div>
      </div>
    </div>
  );
}
