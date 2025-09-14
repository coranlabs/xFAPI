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

import React, { createContext, useCallback, useContext, useState } from "react";
import { COLORS } from "./theme";

const ToastCtx = createContext(null);

export function ToastProvider({ children }) {
  const [toasts, setToasts] = useState([]);
  const push = useCallback((kind, text, ms = 3500) => {
    const id = Math.random().toString(36).slice(2);
    setToasts((ts) => [...ts, { id, kind, text }]);
    setTimeout(() => setToasts((ts) => ts.filter((t) => t.id !== id)), ms);
  }, []);
  const value = {
    success: (t, ms) => push("success", t, ms),
    error:   (t, ms) => push("error", t, ms ?? 6000),
    info:    (t, ms) => push("info", t, ms),
  };
  return (
    <ToastCtx.Provider value={value}>
      {children}
      <div style={{
        position: "fixed", bottom: 20, right: 20,
        display: "flex", flexDirection: "column", gap: 8,
        zIndex: 99999, pointerEvents: "none",
      }}>
        {toasts.map((t) => (
          <div key={t.id} style={{
            pointerEvents: "auto",
            padding: "10px 14px",
            borderRadius: 8,
            border: `1px solid ${COLORS.border}`,
            background: COLORS.surface,
            color: COLORS.text,
            fontSize: 13,
            minWidth: 240,
            boxShadow: "0 8px 24px rgba(0,0,0,0.4)",
            borderLeft: `3px solid ${
              t.kind === "success" ? COLORS.green :
              t.kind === "error"   ? COLORS.red   :
                                     COLORS.cyan}`,
          }}>{t.text}</div>
        ))}
      </div>
    </ToastCtx.Provider>
  );
}

export function useToast() {
  const c = useContext(ToastCtx);
  if (!c) throw new Error("useToast outside ToastProvider");
  return c;
}
