#!/usr/bin/env bash
# Build everything: C++ compiler + tests, backend venv, production frontend bundle.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
check_toolchain
build_compiler
ensure_venv
ensure_node_modules
info "Building frontend…"
(cd "$NOVA_ROOT/frontend" && npm run build >/tmp/nova-frontend-build.log 2>&1) || { tail -30 /tmp/nova-frontend-build.log; die "frontend build failed."; }
ok "frontend built: frontend/dist"
ok "Build complete. Start with ./scripts/dev.sh (development) or ./scripts/serve.sh (single-process production)."
