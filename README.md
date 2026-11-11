# NOVA — Interactive Compiler Engineering Platform

A real compiler for a small statically typed language (C++17), a FastAPI backend, and a React/TypeScript studio that visualises **every compiler phase from real compiler output** — tokens, a canonical LR(1) parse trace, AST, symbol tables, three-address IR, optimization history, NOVA VM bytecode, and a debugger that steps (even backwards) through the actual VM.

```
fn fact(n: int) -> int {
    if (n <= 1) { return 1; }
    return n * fact(n - 1);
}
fn main() { print(fact(5)); }     // 120
```

## Quick start
Requirements: g++ (C++17), CMake ≥ 3.16, Python ≥ 3.11 (with `venv`), Node ≥ 18. Optional: clang or gcc for the C viewer.

```bash
./scripts/dev.sh        # builds the compiler, installs deps, starts backend + website, prints the URL
```
Open the printed `http://localhost:5173`. If ports are busy: `NOVA_API_PORT=8765 NOVA_WEB_PORT=5199 ./scripts/dev.sh`.

| Script | Purpose |
|---|---|
| `scripts/dev.sh` | development: backend (uvicorn) + Vite dev server |
| `scripts/build.sh` | build compiler, backend venv, production frontend bundle |
| `scripts/serve.sh` | single-process production mode (FastAPI serves `frontend/dist`) |
| `scripts/test.sh` | all test suites (CTest, 1000-program fuzz, pytest, tsc + Vitest) |
| `scripts/clean.sh [--all]` | remove build artefacts (and venv/node_modules) |

CLI without the website: `compiler/build/novac compile --run < examples/02_factorial.nova | jq .execution` (also `novac grammar|fuzz|gen|isa|passes`).

## New: language server, arrays & strings, native backend, verified AI assist, CI
* **Language server (LSP over stdio)** — `scripts/nova-lsp.sh` / `backend/app/lsp.py`: diagnostics with exact ranges, hover (types, signatures), go-to-definition, find-references, scope-aware completion, document symbols; all derived from the real compiler's output (no second parser). 11 protocol tests plus a real stdio round-trip. A minimal VS Code client is in `editors/vscode/` (**not yet exercised in a real VS Code session**).
* **Arrays and strings**: `int[]/float[]/bool[]` (literals, `new T[n]`, indexing, bounds checks, pass-by-reference), immutable `string` (`+`, `==`, `len`, `print`). Implemented end to end — lexer, LR(1) grammar (432 states, no conflicts), type checker, IR intrinsics, optimizer-safe, VM heap, reference interpreter, x86-64 runtime — and covered by the random-program generator, so the 1000-program differential fuzz and the native-vs-VM check exercise them. See [docs/GRAMMAR.md](docs/GRAMMAR.md). No garbage collector yet.
* **Native x86-64 backend** (`novac asm`, `scripts/nova-native.sh prog.nova -o prog`): IR → System V assembly → real executable via gcc, with a small C runtime (`compiler/runtime/nova_rt.c`) that traps `/0` and stack overflow like the VM. `scripts/native_diff.sh` checks native output == VM output on every example and N random programs (410/410 in the last run). `scripts/bench.sh` (best of 5, same machine): ~11× faster than CPython on both `fib(35)` and a prime sieve. A **linear-scan register allocator** (liveness-based intervals, callee-saved `%rbx,%r12-%r15`, furthest-end spilling; floats stay on the stack) speeds up call-heavy `fib(35)` by ~1.27× and does nothing for the sieve, which is bound by the integer-divide instruction — measured, not assumed.
* **AI fix + debug, verified by the compiler** (`POST /api/ai/fix`, `/api/ai/debug`, needs `ANTHROPIC_API_KEY`): the model proposes a corrected program; the C++ compiler re-compiles it and only a candidate that passes is returned (up to 3 retries fed with the new diagnostics). The debugger endpoint explains runtime failures from the VM's recorded trace. Tested offline with a scripted model (`backend/tests/test_ai_assist.py`). Frontend UI for these endpoints is not built yet.
* **CI** (`.github/workflows/ci.yml`): full suite plus the ASan/UBSan build on every push.

