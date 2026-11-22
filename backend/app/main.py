"""NOVA backend: a thin FastAPI layer over the C++ compiler. It contains no compiler logic."""
from __future__ import annotations

import logging
import os
from contextlib import asynccontextmanager

from fastapi import FastAPI, HTTPException, Request
from fastapi.exceptions import RequestValidationError
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles

import time

from . import compiler_client as cc
from . import ai_assist, config, explorer, llm, quickfix
from .sandbox import limits_available
from .debug_sessions import store
from .examples import load_examples
from .models import (
    CompileRequest,
    DebugBreakpointsRequest,
    DebugContinueRequest,
    DebugResetRequest,
    DebugSeekRequest,
    DebugStartRequest,
    DebugStepRequest,
    ExecuteRequest,
    ExplorerCRequest,
    ExplainRequest,
    AsmRequest,
    SourceRequest,
    SqlRequest,
    OptimizeRequest,
    TestRunRequest,
)

log = logging.getLogger("nova")


@asynccontextmanager
async def lifespan(app: FastAPI):
    if not config.NOVAC_BIN.exists():
        log.warning("compiler binary %s not found - run ./scripts/build.sh", config.NOVAC_BIN)
    yield


app = FastAPI(title="NOVA Compiler API", version="1.0.0", lifespan=lifespan)
app.add_middleware(CORSMiddleware, allow_origins=config.CORS_ORIGINS, allow_methods=["GET", "POST"], allow_headers=["*"])


@app.exception_handler(cc.CompilerError)
async def _compiler_error(_: Request, exc: cc.CompilerError):
    return JSONResponse(status_code=exc.status, content={"error": "compiler_error", "detail": str(exc)})


@app.exception_handler(RequestValidationError)
async def _validation_error(_: Request, exc: RequestValidationError):
    errors = [{"field": ".".join(str(p) for p in e["loc"][1:]), "message": e["msg"]} for e in exc.errors()]
    return JSONResponse(status_code=422, content={"error": "invalid_request", "detail": errors})


# ---------------------------------------------------------------------------------------------- meta
@app.get("/api/health")
def health():
    ok = config.NOVAC_BIN.exists()
    info = {"status": "ok" if ok else "degraded", "compiler_found": ok, "compiler_path": str(config.NOVAC_BIN), "llm_available": llm.available(),
            "limits": {"max_source_bytes": config.MAX_SOURCE_BYTES, "compile_timeout_s": config.COMPILE_TIMEOUT_S, "max_vm_steps": config.MAX_VM_STEPS},
            "sandbox": {"process_timeout": True, "output_cap_bytes": config.MAX_OUTPUT_BYTES, "rlimits_available": limits_available(),
                        "isolation": "none (no container/namespace/seccomp): run inside a container or VM before exposing to untrusted users"}}
    if ok:
        g = cc.grammar()
        info["parser"] = {"algorithm": g["algorithm"], "states": g["states"], "conflicts": len(g["conflicts"])}
    return info


@app.get("/api/examples")
def examples():
    return {"examples": load_examples()}


@app.get("/api/passes")
def passes():
    return {"passes": cc.pass_list()}


@app.get("/api/isa")
def isa():
    return {"instructions": cc.isa()}


@app.get("/api/grammar")
def grammar():
    return cc.grammar()


@app.get("/api/grammar/state/{n}")
def grammar_state(n: int):
    g = cc.grammar()
    if n < 0 or n >= g["states"]:
        raise HTTPException(404, f"no LR(1) state {n}")
    return cc.lr_state(n)


# ---------------------------------------------------------------------------------------------- compile
@app.post("/api/compile")
def compile_(req: CompileRequest):
    return cc.compile_source(req.source, req.passes, run=req.run, max_steps=req.max_steps)


@app.post("/api/execute")
def execute(req: ExecuteRequest):
    r = cc.compile_source(req.source, req.passes, run=True, max_steps=req.max_steps)
    ex = r.get("execution")
    return {
        "success": r["success"],
        "failedStage": r["failedStage"],
        "diagnostics": r["diagnostics"],
        "stages": r["stages"],
        "execution": ex,
        "output": ex["optimized"]["output"] if ex else [],
        "error": ex["optimized"].get("error") if ex else None,
    }


