# Testing

Run everything with `./scripts/test.sh` (builds, then runs all suites; non-zero exit on any failure). The website's **Compiler Tests** page runs the C++ unit tests and the fuzzer *live* through `POST /api/tests/run`; nothing is pre-recorded.

| Suite | Tool | Location | What it covers |
|---|---|---|---|
| C++ unit/integration | in-tree framework registered with CTest | `compiler/tests/test_compiler.cpp` | lexer (positions, literals, errors), parser (LR(1) conflict-freeness, FIRST/FOLLOW, trace consistency, error kinds), AST (precedence, locations, traversal), semantic (each error code, shadowing, warnings, edit distance), IR (exact 3-address output), optimizer (each pass, soundness cases such as CSE invalidation, trapping division, signed zero), VM (arithmetic, calls, short-circuit, runtime errors, traces), end-to-end (all shipped examples), differential |
| Differential fuzzing | `novac fuzz`, also inside the unit suite | `compiler/src/testing/fuzz.cpp` | random valid programs: reference interpreter vs unoptimized VM vs optimized VM; output + runtime error must match |
| CFG / data-flow / SSA | in the C++ suite (`cfg`, `dataflow`, `ssa`, `analysis`) | `compiler/tests/test_compiler.cpp` | block/edge construction, loops & dominators, liveness and reaching-definition fixed-point equations, φ placement, SSA validity on 150 random programs |
| Backend | pytest + FastAPI TestClient against the **real** `novac` | `backend/tests/test_api.py` | every endpoint, validation, shell-injection safety, step-limit protection, debugger stepping/back/breakpoints/step-over/seek, real test-runner endpoint |
| Frontend | Vitest + Testing Library, `tsc --noEmit` | `frontend/src/**/*.test.ts(x)` | tree layout, line diff, formatting/markers, API client error handling, UI components |

> The framework is a ~40-line header (`tests/test_framework.hpp`) rather than GoogleTest so the project builds offline; tests register with CTest and can emit JSON (`nova_tests --json`).

## Generated-program testing
`generateRandomProgram(seed)` builds well-typed programs: helper functions (call-before-definition order, so no recursion), `let`/assign/print/if-else, bounded `while` loops (dedicated counters), int/float/bool expressions with literals chosen to trigger folding, casts, short-circuit operators, and occasional runtime-error divisions. Programs that hit the step limit are counted as *inconclusive* rather than passed. Failures are reported with seed and full source so they replay exactly (`novac gen --seed=N`).

## Interpreting the numbers
`passed`, `failed`, `inconclusive`, `generatorFailures` and the size/step reductions all come from the run itself. Reference results on the development machine (not guaranteed to be current — re-run): 91 C++ tests (including CFG / data-flow / SSA suites), 34 backend tests (including sandbox and explorer), 25 frontend tests passing; 1000/1000 fuzz programs identical with all passes enabled. See FINAL_VERIFICATION.md.

## Known gaps
* The reference interpreter and the VM share the primitive arithmetic helper (`value.hpp`).
* No fuzzing of *invalid* programs (the diagnostics are covered by targeted unit tests only).
* No browser end-to-end test suite is committed; the UI was smoke-tested manually with a headless browser.
* The parser has no error recovery, so only the first syntax error is reported.
