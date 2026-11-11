#!/usr/bin/env bash
# Build the C++ engine with AddressSanitizer + UndefinedBehaviorSanitizer and run the unit tests and both fuzzers under it.
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
need g++ "Install a C++17 compiler."
DIR="$NOVA_ROOT/compiler/build-san"
info "Configuring sanitizer build (ASan + UBSan)…"
cmake -S "$NOVA_ROOT/compiler" -B "$DIR" -DCMAKE_BUILD_TYPE=Debug -DNOVA_SANITIZE=ON >/dev/null || die "configure failed"
cmake --build "$DIR" -j "$(nproc)" >/tmp/nova-san-build.log 2>&1 || { tail -30 /tmp/nova-san-build.log; die "sanitizer build failed"; }
export ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 UBSAN_OPTIONS=print_stacktrace=1
info "Unit tests under ASan+UBSan"
"$DIR/nova_tests" | tail -3 || die "unit tests failed under sanitizers"
info "Valid-program differential fuzz under sanitizers (300 programs)"
"$DIR/novac" fuzz --count=300 --seed=1 | python3 -c "import json,sys;j=json.load(sys.stdin);print('  passed',j['passed'],'failed',j['failed']);sys.exit(1 if j['failed'] else 0)" || die "fuzz failed under sanitizers"
info "Invalid-program mutation fuzz under sanitizers (600 programs)"
"$DIR/novac" fuzz --mode=invalid --count=600 --seed=1 | python3 -c "import json,sys;j=json.load(sys.stdin);print('  crashes',j['crashes'],'malformed',j['malformedDiagnostics']);sys.exit(1 if j['crashes'] or j['malformedDiagnostics'] or j['acceptedMismatches'] else 0)" || die "invalid fuzz failed under sanitizers"
ok "No sanitizer findings"
