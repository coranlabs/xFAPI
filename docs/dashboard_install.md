# xFAPI Dashboard — Install & Run

Web dashboard for exploring 5G FAPI message captures
(`generated_logs/message_stats.json`).

---

## Prerequisites

| Requirement | Version |
|---|---|
| Python | 3.9+ |
| Node.js | 18+ |
| npm | 9+ |

---

## 1. Backend (FastAPI server)

```bash
cd src/dashboard/server
pip install -r requirements.txt
```

Start the server:

```bash
bash run_server.sh
```

Or directly:

```bash
uvicorn main:app --host 0.0.0.0 --port 8080 --workers 2
```

The server reads `generated_logs/message_stats.json` relative to the
xFAPI root. Point it at a different directory by setting `XFAPI_HOME`:

```bash
XFAPI_HOME=/path/to/xfapi/run uvicorn main:app --host 0.0.0.0 --port 8080
```

---

## 2. Frontend (React/Vite client)

Install dependencies (first time only):

```bash
cd src/dashboard/client
npm install
```

**Development mode** (hot-reload, proxies API to port 8080):

```bash
npm run dev
```

Open <http://localhost:5173>.

**Production build** (serves static files from the backend):

```bash
npm run build
```

The built files land in `src/dashboard/client/dist/`. The FastAPI server
serves them automatically when the `dist/` directory is present.

---

## 3. Environment variables

| Variable | Default | Purpose |
|---|---|---|
| `XFAPI_HOME` | parent of `src/dashboard/server/` | Root directory containing `generated_logs/message_stats.json` |
| `XFAPI_VAULT_DIR` | `src/dashboard/server/stats_vault/` | Directory for saved vault sessions |
| `XFAPI_CORS_ORIGINS` | `http://localhost:5173,http://localhost:8080` | Comma-separated allowed origins |
| `XFAPI_LOG_LEVEL` | `INFO` | `DEBUG` · `INFO` · `WARNING` · `ERROR` |
| `PORT` | `8080` | Backend listen port |

---

## 4. Verify

```bash
curl http://localhost:8080/api/health
```

Healthy response:

```json
{
  "status": "ok",
  "data_loaded": true,
  "messages_loaded": 100000,
  "vault_files": 0
}
```

- `data_loaded: false` → `generated_logs/message_stats.json` not found.
  Check `XFAPI_HOME` points to the correct xFAPI run directory.
- `vault_files: 0` → expected on a fresh install.

API docs: <http://localhost:8080/docs>

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `data_loaded: false` | `message_stats.json` not found | Set `XFAPI_HOME` to the directory containing `generated_logs/` |
| Port already in use | Something bound to 8080 | Set `PORT=9090` before starting the server |
| Frontend can't reach API | CORS or wrong port | Set `XFAPI_CORS_ORIGINS` to include the frontend origin |
| Vault page empty | No sessions saved yet | Use **Save to Vault** in the dashboard UI |
