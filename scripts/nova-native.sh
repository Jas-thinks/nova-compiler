#!/usr/bin/env bash
# Compile a NOVA program to a native x86-64 executable:  scripts/nova-native.sh prog.nova [-o out] [--passes=all|none|...]
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="${1:?usage: nova-native.sh prog.nova [-o out] [--passes=...]}"; shift
out="${src%.nova}"; passes="--passes=all"; extra=""
while [ $# -gt 0 ]; do case "$1" in -o) out="$2"; shift 2;; --passes=*) passes="$1"; shift;; --no-regalloc) extra="--no-regalloc"; shift;; *) echo "unknown arg $1" >&2; exit 2;; esac; done
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
"$here/compiler/build/novac" asm "$passes" $extra < "$src" > "$tmp/prog.s"
gcc -O1 -o "$out" "$tmp/prog.s" "$here/compiler/runtime/nova_rt.c" -lm