@app.post("/api/optimize")
def optimize(req: OptimizeRequest):
    r = cc.compile_source(req.source, req.passes, run=True)
    if not r["success"]:
        return {"success": False, "failedStage": r["failedStage"], "diagnostics": r["diagnostics"], "stages": r["stages"]}
    ex = r["execution"]
    bc = r["bytecode"]
    return {
        "success": True,
        "config": r["optimization"]["config"],
        "ir": r["ir"],
        "optimization": r["optimization"],
        "bytecode_counts": {"baseline": bc["baseline"]["count"], "optimized": bc["optimized"]["count"]},
        "execution": ex,
        "metrics": {
            "ir_before": r["optimization"]["instructionsBefore"],
            "ir_after": r["optimization"]["instructionsAfter"],
            "ir_eliminated": r["optimization"]["eliminated"],
            "bytecode_before": bc["baseline"]["count"],
            "bytecode_after": bc["optimized"]["count"],
            "vm_steps_before": ex["baseline"]["steps"],
            "vm_steps_after": ex["optimized"]["steps"],
            "semantics_preserved": ex["optimizationPreservesBehavior"],
            "matches_reference_interpreter": ex["matchesReference"],
        },
        "bytecode": bc,
    }


# ---------------------------------------------------------------------------------------------- debugger
def _session(sid: str):
    s = store.get(sid)
    if s is None:
        raise HTTPException(404, "unknown or expired debug session; start a new one")
    return s


@app.post("/api/debug/start")
def debug_start(req: DebugStartRequest):
    r = cc.compile_source(req.source, req.passes, run=True, trace=True, target=req.target, max_steps=req.max_steps)
    if not r["success"]:
        return JSONResponse(status_code=200, content={"success": False, "failedStage": r["failedStage"], "diagnostics": r["diagnostics"], "stages": r["stages"]})
    ex = r["execution"][req.target]
    trace = ex.get("trace") or []
    if not trace:
        raise HTTPException(500, "VM returned no trace")
    session = store.create(
        bytecode=r["bytecode"][req.target],
        trace=trace,
        output=ex["output"],
        error=ex.get("error"),
        truncated=ex.get("traceTruncated", False),
        target=req.target,
        total_steps=ex["steps"],
    )
    session.breakpoints = set(req.breakpoints)
    view = session.view("started")
    return {"success": True, "diagnostics": r["diagnostics"], "bytecode": r["bytecode"][req.target], "state": view}


@app.post("/api/debug/step")
def debug_step(req: DebugStepRequest):
    s = _session(req.session_id)
    s.step(req.direction, req.mode, req.count)
    return s.view("step")


@app.post("/api/debug/continue")
def debug_continue(req: DebugContinueRequest):
    s = _session(req.session_id)
    if req.breakpoints is not None:
        s.breakpoints = set(req.breakpoints)
    if req.breakpoint_addrs is not None:
        s.breakpoint_addrs = set(req.breakpoint_addrs)
    reason = s.continue_()
    return s.view(reason)


@app.post("/api/debug/breakpoints")
def debug_breakpoints(req: DebugBreakpointsRequest):
    s = _session(req.session_id)
    s.breakpoints = set(req.breakpoints)
    s.breakpoint_addrs = set(req.breakpoint_addrs)
    return s.view("breakpoints")


@app.post("/api/debug/seek")
def debug_seek(req: DebugSeekRequest):
    s = _session(req.session_id)
    s.cursor = min(req.cursor, len(s.trace) - 1)
    return s.view("seek")


@app.post("/api/debug/reset")
def debug_reset(req: DebugResetRequest):
    s = _session(req.session_id)
    s.reset()
    return s.view("reset")


# ---------------------------------------------------------------------------------------------- tests & AI
@app.post("/api/tests/run")
def run_tests(req: TestRunRequest):
    out: dict = {}
    if req.unit_tests:
        out["unit"] = cc.unit_tests()
    if req.fuzz_count > 0:
        out["differential"] = cc.fuzz(req.fuzz_count, req.seed, req.passes)
    _last_tests.clear()
    _last_tests.update({"ran": True, "at": time.time(), "result": out})
    return out


_last_tests: dict = {}


@app.get("/api/tests/last")
def last_tests():
    return _last_tests or {"ran": False}


@app.get("/api/capabilities")
def capabilities():
    """What this build can really do, discovered by compiling a probe program (not a hard-coded claim)."""
    probe = cc.compile_source("fn f(n: int) -> int { let s: int = 0; while (n > 0) { s = s + n; n = n - 1; } return s; } fn main() { print(f(3)); }", None, run=True)
    g = cc.grammar()
    fn0 = probe["analysis"]["original"][0]
    return {
        "nova": {
            "pipeline_stages": [s["name"] for s in probe["stages"]],
            "stages_ok": all(s["status"] == "ok" for s in probe["stages"]),
            "parser": {"algorithm": g["algorithm"], "states": g["states"], "conflicts": len(g["conflicts"]), "productions": len(g["grammar"]["productions"]) - 1},
            "optimization_passes": [p["id"] for p in probe["optimization"]["passes"]],
            "analyses": {"cfg": "blocks" in fn0, "dominators": any(b["idom"] >= 0 for b in fn0["blocks"]), "liveness": fn0["liveness"]["converged"],
                         "reaching_definitions": fn0["reachingDefinitions"]["converged"], "ssa": "ssa" in fn0},
            "bytecode_instructions": len(cc.isa()),
            "execution_verified_against_reference": probe["execution"]["matchesReference"],
        },
        "explorer": explorer.languages(),
        "toolchains": explorer.tools(),
        "llm_available": llm.available(),
    }


