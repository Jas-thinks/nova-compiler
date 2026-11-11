#!/usr/bin/env bash
# Production-style single process: FastAPI serves the built frontend (frontend/dist) and the API.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
PORT="${NOVA_PORT:-8000}"
[ -x "$NOVA_ROOT/compiler/build/novac" ] || build_compiler
ensure_venv
[ -d "$NOVA_ROOT/frontend/dist" ] || { ensure_node_modules; (cd "$NOVA_ROOT/frontend" && npm run build >/dev/null) || die "frontend build failed"; }
echo "  ${BOLD}NOVA${RESET} → http://localhost:$PORT"
cd "$NOVA_ROOT/backend" && exec .venv/bin/uvicorn app.main:app --host 127.0.0.1 --port "$PORT" --log-level warning
