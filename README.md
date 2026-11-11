# NOVA — Compiler Engineering Platform

> Two compilers, one native backend, one studio. **NOVA** (a statically typed language → LR(1) parser → SSA analyses → optimizer → VM / x86-64) and **NOVA-SQL** (SQL → logical plan → rule-based optimizer → bytecode VM → executor), all visualised from *real compiler output* and **verified against independent oracles** (reference interpreter, native code, SQLite).

```
fn sq(x: int) -> int { return x * x; }
fn hyp(a: int, b: int) -> int { return sq(a) + sq(b); }
fn main() { print(hyp(3, 4)); }                         // 25  — hyp and sq are inlined, then folded by the optimizer
```
```sql
SELECT u.name, SUM(o.amount) AS total
FROM users u, orders o WHERE u.id = o.uid AND u.age > 20     -- cross join + WHERE ...
GROUP BY u.name HAVING SUM(o.amount) > 5 ORDER BY total DESC LIMIT 5;   -- ... becomes filter-below-hash-join + top-N
```

## Highlights

| Area | What is built | How it is verified |
|---|---|---|
| **NOVA front end** | Hand-written lexer, **canonical LR(1)** parser generator (432 states, 0 conflicts, panic-mode error recovery), typed AST, scoped semantic analysis with "did you mean" | Grammar conflict test, 1000-program random fuzz, mutation fuzz of invalid programs (diagnostics must be well-formed, never crash) |
| **NOVA middle end** | 3-address IR, **CFG / dominators / liveness / reaching definitions / pruned SSA**, **7 optimization passes** incl. **interprocedural inlining** (call-graph recursion check, size limits), full per-rewrite history | Reference interpreter ≡ VM ≡ optimized VM on 1000s of random programs; injected-optimizer-bug test proves the checker can fail |
| **NOVA back end** | Stack **bytecode VM** with time-travel debugger; **native x86-64** (System V) with a **linear-scan register allocator**, heap arrays & strings, runtime traps | Native output ≡ VM output on every example + random programs (`scripts/native_diff.sh`); ASan/UBSan clean |
| **NOVA-SQL** | Lexer, recursive-descent parser, binder/type checker (**3-valued NULL logic**), logical plan, optimizer (**constant folding, predicate pushdown through joins, hash join, top-N**), expression bytecode VM, executor, `CASE`, joins/aggregates/`HAVING`/`DISTINCT`/`ORDER BY`/`LIMIT` | **Differential fuzzer vs SQLite** (thousands of random schemas+queries agree; it found a real bug), garbled-input fuzzing under sanitizers |
| **Assistant** | Compiler-**verified quick fixes**, iterative auto-fix, rule-based **runtime root-cause analysis** (no API key), optional Claude fixes re-checked by the compiler, LSP quick-fix code actions | A fix is "verified" only if applying it and re-compiling removes the error without adding new ones |
| **Tooling** | FastAPI service with sandboxed subprocess execution, **language server (LSP)**, React/TypeScript studio (19 pages, Monaco, React Flow), CI workflow | 289 automated tests + fuzzers (below) |

## Quick start
Requirements: g++ (C++17), CMake ≥ 3.16, Python ≥ 3.11 (with `venv`), Node ≥ 18, gcc (for the native backend).

```bash
./scripts/dev.sh        # builds the compiler, installs deps, starts backend + website, prints the URL
./scripts/serve.sh      # single-process production mode (FastAPI serves frontend/dist) on :8000
./scripts/test.sh       # every suite: CTest, 1000-program fuzz, native-vs-VM, SQL-vs-SQLite, pytest, tsc + Vitest
./scripts/sanitize.sh   # ASan + UBSan build: unit tests, fuzzers, SQL differential + mutation fuzz
./scripts/bench.sh      # native vs CPython, register allocation on/off, inlining on/off
```
CLI: `novac compile --run < prog.nova | jq .execution` · `scripts/nova-native.sh prog.nova -o prog` · `novac asm < prog.nova` · `novac sql < script.sql` · `scripts/nova-lsp.sh` (LSP on stdio).

