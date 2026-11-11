# Compiler Pipeline

| # | Stage | Input → Output | Implementation | Real timing shown in UI |
|---|---|---|---|---|
| 1 | Lexical analysis | source → tokens (+line/col) | `lexer/lexer.cpp` | ✓ |
| 2 | Syntax analysis | tokens → parse tree + shift/reduce trace | canonical LR(1): `parser/{grammar,lr1,parser}.cpp` (241 states, 0 conflicts) | ✓ |
| 3 | AST construction | parse tree → typed AST | `ast/ast.cpp` | ✓ |
| 4 | Semantic analysis | AST → scopes, symbols, types, diagnostics | `semantic/semantic.cpp` | ✓ |
| 5 | IR generation | AST → three-address code | `ir/irgen.cpp` | ✓ |
| 6 | Optimization | IR → IR + transformation log | `optimizer/optimizer.cpp` (6 passes, fixed point) | ✓ |
| 7 | Code generation | IR → NOVA VM bytecode (+ source map) | `codegen/codegen.cpp` | ✓ |
| — | Analysis (side branch) | IR → CFG, dominators, liveness, reaching defs, SSA | `analysis/analysis.cpp` (computed for original *and* optimized IR) | part of response |
| — | Execution | bytecode → output, steps, snapshots | `vm/vm.cpp`; oracle: `vm/interp.cpp` | ✓ |

A failing stage stops later stages and they are reported `skipped`. All stages return structured objects serialised to JSON at the edge (`pipeline.cpp`). See [PARSING_ALGORITHM.md](PARSING_ALGORITHM.md), [IR_AND_OPTIMIZATION.md](IR_AND_OPTIMIZATION.md), [BYTECODE_SPEC.md](BYTECODE_SPEC.md), [ADVANCED_ANALYSIS.md](ADVANCED_ANALYSIS.md).
