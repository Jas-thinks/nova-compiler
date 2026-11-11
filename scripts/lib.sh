#!/usr/bin/env bash
# Shared helpers for NOVA scripts.
set -euo pipefail
NOVA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export NOVA_ROOT
RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; BLUE=$'\033[34m'; BOLD=$'\033[1m'; RESET=$'\033[0m'
info() { echo "${BLUE}==>${RESET} $*"; }
ok()   { echo "${GREEN}✓${RESET} $*"; }
warn() { echo "${YELLOW}!${RESET} $*"; }
die()  { echo "${RED}✗ error:${RESET} $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "'$1' is required but was not found. $2"; }

check_toolchain() {
  need cmake "Install CMake >= 3.16 (e.g. sudo apt install cmake)."
  need g++ "Install a C++17 compiler (e.g. sudo apt install build-essential)."
  need python3 "Install Python 3.11+."
  python3 - <<'PY' || die "Python 3.11 or newer is required."
import sys; sys.exit(0 if sys.version_info >= (3, 11) else 1)
PY
  need node "Install Node.js >= 18."
  need npm "Install npm."
}

build_compiler() {
  local dir="$NOVA_ROOT/compiler/build"
  info "Building C++ compiler (Release)…"
  cmake -S "$NOVA_ROOT/compiler" -B "$dir" -DCMAKE_BUILD_TYPE=Release >/dev/null || die "CMake configuration failed."
  cmake --build "$dir" -j "$(nproc 2>/dev/null || echo 4)" >/tmp/nova-build.log 2>&1 || { tail -30 /tmp/nova-build.log; die "C++ build failed (full log: /tmp/nova-build.log)."; }
  ok "compiler built: $dir/novac"
}

ensure_venv() {
  local venv="$NOVA_ROOT/backend/.venv"
  if [ ! -x "$venv/bin/python" ]; then
    info "Creating Python virtualenv…"
    python3 -m venv "$venv" || die "Could not create a virtualenv (is python3-venv installed?)."
  fi
  if ! "$venv/bin/python" -c "import fastapi, uvicorn, pydantic, httpx" >/dev/null 2>&1; then
    info "Installing backend dependencies…"
    "$venv/bin/pip" install -q -r "$NOVA_ROOT/backend/requirements.txt" || die "pip install failed."
  fi
}

ensure_node_modules() {
  if [ ! -d "$NOVA_ROOT/frontend/node_modules" ]; then
    info "Installing frontend dependencies (npm install)…"
    (cd "$NOVA_ROOT/frontend" && npm install --no-audit --no-fund) || die "npm install failed."
  fi
}