## Measured results (this machine, best of 5)
| Benchmark | Result |
|---|---|
| `fib(35)` native x86-64 vs CPython | **~12× faster** (≈55 ms vs ≈650 ms) |
| prime sieve native vs CPython | **~12× faster** |
| Linear-scan register allocation | `fib(35)` **1.3×** faster; **no gain** on the sieve (bound by `idiv`) — reported honestly |
| Interprocedural inlining (hot loop calling small helpers) | **2.2× faster** native code (104 ms → 48 ms), identical output |
| Optimizer on 2000 random programs | IR instructions **305 047 → 111 822**, VM steps **1.86 M → 0.76 M**, 0 mismatches |
| NOVA-SQL vs SQLite | 4910/4910 random queries agree (earlier run), 2460/2460 after adding `CASE`; 0 failures |

## Test status (last full run)
**CI: green on GitHub Actions** (test job + ASan/UBSan job). Its first run failed on a real portability bug — a missing `#include <algorithm>` that only the hosted compiler exposed — fixed in the next commit.

**184** C++ unit tests · **73** backend tests · **32** frontend tests · 1000/1000 random programs identical across 3 engines · 313/313 native-vs-VM programs identical · SQL differential 0 failures · sanitizers: 0 findings, 0 crashes on 600 garbled SQL scripts + 600 mutated NOVA programs.

## Architecture
```
            NOVA source                                        SQL script
                │                                                  │
   lexer ──► canonical LR(1) parser ──► typed AST         lexer ──► recursive-descent parser ──► AST
                │                                                  │
        semantic analysis (scopes, types)               binder / type checker (3-valued logic)
                │                                                  │
          3-address IR ──► [inlining] ──► CFG · SSA · dataflow     logical plan ──► rule-based optimizer
                │              + 6 intra-procedural passes                │   (fold · pushdown · hash join · top-N)
        ┌───────┴────────┐                                       expression bytecode (SQL-VM)
   stack bytecode   x86-64 asm (linear-scan regalloc)                     │
   VM + debugger    + C runtime (traps, strings, heap)               executor ──► rows + per-operator stats
        │                │
        └── differential tests vs reference interpreter ──┘          └── differential tests vs SQLite
```

## Layout
```
compiler/   C++17: lexer, parser (LR(1) generator), ast, semantic, ir, optimizer, analysis, codegen (VM + x86-64), vm, sql/, tests
backend/    FastAPI app (compile, optimize, debug sessions, asm, sql, suggest/autofix, AI, LSP) + pytest suite
frontend/   React + TypeScript + Vite + Tailwind + Monaco + React Flow (19 pages) + Vitest
examples/   NOVA programs (valid, error, runtime-error) and SQL scripts
benchmarks/ fib, primes, inlining workloads (+ CPython equivalents)
editors/    VS Code client for the language server
report/     the full LaTeX project report (PDF)
docs/       specs, architecture, SQL, testing, API, demo script
scripts/    dev / build / serve / test / sanitize / bench / native build / SQL fuzzer / LSP launcher
```

## Documentation
[Project report (PDF)](report/nova_report.pdf) · [ARCHITECTURE](docs/ARCHITECTURE.md) · [COMPILER_PIPELINE](docs/COMPILER_PIPELINE.md) · [SQL](docs/SQL.md) · [ADVANCED_ANALYSIS](docs/ADVANCED_ANALYSIS.md) · [LANGUAGE_SPEC](docs/LANGUAGE_SPEC.md) · [GRAMMAR](docs/GRAMMAR.md) · [PARSING_ALGORITHM](docs/PARSING_ALGORITHM.md) · [IR_AND_OPTIMIZATION](docs/IR_AND_OPTIMIZATION.md) · [BYTECODE_SPEC](docs/BYTECODE_SPEC.md) · [API_DOCUMENTATION](docs/API_DOCUMENTATION.md) · [TESTING](docs/TESTING.md) · [DEMO_SCRIPT](docs/DEMO_SCRIPT.md)

## Honest limitations
No garbage collector (allocations live until exit and are capped) · strings are immutable byte strings · arrays are single-level `int[]/float[]/bool[]` · floats are not register-allocated (SysV has no callee-saved xmm) · inlining skips recursive and large callees · NOVA-SQL has no subqueries, `UNION`, window functions or indexes and keeps everything in memory · Claude-based fixes were only exercised against a scripted model (no API key in the test environment) · the VS Code client in `editors/vscode` has not been run in a real VS Code session.

## Safety
The backend never uses a shell; external tools run with argv only, a wall-clock timeout, an output cap, CPU/memory rlimits (reported by `/api/health` when unavailable), and source/step/depth limits; SQL has row, join-pair and result caps. **There is no container/namespace/seccomp isolation** — run the service inside a container or VM before exposing it to untrusted users.
