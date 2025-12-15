# syntax=docker/dockerfile:1.7
#
# Production image for the XFAPI dashboard.
# Build context: src/dashboard/
#
# Stage 1: Node Alpine builds the React/Vite bundle.
# Stage 2: Python slim runs FastAPI/uvicorn and serves the built bundle.
#
# Runtime contract:
#   - Mount the host XFAPI directory at /data/xfapi (read-only is fine):
#       docker run -v $XFAPI_HOME:/data/xfapi:ro ...
#   - Mount a writable directory at /data/vault for saved sessions:
#       docker run -v xfapi-vault:/data/vault ...
#   - Override $WEB_CONCURRENCY to tune worker count (default 2).
#   - $XFAPI_CORS_ORIGINS  — comma-separated allowed origins (default same-origin)
#   - $XFAPI_LOG_LEVEL     — INFO | DEBUG | WARNING (default INFO)
#   - $XFAPI_LOG_FORMAT    — json | text (default json)


# ── Stage 1 — client build ────────────────────────────────────────────────
FROM node:20-alpine AS client-builder
WORKDIR /build

# Install deps first for layer-cache friendliness.
COPY client/package.json client/package-lock.json* ./
RUN npm ci --no-audit --no-fund

# Then the source.
COPY client/index.html ./
COPY client/vite.config.js ./
COPY client/public ./public
COPY client/src ./src

RUN npm run build  # writes /build/dist


# ── Stage 2 — runtime ─────────────────────────────────────────────────────
FROM python:3.12-slim AS runtime

# Build args bubbled into image metadata for auditability.
ARG GIT_SHA="unknown"
ARG BUILD_DATE="unknown"
LABEL org.opencontainers.image.title="XFAPI Dashboard" \
      org.opencontainers.image.description="5G FAPI message stats dashboard (React + FastAPI)" \
      org.opencontainers.image.source="https://github.com/coranlabs/xfapi" \
      org.opencontainers.image.revision="${GIT_SHA}" \
      org.opencontainers.image.created="${BUILD_DATE}"

ENV PYTHONUNBUFFERED=1 \
    PYTHONDONTWRITEBYTECODE=1 \
    PIP_NO_CACHE_DIR=1 \
    PIP_DISABLE_PIP_VERSION_CHECK=1 \
    # Dashboard runtime defaults — override per `docker run -e ...`
    XFAPI_HOME=/data/xfapi \
    XFAPI_VAULT_DIR=/data/vault \
    XFAPI_LOG_LEVEL=INFO \
    XFAPI_LOG_FORMAT=json \
    XFAPI_CORS_ORIGINS=http://localhost:8080,http://127.0.0.1:8080 \
    WEB_CONCURRENCY=2 \
    PORT=8080

# curl is used by Docker HEALTHCHECK below; nothing else needed at runtime.
RUN apt-get update \
 && apt-get install -y --no-install-recommends curl \
 && rm -rf /var/lib/apt/lists/*

# Python deps in their own layer for caching.
COPY server/requirements.txt /tmp/requirements.txt
RUN pip install -r /tmp/requirements.txt && rm /tmp/requirements.txt

# Create a non-root user. UID 1000 is the conventional first user on most
# host systems, so bind-mounted directories will be writable without
# fiddling with `chown` on the host.
RUN groupadd --system --gid 1000 xfapi \
 && useradd  --system --uid 1000 --gid 1000 --home /app --shell /usr/sbin/nologin xfapi

WORKDIR /app

# Server source.
COPY server/ /app/server/
# Built client bundle from stage 1, placed exactly where server/main.py
# expects it (../client/dist relative to /app/server).
COPY --from=client-builder /build/dist /app/client/dist

# Vault dir — needs to be writable. Owned by xfapi so mounting a volume
# inherits the right permissions on most setups.
RUN mkdir -p /data/xfapi /data/vault \
 && chown -R xfapi:xfapi /app /data

USER xfapi
WORKDIR /app/server

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
  CMD curl -fsS "http://127.0.0.1:${PORT}/api/health" >/dev/null || exit 1

# Use uvicorn directly. --proxy-headers lets it trust X-Forwarded-* when
# behind a reverse proxy; --forwarded-allow-ips=* opens that to all
# upstream IPs (tighten in your deployment if needed).
CMD ["sh", "-c", "exec uvicorn main:app \
  --host 0.0.0.0 --port ${PORT} \
  --workers ${WEB_CONCURRENCY} \
  --proxy-headers --forwarded-allow-ips='*'"]
