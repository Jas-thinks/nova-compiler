#!/usr/bin/env bash
# Run every test suite: C++ (CTest), backend (pytest, against the real compiler), frontend (Vitest).
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
FAIL=0
build_compiler
info "C++ unit tests (CTest)"
(cd "$NOVA_ROOT/compiler/build" && ctest --output-on-failure) || FAIL=1
info "Differential fuzzing: 1000 random programs, all optimization passes"
"$NOVA_ROOT/compiler/build/novac" fuzz --count=1000 --seed=1 | python3 -c "
import json,sys
j=json.load(sys.stdin)
print(f\"  programs={j['requested']} identical={j['passed']} mismatched={j['failed']} inconclusive={j['inconclusive']} generator_failures={j['generatorFailures']}\")
sys.exit(1 if j['failed'] or j['generatorFailures'] else 0)" || FAIL=1
if command -v gcc >/dev/null 2>&1; then
  info "Native x86-64 backend vs VM (examples + 100 random programs)"
  "$NOVA_ROOT/scripts/native_diff.sh" 100 || FAIL=1
fi
info "NOVA-SQL vs SQLite differential fuzz (300 random schemas/queries)"
python3 "$NOVA_ROOT/scripts/sql_fuzz.py" --count 300 || FAIL=1
ensure_venv
info "Backend tests (pytest)"
(cd "$NOVA_ROOT/backend" && .venv/bin/python -m pytest -q tests) || FAIL=1
ensure_node_modules
info "Frontend type-check + tests (tsc, Vitest)"
(cd "$NOVA_ROOT/frontend" && npx tsc --noEmit && npx vitest run) || FAIL=1
echo
if [ "$FAIL" = 0 ]; then ok "ALL TEST SUITES PASSED"; else die "some test suites FAILED (see output above)"; fi
