# Compiler Architecture

```
 source ─► Lexer ─► tokens ─► LR(1) Parser ─► parse tree ─► AST builder ─► AST
                                                                          │
   bytecode ◄─ Codegen ◄─ optimized IR ◄─ Optimizer ◄─ IR ◄─ IR gen ◄─ Semantic analysis
       │
       └─► VM  (also: reference AST interpreter for differential testing)
```

## Layers
| Layer | Tech | Responsibility |
|---|---|---|
| `compiler/` | C++17, CMake | The whole compiler + VM. Static library `nova_core`, CLI `novac` (JSON on stdout), test binary `nova_tests`. **No dependency on the web layers.** |
| `backend/` | FastAPI, Pydantic | Validates requests, runs `novac` with an argv vector (no shell), CPU/memory/time limits; keeps debugger sessions. Contains no compiler logic. |
| `frontend/` | React, TS, Vite, Tailwind, Monaco, React Flow | Renders the compiler's JSON. The debugger is a cursor over VM snapshots. |

## C++ modules (`compiler/src/…`)
| Dir | Files | Role |
|---|---|---|
| `lexer/` | `lexer.cpp` | Scanner → positioned tokens, lexical diagnostics |
| `parser/` | `grammar.cpp`, `lr1.cpp`, `parser.cpp` | Grammar, FIRST/FOLLOW, canonical LR(1) tables, shift/reduce driver, syntax diagnostics |
| `ast/` | `ast.cpp` | Typed node classes, parse-tree → AST, JSON, traversal (`walk`) |
| `semantic/` | `semantic.cpp` | Scopes, symbol tables, type checking, call/return validation, warnings |
| `ir/` | `ir.cpp`, `irgen.cpp` | IR data structures, text/JSON, AST → IR |
| `optimizer/` | `optimizer.cpp` | Six passes + history |
| `codegen/` | `codegen.cpp` | IR → bytecode, disassembly, source map |
| `vm/` | `vm.cpp`, `interp.cpp` | Bytecode VM with tracing; reference interpreter |
| `diagnostics/` | `diagnostics.cpp` | `Diagnostic`, edit distance, `closestMatch` |
| `pipeline/`, `tools/`, `testing/` | | Orchestration + JSON, CLI, random program generator / fuzz driver |

`value.hpp` is the single definition of NOVA arithmetic (wrapping ints, float ops, division traps) used by the folder, VM and interpreter.

## Data flow and design decisions
* **Stage objects, not strings.** Tokens, parse nodes, AST nodes, IR instructions and bytecode are all structured objects; JSON is produced at the edge (`resultToJson`).
* **Stage status.** The pipeline records `ok | error | skipped` and a real wall-clock duration per stage. A failing stage stops later ones; nothing is silently skipped.
* **Semantic info lives on the AST** (`Expr::type`, `VarRef::sym`, `Call::callee`), so IR generation never re-derives names or types.
* **Unique IR names.** Shadowed variables get distinct IR names (`x`, `x.1`) so the optimizer can treat names as storage locations.
* **Two bytecode programs** are generated per compile (unoptimized and optimized) so the lab and the tests can compare behaviour.
* **Debugging by recording.** The VM runs once with snapshots; the backend serves a cursor. This makes stepping *backwards* exact. Trade-off: a debug run is bounded by the trace cap (default 20,000 snapshots, flagged when truncated).
* **Safety.** Source size ≤ 64 KiB; VM instruction, call-depth and output limits; subprocess timeout + `RLIMIT_CPU/AS`; unknown optimization-pass ids and oversized numbers are rejected by Pydantic.

## Extension points
Add an optimization: implement a `bool pass(Ctx&)`, add a flag in `OptConfig`, register it in `optimizationPasses()`. Add an operator: extend `BinOp`/`value.hpp`, the grammar text, semantic typing, and `opFromBin`.
