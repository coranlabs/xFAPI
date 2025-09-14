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

// Tiny fetch wrapper. Same-origin in production (FastAPI serves the bundle);
// in dev the Vite proxy forwards /api → :8080.

async function jget(path) {
  const r = await fetch(path);
  if (!r.ok) {
    const text = await r.text();
    throw new Error(`${r.status} ${r.statusText}: ${text}`);
  }
  return r.json();
}

async function jpost(path, body) {
  const r = await fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body || {}),
  });
  if (!r.ok) {
    const text = await r.text();
    throw new Error(`${r.status} ${r.statusText}: ${text}`);
  }
  return r.json();
}

async function jpatch(path, body) {
  const r = await fetch(path, {
    method: "PATCH",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body || {}),
  });
  if (!r.ok) {
    const text = await r.text();
    throw new Error(`${r.status} ${r.statusText}: ${text}`);
  }
  return r.json();
}

async function jdelete(path) {
  const r = await fetch(path, { method: "DELETE" });
  if (!r.ok) {
    const text = await r.text();
    throw new Error(`${r.status} ${r.statusText}: ${text}`);
  }
  return r.json();
}

export const api = {
  stats:      ()                  => jget("/api/stats"),
  refresh:    ()                  => jget("/api/refresh"),
  messages:   (params = {})       => {
    const q = new URLSearchParams();
    for (const [k, v] of Object.entries(params))
      if (v !== undefined && v !== null && v !== "") q.append(k, v);
    return jget(`/api/messages?${q.toString()}`);
  },
  message:    (id)                => jget(`/api/message/${id}`),
  searchPayload: (query)          => jget(`/api/search-payload?query=${encodeURIComponent(query)}`),
  timeseries: (buckets = 240)     => jget(`/api/timeseries?buckets=${buckets}`),
  sequence:   (params = {})       => {
    const q = new URLSearchParams();
    for (const [k, v] of Object.entries(params))
      if (v !== undefined && v !== null && v !== "") q.append(k, v);
    return jget(`/api/sequence?${q.toString()}`);
  },
  diff:       (a, b, buckets = 180) =>
    jget(`/api/diff?a=${encodeURIComponent(a)}&b=${encodeURIComponent(b)}&buckets=${buckets}`),
  vault: {
    list:   ()                                       => jget("/api/vault/list"),
    save:   ({ name, tag, note, build_version })     =>
      jpost("/api/vault/save", { name, tag, note, build_version }),
    load:   (filename)                               => jpost("/api/vault/load", { filename }),
    update: (filename, patch)                        => jpatch(`/api/vault/${filename}`, patch),
    remove: (filename)                               => jdelete(`/api/vault/delete/${filename}`),
  },
  path: {
    current:  () => jget("/api/current-path"),
    validate: (path) => jpost("/api/validate-path", { path }),
    load:     (path) => jpost("/api/load-path", { path }),
  },
};
