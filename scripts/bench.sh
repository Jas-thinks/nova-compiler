#!/usr/bin/env bash
# Wall-clock benchmark (best of 5): NOVA native without / with the linear-scan register allocator vs CPython. Usage: scripts/bench.sh
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
best() { local b=999999; for _ in 1 2 3 4 5; do local s=$(date +%s%N); "$@" > /dev/null; local e=$(( ($(date +%s%N) - s) / 1000000 )); [ "$e" -lt "$b" ] && b=$e; done; echo "$b"; }
printf "%-10s %12s %12s %12s %10s %10s\n" benchmark noregalloc_ms regalloc_ms cpython_ms vs_noreg vs_cpython
for b in fib35 primes; do
  "$here/scripts/nova-native.sh" "$here/benchmarks/$b.nova" -o "$tmp/$b.noreg" --no-regalloc
  "$here/scripts/nova-native.sh" "$here/benchmarks/$b.nova" -o "$tmp/$b.reg"
  [ "$("$tmp/$b.reg")" == "$(python3 "$here/benchmarks/$b.py")" ] && [ "$("$tmp/$b.noreg")" == "$("$tmp/$b.reg")" ] || { echo "OUTPUT MISMATCH $b" >&2; exit 1; }
  n=$(best "$tmp/$b.noreg"); r=$(best "$tmp/$b.reg"); p=$(best python3 "$here/benchmarks/$b.py")
  printf "%-10s %12s %12s %12s %9sx %9sx\n" "$b" "$n" "$r" "$p" "$(echo "scale=2; $n / $r" | bc)" "$(echo "scale=1; $p / $r" | bc)"
done

# ---- interprocedural inlining: native code with and without the inlining pass (same machine, best of 5)
echo
printf "%-10s %14s %12s %12s %10s\n" benchmark noinline_ms inline_ms cpython_ms speedup
NOINL="constant_propagation,constant_folding,algebraic,cse,unreachable,dce"
"$here/scripts/nova-native.sh" "$here/benchmarks/inline.nova" -o "$tmp/inl.off" --passes=$NOINL
"$here/scripts/nova-native.sh" "$here/benchmarks/inline.nova" -o "$tmp/inl.on"
[ "$("$tmp/inl.on")" == "$("$tmp/inl.off")" ] || { echo "OUTPUT MISMATCH inline" >&2; exit 1; }
n=$(best "$tmp/inl.off"); r=$(best "$tmp/inl.on")
printf "%-10s %14s %12s %12s %9sx\n" inline "$n" "$r" "n/a" "$(echo "scale=2; $n / $r" | bc)"
