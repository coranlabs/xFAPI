# xFAPI — Build & Run

A short guide for compiling and running the xFAPI bridge in `OCUDU_OCUDU` mode.

## 1. Prerequisites

Install the toolchain and runtime dependencies:

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config \
                    libyaml-dev zlib1g-dev libhugetlbfs-dev
```

DPDK must be installed separately. Export its install prefix before building:

```bash
export DPDK_PATH=/path/to/dpdk/install
```

(`setup_env.sh` in the repo root is a convenience helper for setting this.)

## 2. Build

From the repository root:

```bash
./build_xfapi.sh --mode=ocudu_ocudu
```

This runs CMake into `build/` and produces the binary at `bin/xfapi_main`.

Other useful invocations:

```bash
./build_xfapi.sh --clean         # remove build/ and bin/
./build_xfapi.sh --mode=ocudu_ocudu -v   # verbose make output
./build_xfapi.sh --help          # list all options
```

## 3. Hugepages (one-time, per boot)

DPDK needs hugepages reserved before the process starts:

```bash
echo 1024 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
sudo mkdir -p /mnt/huge
sudo mount -t hugetlbfs nodev /mnt/huge
```

## 4. Configure

Edit the sample config to match your deployment:

```
conf/ocudu_ocudu_config.yaml
```

Key fields to review: `dpdk.file_prefix`, `dpdk.lcores`, xSM memzone names,
and the L1/L2 endpoint settings.

## 5. Run

```bash
./run_xfapi.sh
```

`run_xfapi.sh` invokes the binary with the config path baked into the script.
To use a different config, either edit `MAIN_CONFIG_FILE` at the top of
`run_xfapi.sh` or run the binary directly:

```bash
sudo ./bin/xfapi_main --cfgfile conf/ocudu_ocudu_config.yaml
```

`sudo` is typically required for DPDK hugepage and NIC access.

## 6. Stopping

Press `Ctrl+C`. The signal handler flushes message statistics to
`message_stats.json` and (if enabled in the config) writes logs to
`generated_logs/xfapi_logs.txt`.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `DPDK_PATH environment variable not set` at cmake time | `export DPDK_PATH=...` before running `build_xfapi.sh` |
| `libxsm.so not found` at cmake time | `src/ipc/xsm/libxsm.so` is missing from the tree |
| `Cannot mmap memory` / hugepage errors at runtime | hugepages not reserved or `/mnt/huge` not mounted |
| `Permission denied` on `/dev/hugepages` | run with `sudo` |
| `--cfgfile is mandatory` | pass a YAML config path with `--cfgfile` |
