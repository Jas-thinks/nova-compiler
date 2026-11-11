#!/usr/bin/env bash
# Wall-clock benchmark: NOVA native (x86-64) vs CPython. Usage: scripts/bench.sh
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; novac="$here/compiler/build/novac"; tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
t() { local s=$(date +%s.%N); "$@" > /dev/null; printf "%.2f" "$(echo "$(date +%s.%N) - $s" | bc)"; }
printf "%-10s %10s %10s %9s\n" benchmark native_s cpython_s speedup
for b in fib35 primes; do
  "$here/scripts/nova-native.sh" "$here/benchmarks/$b.nova" -o "$tmp/$b"
  nat=$(t "$tmp/$b")
  py=$(t python3 "$here/benchmarks/$b.py")
  [ "$("$tmp/$b")" == "$(python3 "$here/benchmarks/$b.py")" ] || { echo "OUTPUT MISMATCH $b" >&2; exit 1; }
  printf "%-10s %10s %10s %8sx\n" "$b" "$nat" "$py" "$(echo "scale=1; $py / $nat" | bc)"
done
