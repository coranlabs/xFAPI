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

import React, { useEffect, useState } from "react";
import IconRail from "./components/IconRail.jsx";
import Dashboard from "./pages/Dashboard.jsx";
import Sequence from "./pages/Sequence.jsx";
import Diff from "./pages/Diff.jsx";
import Vault from "./pages/Vault.jsx";
import Settings from "./pages/Settings.jsx";

export default function App() {
  const [active, setActive] = useState("dashboard");
  const [collapsed, setCollapsed] = useState(false);

  // ⌘1–4 / ⌘, navigation
  useEffect(() => {
    const onKey = (e) => {
      if (!(e.metaKey || e.ctrlKey)) return;
      const map = { 1: "dashboard", 2: "sequence", 3: "diff", 4: "vault", ",": "settings" };
      if (map[e.key]) { e.preventDefault(); setActive(map[e.key]); }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, []);

  return (
    <div style={{ position: "fixed", inset: 0, display: "flex" }}>
      <IconRail active={active} setActive={setActive} collapsed={collapsed} setCollapsed={setCollapsed} />
      <div style={{ flex: 1, minWidth: 0, overflow: "auto" }}>
        {active === "dashboard" && <Dashboard onOpenVault={() => setActive("vault")} onOpenSettings={() => setActive("settings")} />}
        {active === "sequence"  && <Sequence />}
        {active === "diff"      && <Diff />}
        {active === "vault"     && <Vault onOpenDashboard={() => setActive("dashboard")} />}
        {active === "settings"  && <Settings />}
      </div>
    </div>
  );
}
