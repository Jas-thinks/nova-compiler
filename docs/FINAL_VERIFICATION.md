# Final Verification

Run on the development machine (Ubuntu, g++ 13.3, gcc 13.3 present, **clang absent**, Python 3.12, Node 25) with `./scripts/test.sh` and `./scripts/build.sh`.

| Check | Command | Result |
|---|---|---|
| Baseline before the sprint | ctest / pytest / vitest | 77 C++ / 24 backend / 20 frontend — all passing |
| C++ tests (CTest) | `compiler/build/nova_tests` | **91 passed, 0 failed** (14 new: CFG 5, data-flow 4, SSA 4, JSON 1) |
| Differential fuzzing | `novac fuzz --count=1000 --seed=1` | **1000 identical, 0 mismatched, 0 inconclusive, 0 generator failures** |
| Backend tests (pytest, real compiler) | `pytest backend/tests` | **34 passed** (10 new: analysis JSON, capabilities, explorer, sandbox timeout/output cap/env/shell, last-test record) |
| Frontend tests + types | `tsc --noEmit`, `vitest run` | **25 passed** (5 new: CFG layout, data-flow view), 0 type errors |
| Production build | `npm run build` | succeeds |
| Browser smoke test (headless Chrome via Playwright) | manual script | CFG, liveness, SSA, explorer, dashboard, debugger, optimizer pages render real data, no page errors |

Not verified: Clang adapter path (clang not installed — only the GCC path ran); behaviour on non-Linux systems; load/concurrency behaviour; browser E2E suite is not committed.

Reproduce: `./scripts/test.sh` (exits non-zero on any failure).
