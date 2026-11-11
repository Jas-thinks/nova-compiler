# NOVA — Project Overview (for the Compiler Design evaluation)

## 1. Problem statement
Compilers are usually taught from static diagrams, while real compiler output (token streams, parser tables, IR, optimizer decisions, VM state) is hard to see. NOVA is a complete, working compiler for a small language *together with* an interactive studio that exposes the real data of every phase.

## 2. Motivation
Make each phase inspectable and verifiable: every number and tree shown in the UI is produced by the compiler, optimizations are checked against an independent interpreter, and the debugger controls the actual virtual machine.

## 3. Language design
Statically typed, `int`/`float`/`bool`, functions with recursion, nested scopes with shadowing, `if/else`, `while`, short-circuit logic, explicit casts. Unambiguous grammar (mandatory braces → no dangling else). See [LANGUAGE_SPEC.md](LANGUAGE_SPEC.md), [GRAMMAR.md](GRAMMAR.md).

## 4. Compiler architecture
Lexer → canonical LR(1) parser → AST → semantic analysis → three-address IR → optimizer → stack-machine bytecode → VM (about 3,900 lines of C++17, module per phase). See [COMPILER_ARCHITECTURE.md](COMPILER_ARCHITECTURE.md).

## 5. Algorithms implemented
* Scanning with longest match and position tracking.
* **Canonical LR(1)**: FIRST/FOLLOW fixed points, closure, goto, canonical collection (241 states), ACTION/GOTO construction with conflict detection, shift/reduce driver, syntax-error diagnostics from the expected-token set.
* Scoped symbol tables, bottom-up type inference, all-paths-return analysis, Levenshtein-based suggestions.
* AST → three-address code with short-circuit lowering.
* Optimizations: local + single-assignment constant/copy propagation, constant folding (with branch folding), algebraic simplification & strength reduction, local value numbering (CSE), CFG reachability, **iterative backward live-variable analysis** for DCE.
* Stack-VM code generation with label back-patching.

## 6. Data structures
Packed LR(1) items and kernel-keyed state map; table of `[state][symbol]` actions; parse-node tree; AST class hierarchy with `children()` traversal; scope tree with symbol vectors; structured IR instruction (`Operand`, `IRInstr`); basic blocks + successor lists; per-frame locals/operand stacks in the VM; snapshot vectors for the debugger.

## 7. Optimization techniques
See [IR_AND_OPTIMIZATION.md](IR_AND_OPTIMIZATION.md). Soundness details worth discussing: integer division is never folded or deleted when it may trap; float identities are limited to IEEE-safe ones; CSE entries are invalidated on reassignment; DCE uses true liveness, not "never used anywhere".

## 8. Runtime architecture
Tagged-value stack VM with call frames, no heap; limits on steps, call depth and output; runtime errors carry source lines. See [BYTECODE_SPEC.md](BYTECODE_SPEC.md).

## 9. Website visualizations
Dashboard · Playground (Monaco, pipeline, stage inspector, problems) · Lexical (token table + source highlighting) · Syntax (live parse-trace stepper, grammar, FIRST/FOLLOW, LR(1) item sets, ACTION/GOTO, parse tree) · AST explorer (React Flow, collapsible) · Semantic (scope tree, symbol tables, usage) · IR (per-instruction explanation) · Optimization Lab (pass toggles, diff, history, measured metrics) · Target code (source↔bytecode) · Debugger (breakpoints, step in/over/back, watch, call stack, operand stack, history) · Runtime memory · Compiler Tests (live runs) · Documentation · Learn Mode (stage-by-stage, filled with the real results of the current program).

## 10. Testing and limitations
91 C++ tests, 34 API tests, 25 frontend tests, plus differential fuzzing (reference interpreter vs VM vs optimized VM): 1000/1000 random programs identical in the last run. See [TESTING.md](TESTING.md).
Limitations: first-syntax-error-only (no recovery); no strings/arrays/heap; intra-procedural optimization only (no inlining/loop optimizations); the debugger replays a recorded trace (capped, flagged when truncated); the arithmetic primitives are shared by VM/interpreter/folder; no native code generation.

## 11. Future extensions
Panic-mode error recovery; arrays/strings with a heap and GC visualisation; SSA form with global value numbering and loop-invariant code motion; inlining; LLVM IR backend as an alternative target; LALR/Pager table-size comparison; language-server features from the diagnostics.
