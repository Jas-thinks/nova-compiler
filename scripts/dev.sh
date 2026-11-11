#!/usr/bin/env bash
# One-command development startup: builds the compiler if needed, installs dependencies,
# starts the FastAPI backend and the Vite dev server, and prints the website URL.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
API_PORT="${NOVA_API_PORT:-8000}"
WEB_PORT="${NOVA_WEB_PORT:-5173}"

check_toolchain
port_busy() { (exec 3<>"/dev/tcp/127.0.0.1/$1") >/dev/null 2>&1; }
port_busy "$API_PORT" && die "port $API_PORT is already in use (set NOVA_API_PORT to use another)."
port_busy "$WEB_PORT" && die "port $WEB_PORT is already in use (set NOVA_WEB_PORT to use another)."

# rebuild when sources are newer than the binary
BIN="$NOVA_ROOT/compiler/build/novac"
if [ ! -x "$BIN" ] || [ -n "$(find "$NOVA_ROOT/compiler/src" "$NOVA_ROOT/compiler/include" "$NOVA_ROOT/compiler/CMakeLists.txt" -newer "$BIN" -print -quit 2>/dev/null)" ]; then
  build_compiler
else
  ok "compiler is up to date"
fi
ensure_venv
ensure_node_modules

PIDS=()
cleanup() { echo; info "Shutting down…"; for p in "${PIDS[@]:-}"; do kill "$p" 2>/dev/null || true; done; wait 2>/dev/null || true; }
trap cleanup EXIT INT TERM

info "Starting backend on http://127.0.0.1:$API_PORT"
(cd "$NOVA_ROOT/backend" && NOVA_SERVE_FRONTEND=0 exec .venv/bin/uvicorn app.main:app --host 127.0.0.1 --port "$API_PORT" --log-level warning) &
PIDS+=($!)

for _ in $(seq 1 50); do
  curl -sf "http://127.0.0.1:$API_PORT/api/health" >/dev/null 2>&1 && break
  kill -0 "${PIDS[0]}" 2>/dev/null || die "backend failed to start."
  sleep 0.2
done
curl -sf "http://127.0.0.1:$API_PORT/api/health" >/dev/null 2>&1 || die "backend did not become healthy."
ok "backend healthy"

info "Starting frontend dev server…"
(cd "$NOVA_ROOT/frontend" && NOVA_API="http://127.0.0.1:$API_PORT" exec npx vite --host 127.0.0.1 --port "$WEB_PORT" --strictPort --logLevel warn) &
PIDS+=($!)
sleep 1.5
kill -0 "${PIDS[1]}" 2>/dev/null || die "frontend dev server failed to start."

echo
echo "  ${BOLD}NOVA is running${RESET}"
echo "  ${GREEN}Website:${RESET}  http://localhost:$WEB_PORT"
echo "  ${GREEN}API docs:${RESET} http://localhost:$API_PORT/docs"
echo "  Press Ctrl+C to stop."
wait