## New in the previous release
**CFG & Data-flow page** (basic blocks, edges, dominators, loops, liveness and reaching-definitions with iteration history, pruned SSA with φ nodes) · **Compiler Explorer** (real gcc/clang compile-only viewer for C; other languages explicitly *not implemented*) · **live capability and test dashboard** · hardened subprocess sandbox (`backend/app/sandbox.py`: timeout, output cap, rlimits — *no* container isolation) · pass-impact table in the Optimization Lab. See [docs/ADVANCED_ANALYSIS.md](docs/ADVANCED_ANALYSIS.md), [docs/LANGUAGE_SUPPORT.md](docs/LANGUAGE_SUPPORT.md), [docs/FINAL_VERIFICATION.md](docs/FINAL_VERIFICATION.md).

## What is implemented
* **Lexer**, **canonical LR(1) parser** (items, closure, goto, ACTION/GOTO, shift/reduce/accept/error, parse trace), typed **AST**, **semantic analysis** (scopes, symbol tables, type/call/return checks, warnings), **3-address IR**, **6 optimization passes** (constant propagation & folding, algebraic simplification, CSE, unreachable-code elimination, liveness-based DCE) with a full transformation log, **bytecode generator + VM** (calls, recursion, runtime errors, state snapshots), a separate **reference interpreter** for differential testing and a **random program generator**.
* **Website:** 13 sections + Learn Mode; Monaco editor with live diagnostics, AST graph (React Flow), optimization lab with per-pass toggles and measured before/after metrics, debugger with breakpoints/step-in/over/back, runtime frame inspector, live test runner.
* **Diagnostics:** exact line/column ranges, category, explanation, suggested fix, edit-distance spelling suggestions. Optional AI explanation (`ANTHROPIC_API_KEY`) only rephrases an existing diagnostic; everything works without it.

## Layout
```
compiler/   C++17 engine (lexer, parser, ast, semantic, ir, optimizer, codegen, vm, diagnostics) + tests
backend/    FastAPI app (app/) + pytest suite
frontend/   React + TS + Vite + Tailwind + Monaco + React Flow
examples/   shipped NOVA programs (valid, error, runtime-error)
docs/       specs, architecture, testing, API, demo script, project overview
scripts/    dev / build / serve / test / clean
```

## Test status (last run)
169 C++ · 53 backend · 25 frontend tests passing; 1000/1000 random programs (now including arrays and strings) identical across reference interpreter / VM / optimized VM; native x86-64 output equal to the VM on all examples + random programs; ASan/UBSan clean (`./scripts/test.sh`, `./scripts/sanitize.sh`).

## Documentation
[ARCHITECTURE](docs/ARCHITECTURE.md) · [COMPILER_PIPELINE](docs/COMPILER_PIPELINE.md) · [ADVANCED_ANALYSIS](docs/ADVANCED_ANALYSIS.md) · [LANGUAGE_SUPPORT](docs/LANGUAGE_SUPPORT.md) · [FINAL_VERIFICATION](docs/FINAL_VERIFICATION.md) · [RESUME_DESCRIPTION](docs/RESUME_DESCRIPTION.md) · [PROJECT_OVERVIEW](docs/PROJECT_OVERVIEW.md) (professor-facing) · [LANGUAGE_SPEC](docs/LANGUAGE_SPEC.md) · [GRAMMAR](docs/GRAMMAR.md) · [COMPILER_ARCHITECTURE](docs/COMPILER_ARCHITECTURE.md) · [PARSING_ALGORITHM](docs/PARSING_ALGORITHM.md) · [IR_AND_OPTIMIZATION](docs/IR_AND_OPTIMIZATION.md) · [BYTECODE_SPEC](docs/BYTECODE_SPEC.md) · [API_DOCUMENTATION](docs/API_DOCUMENTATION.md) · [TESTING](docs/TESTING.md) · [DEMO_SCRIPT](docs/DEMO_SCRIPT.md)

## Honest limitations
No garbage collector (heap is bump-allocated and capped) · strings are immutable and byte-oriented · only int/float/bool arrays (no nesting) · bytecode targets the NOVA VM, not native code · debugger replays a recorded, capped trace · optimizations are intra-procedural.

## Safety
The backend never uses a shell; external tools run with argv only, a wall-clock timeout, an output cap, CPU/memory rlimits (reported by `/api/health` when unavailable), and source/step/depth limits. **There is no container/namespace/seccomp isolation** — run the service inside a container or VM before exposing it to untrusted users.
