#!/usr/bin/env bash
# Differential test: native x86-64 output must equal the VM output (stdout + runtime-error status) on examples and N random programs.
set -uo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; novac="$here/compiler/build/novac"
n="${1:-200}"; tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT; pass=0; fail=0; skip=0
check() {  # $1 = file
  "$novac" asm < "$1" > /dev/null 2>&1 || { skip=$((skip+1)); return; }
  "$here/scripts/nova-native.sh" "$1" -o "$tmp/a.out" || { echo "BUILD FAIL $1"; fail=$((fail+1)); return; }
  nat="$(timeout 10 "$tmp/a.out" 2>/dev/null)"; nrc=$?
  vm="$("$novac" compile --run --max-steps=50000000 < "$1" | python3 -c 'import json,sys;e=json.load(sys.stdin)["execution"]["baseline"];print("\n".join(e["output"]));sys.exit(0 if e["ok"] else 1)')"; vrc=$?
  if [ "$nat" == "$vm" ] && [ "$((nrc!=0))" == "$((vrc!=0))" ]; then pass=$((pass+1)); else echo "MISMATCH $1 (native rc=$nrc vm rc=$vrc)"; fail=$((fail+1)); fi
}
for f in "$here"/examples/*.nova; do check "$f"; done
for s in $(seq 1 "$n"); do "$novac" gen --seed=$s > "$tmp/g.nova"; check "$tmp/g.nova"; done
echo "native==vm: pass=$pass fail=$fail skipped(non-compiling)=$skip"; [ "$fail" -eq 0 ]