@app.get("/api/explorer/languages")
def explorer_languages():
    return {"languages": explorer.languages(), "toolchains": explorer.tools()}


@app.post("/api/explorer/c")
def explorer_c(req: ExplorerCRequest):
    try:
        return explorer.compile_c(req.source, req.opt_level)
    except explorer.ExplorerError as exc:
        raise HTTPException(exc.status, str(exc))


@app.post("/api/explain")
def explain(req: ExplainRequest):
    if not llm.available():
        return {"available": False, "explanation": None, "note": "Set ANTHROPIC_API_KEY or OPENROUTER_API_KEY to enable optional plain-language explanations. The compiler diagnostics themselves never depend on it."}
    try:
        return {"available": True, "explanation": llm.explain(req.source, req.diagnostic)}
    except Exception as exc:  # network / API failure must not affect compilation
        return {"available": True, "explanation": None, "note": f"explanation service failed: {exc}"}


def _asm_stats(text: str) -> dict:
    ins = [l for l in text.splitlines() if l.startswith("    ") and not l.lstrip().startswith(".")]
    mem = sum(1 for l in ins if "(%rbp)" in l)
    return {"instructions": len(ins), "memoryOperands": mem, "functions": sum(1 for l in text.splitlines() if l.startswith("nova_fn_") and l.endswith(":")),
            "calleeSavedRegisterUses": sum(1 for l in ins if any(r in l for r in ("%rbx", "%r12", "%r13", "%r14", "%r15")))}


@app.post("/api/asm")
def native_asm(req: AsmRequest):
    """Native x86-64 backend: assembly for the program, with a register-allocation before/after comparison."""
    res = cc.compile_source(req.source, req.passes, run=False)
    if not res.get("success"):
        return {"success": False, "diagnostics": res.get("diagnostics", [])}
    text = cc.asm(req.source, req.passes, regalloc=req.regalloc)
    out = {"success": True, "assembly": text, "stats": _asm_stats(text), "regalloc": req.regalloc}
    other = cc.asm(req.source, req.passes, regalloc=not req.regalloc)
    out["compare"] = {"with_regalloc": _asm_stats(text if req.regalloc else other), "without_regalloc": _asm_stats(other if req.regalloc else text)}
    return out


@app.post("/api/sql")
def run_sql(req: SqlRequest):
    """NOVA-SQL compiler + engine: diagnostics, AST, plans before/after optimization, expression bytecode, results."""
    return cc.run_sql(req.script)


def _ai_call(fn, req: SourceRequest):
    if not llm.available():
        return {"available": False, "note": "Set ANTHROPIC_API_KEY or OPENROUTER_API_KEY to enable AI assistance. The compiler works without it."}
    try:
        return {"available": True, **fn(req.source)}
    except cc.CompilerError:
        raise
    except Exception as exc:  # API/network failure must never break compilation
        return {"available": True, "status": "ai_error", "note": f"AI service failed: {exc}"}


@app.post("/api/suggest")
def suggest(req: SourceRequest):
    """Compiler-verified quick fixes for the current diagnostics (no API key needed)."""
    return quickfix.suggest(req.source)


@app.post("/api/autofix")
def autofix(req: SourceRequest):
    """Iteratively apply verified fixes until the program compiles; returns the steps and a diff."""
    return quickfix.autofix(req.source)


@app.post("/api/debug/explain")
def debug_explain(req: SourceRequest):
    """Rule-based root-cause analysis of a runtime trap from the VM's recorded trace (no API key needed)."""
    return ai_assist.explain_runtime(req.source)


@app.post("/api/ai/fix")
def ai_fix(req: SourceRequest):
    """Model proposes a fix; the C++ compiler verifies it before it is returned."""
    return _ai_call(ai_assist.fix_program, req)


@app.post("/api/ai/debug")
def ai_debug(req: SourceRequest):
    """Model explains a runtime failure using the VM's recorded trace as evidence."""
    return _ai_call(ai_assist.debug_runtime, req)


# ---------------------------------------------------------------------------------------------- static frontend (production build)
_dist = config.ROOT / "frontend" / "dist"
if _dist.exists() and os.environ.get("NOVA_SERVE_FRONTEND", "1") == "1":
    app.mount("/assets", StaticFiles(directory=_dist / "assets"), name="assets")

    @app.get("/{path:path}", include_in_schema=False)
    def spa(path: str):
        f = _dist / path
        if path and f.is_file() and _dist in f.resolve().parents:
            return FileResponse(f)
        return FileResponse(_dist / "index.html")
