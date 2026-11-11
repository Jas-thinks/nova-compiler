# Resume Descriptions (all claims are verifiable in the repository)

## Short (2 bullets)
* Built **NOVA**, a statically typed language and complete compiler in C++17 — table-driven canonical LR(1) parser (241 states, conflict-free), semantic analysis, three-address IR, six-pass optimizer, stack-VM bytecode backend — with a reference interpreter and a random-program differential fuzzer (1000/1000 programs identical across interpreter, VM, and optimized VM).
* Implemented CFG construction, dominators, iterative liveness / reaching-definitions analyses (with per-iteration history) and pruned SSA with φ-placement; shipped a FastAPI + React/TypeScript studio (Monaco, React Flow) exposing every compiler stage, an exact step-back debugger, and 150 automated tests across C++, Python and TypeScript.

## Longer (4 bullets)
* Designed and implemented a complete compiler toolchain in C++17 (~4,300 lines): hand-written lexer, canonical LR(1) generator (FIRST/FOLLOW, closure/goto, ACTION/GOTO with conflict detection), typed AST, scoped semantic analysis with edit-distance suggestions, IR, bytecode generator, and a tracing virtual machine.
* Wrote optimization passes — constant/copy propagation, folding, algebraic simplification, value-numbering CSE, CFG-reachability elimination and liveness-based DCE — each logging before/after IR with an explanation; guarded soundness cases (trapping division, signed-zero floats, CSE invalidation) with unit tests and differential fuzzing.
* Added advanced analyses: basic blocks/dominators/loops, iterative data-flow framework (liveness, reaching definitions) and SSA construction (dominance frontiers, pruned φ-placement, dominator-tree renaming) validated by property tests on randomly generated programs.
* Built the platform around it: FastAPI service running the compiler as a resource-limited subprocess (timeouts, output caps, rlimits), a compile-only C toolchain viewer (gcc/clang), and a React/TS IDE with AST/CFG graphs, optimization lab, debugger and live test runner.

## Do not claim
Production sandboxing, full C/C++/Python/Java/SQL support, native code generation, SSA-based optimization, or performance benchmarks — none exist.
