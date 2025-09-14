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

import React, { createContext, useContext, useEffect, useState } from "react";
import { api } from "./api";

// Server-side fallback that matches main.py THRESHOLDS — used until the API
// call returns, so first paint isn't blocked.
const FALLBACK = {
  sla_us: 50,
  outlier_us: 100,
  hard_outlier_us: 500,
  histogram_bins_us: [1, 2, 3, 5, 10, 20, 30, 50, 100, 200, 500, 1000, 5000],
  stream_top_n: 8,
  heatmap_sfn_window: 256,
  slot_count: 20,
  default_buckets: 240,
  diff_buckets: 180,
};

const ThresholdsCtx = createContext(FALLBACK);

export function ThresholdsProvider({ children }) {
  const [t, setT] = useState(FALLBACK);
  useEffect(() => {
    fetch("/api/thresholds")
      .then((r) => r.ok ? r.json() : null)
      .then((d) => { if (d) setT({ ...FALLBACK, ...d }); })
      .catch(() => { /* fall back to defaults */ });
  }, []);
  return <ThresholdsCtx.Provider value={t}>{children}</ThresholdsCtx.Provider>;
}

export const useThresholds = () => useContext(ThresholdsCtx);
