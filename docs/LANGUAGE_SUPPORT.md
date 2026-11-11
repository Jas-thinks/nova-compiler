# Language Support

| Language | Status | What actually exists |
|---|---|---|
| **NOVA** | **Full** | Custom C++17 compiler: lexer, canonical LR(1) parser, AST, semantic analysis, IR, 6 optimizations, CFG / liveness / reaching definitions / SSA, bytecode generator, VM, debugger, reference interpreter, fuzzing. |
| **C** | **Viewer only** | `POST /api/explorer/c` shells out (argv only, compile-only, never executes) to the installed toolchain. With **clang**: AST dump, LLVM IR, assembly. With **gcc** (what this machine has; clang is *not* installed): GIMPLE dump, CFG dump, assembly. The adapter switches automatically when clang appears. Header allow-list (`stdio.h stdlib.h string.h stdint.h stdbool.h math.h stddef.h limits.h ctype.h`), 16 KiB limit, `-O0..3`. |
| C++ | Not implemented | — |
| SQL | Full (NOVA-SQL) | Own compiler and engine, see [SQL.md](SQL.md). |
| Python / Java | Not implemented | Listed as such in the UI and API. No fake integrations. |

## Adapter interface
`backend/app/explorer.py` — `languages()` describes each language (`id`, `status`, `outputs`, `toolchain`, `note`, `next_step`) and `compile_c()` returns `{tool, tool_path, opt_level, ok, outputs{name→text}, diagnostics, note}`. To add a language: implement one function that wraps a real tool through `sandbox.run_limited`, register it in `languages()`, and add an endpoint. `GET /api/explorer/languages` and `GET /api/capabilities` report what is genuinely available at runtime.

## Next integration step (Clang)
`sudo apt install clang` — no code change needed; the adapter then also returns `ast` (`-Xclang -ast-dump`) and `llvm-ir` (`-S -emit-llvm`). A future step would map LLVM IR / assembly lines back to C source lines via debug info.
