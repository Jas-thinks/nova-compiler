# Architecture

```
Browser (React + TS + Vite + Tailwind + Monaco + React Flow)      — no compiler logic
        │  JSON over REST (/api/*)
FastAPI backend (Pydantic validation, session store, sandbox.py)  — no compiler logic, never exec()s user code
        │  argv-only subprocess, stdin = source, JSON on stdout, timeout + output cap + rlimits
C++17 engine  novac / nova_tests (nova_core static library)
        └─ lexer · parser(LR1) · ast · semantic · ir · optimizer · analysis · codegen · vm · interp · fuzz
        (optional adapters) gcc / clang → backend/app/explorer.py (compile-only viewers)
```

| Layer | Technology | Key files |
|---|---|---|
| Compiler | C++17, CMake, CTest (in-tree test framework) | `compiler/src/*`, `compiler/tests/` |
| Backend | Python 3.11+, FastAPI, Pydantic, httpx (optional LLM explain) | `backend/app/{main,models,compiler_client,sandbox,explorer,debug_sessions}.py` |
| Frontend | React 18, TypeScript, Vite, Tailwind, Monaco (bundled locally), React Flow, Lucide, Vitest | `frontend/src/{pages,features,components,state,services}` |

Design rules: the compiler is independent of the web layers; the frontend renders compiler JSON and never re-implements analysis; the debugger is a cursor over snapshots recorded by the real VM; metrics and test counts are measured, never stored constants. Older, module-level detail: [COMPILER_ARCHITECTURE.md](COMPILER_ARCHITECTURE.md).

## API summary
`GET /api/{health,capabilities,examples,passes,isa,grammar,grammar/state/{n},tests/last,explorer/languages}` · `POST /api/{compile,execute,optimize,tests/run,explain,explorer/c}` · `POST /api/debug/{start,step,continue,breakpoints,seek,reset}`. Details: [API_DOCUMENTATION.md](API_DOCUMENTATION.md) (compile responses now also carry `analysis`).

## Security & reliability boundaries
Implemented (`backend/app/sandbox.py`): argv-only `Popen` (no shell; the source goes through stdin / a temp file), wall-clock timeout killing the whole process group, hard stdout+stderr cap (child killed when exceeded), `RLIMIT_CPU`, `RLIMIT_AS`, no core dumps, scrubbed environment (`PATH`, `LC_ALL` only), request validation (source size, pass ids, step bounds), VM instruction / call-depth / output limits, C-viewer header allow-list and compile-only mode. `GET /api/health` reports whether rlimits are available and states that isolation is **none**.
**Not implemented:** filesystem/network/syscall isolation (containers, namespaces, seccomp), per-user quotas, authentication, rate limiting. Do not expose the service to untrusted users without running it in a container/VM. The FastAPI process itself never executes submitted code; only the NOVA VM (inside `novac`, bounded by limits) and gcc/clang (compile-only) process it.
