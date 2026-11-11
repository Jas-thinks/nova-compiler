#!/usr/bin/env bash
# Remove build artefacts. Pass --all to also remove the Python venv and node_modules.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
rm -rf "$NOVA_ROOT/compiler/build" "$NOVA_ROOT/frontend/dist" "$NOVA_ROOT/backend/.pytest_cache"
find "$NOVA_ROOT/backend" -name __pycache__ -type d -not -path '*/.venv/*' -prune -exec rm -rf {} + 2>/dev/null || true
if [ "${1:-}" = "--all" ]; then rm -rf "$NOVA_ROOT/backend/.venv" "$NOVA_ROOT/frontend/node_modules"; ok "removed venv and node_modules"; fi
ok "clean"
