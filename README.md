# xFAPI

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

**xFAPI** is an open-source 5G FAPI message bridge that connects a PHY-layer L1 and a MAC-layer L2 within a 5G New Radio stack. It forwards FAPI P7 messages bidirectionally over shared memory using DPDK, supporting both co-located and distributed deployments.

---

## Features

- Bidirectional FAPI P7 message forwarding (L1 ↔ L2)
- DPDK-based shared-memory transport (SECONDARY process)
- YAML-configurable endpoints, threading, and log levels
- Ring-buffered message statistics with JSON export
- Web dashboard for live and post-capture inspection

## Architecture

```
┌────────────┐  FAPI P7  ┌──────────┐  FAPI P7  ┌────────────┐
│  L1 (PHY)  │ ◄────────►│  xFAPI   │◄──────────►│  L2 (MAC)  │
└────────────┘           └──────────┘            └────────────┘
                         DPDK xSM IPC
```

## Getting Started

See [docs/BUILD_AND_RUN.md](docs/BUILD_AND_RUN.md) for prerequisites, build steps, and startup order.

## Dashboard

See [docs/dashboard_install.md](docs/dashboard_install.md) for the web dashboard setup.

## License

Apache License 2.0 — see [LICENSE](LICENSE).

## Dashboard

xFAPI ships a web dashboard for exploring captured 5G FAPI message statistics
in real time. It connects to the FastAPI backend (`src/dashboard/server`) and
renders per-message KPI tiles, flow charts, sequence diagrams, and a hex
payload search.

See [docs/dashboard_install.md](docs/dashboard_install.md) for setup.
