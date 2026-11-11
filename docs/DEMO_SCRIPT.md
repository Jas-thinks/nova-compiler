# 5-Minute Demonstration Script

**Setup (before the audience arrives):** `./scripts/dev.sh`, open the printed URL. If port 8000 is busy: `NOVA_API_PORT=8765 ./scripts/dev.sh`.

| Time | Do | Say / point out |
|---|---|---|
| 0:00 | **Dashboard.** Point at "Parser: LR(1), 241 states, 0 conflicts". | "A real C++17 compiler; the site only visualises its JSON. These numbers are computed by the compiler at startup." |
| 0:30 | **Playground** → load *Arithmetic & branching* → **Compile**. Click each pipeline stage. | "Seven real stages with measured times. Tokens, parse steps, AST, symbols, IR, optimizer log, bytecode." |
| 1:00 | **Syntax Analysis** → trace stepper; then *LR(1) states & tables*, pick a state. | "Canonical LR(1): item sets with closure/goto, ACTION/GOTO table, shift/reduce stack. Step through the trace." |
| 1:45 | **AST Explorer.** Click `10 + 20 * 2`; collapse a function. | "Precedence is already in the tree shape; each node has a source span." |
| 2:00 | **Optimization Lab** with *Optimization showcase*. Toggle passes off/on one by one. | "Each pass changes the IR; red/green diff; every transformation lists its reason. Metrics are measured, and the output is identical before/after — checked against a separate reference interpreter." |
| 2:30 | **CFG & Data-flow** with a loop + if/else program. Show the graph (loop header, dashed back edge), then *Liveness* stepper, then *SSA & φ nodes*. | "Basic blocks from leaders, dominators decide the loop, liveness iterates to a fixed point you can step through, and φ nodes appear exactly at merges." |
| 2:45 | **Run**, then **Debugger** → *Start*. Set a breakpoint on a `print` in a loop (click gutter), **Continue**, **Step back**. | "This is the real VM's state: variable watch, call stack, operand stack. Backward stepping is exact because every snapshot is a real VM state." |
| 3:30 | Load *Recursive factorial*; debugger → step into a call; open **Runtime Memory**. | "Frames are created and destroyed on CALL/RET. No heap exists in NOVA, so none is drawn; slots are frame-relative indexes, not addresses." |
| 4:00 | Load *Error: undefined variable*, then *type mismatch*. Hover squiggles; click a diagnostic. | "Exact line/column, category, explanation, and 'did you mean' via edit distance. No API key is needed; AI explanations are optional and only rephrase existing diagnostics." |
| 4:30 | **Compiler Tests** → *Run all tests*. | "This executes the C++ test binary and generates random programs for differential testing — the counts shown are what just ran." |
| 5:00 | Close with *Learn Mode* or limitations (no error recovery, no heap, VM bytecode not native). | |

**Backup if something fails:** `./scripts/test.sh` shows all suites in the terminal; `curl -s localhost:8000/api/health`.
