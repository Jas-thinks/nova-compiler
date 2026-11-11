"""Thin, safe wrapper around the C++ `novac` executable.

Security notes: the compiler is started with an argument vector (never through a shell), the program source is
passed on stdin, and the child gets CPU/memory limits, a wall-clock timeout and its own session so it can be
killed as a group. The only user-controlled argv values are validated pass identifiers and bounded integers.
"""
from __future__ import annotations

import json
import os
from typing import Any, Sequence

from . import config
from .sandbox import RunResult, run_limited


class CompilerError(RuntimeError):
    """The compiler binary is missing, crashed, timed out or produced malformed output."""

    def __init__(self, message: str, status: int = 500):
        super().__init__(message)
        self.status = status


def _run(binary, args: Sequence[str], stdin: str = "", timeout: float | None = None) -> RunResult:
    if not os.path.exists(binary):
        raise CompilerError(f"compiler binary not found at {binary}. Build it first with ./scripts/build.sh", status=503)
    r = run_limited([str(binary), *args], stdin.encode("utf-8"), timeout=timeout)
    if r.timed_out:
        raise CompilerError(f"compiler exceeded the {timeout or config.COMPILE_TIMEOUT_S:.0f}s time limit", status=408)
    if r.output_truncated:
        raise CompilerError("compiler output exceeded the size limit and was killed")
    return r


def _parse(proc: RunResult) -> Any:
    if proc.returncode != 0:
        stderr = proc.stderr.decode("utf-8", "replace").strip()[:500]
        raise CompilerError(f"compiler exited with status {proc.returncode}: {stderr}")
    try:
        return json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        raise CompilerError(f"compiler produced invalid JSON: {exc}") from exc


def compile_source(
    source: str,
    passes: Sequence[str] | None = None,
    run: bool = False,
    trace: bool = False,
    target: str = "optimized",
    max_steps: int | None = None,
    max_trace: int | None = None,
) -> dict:
    args = ["compile"]
    if passes is None:
        args.append("--passes=all")
    else:
        args.append("--passes=" + (",".join(passes) if passes else "none"))
    if run:
        args.append("--run")
    if trace:
        args += ["--trace", f"--target={target}", f"--max-trace={int(max_trace or config.MAX_TRACE_STEPS)}"]
    args.append(f"--max-steps={int(max_steps or config.DEFAULT_VM_STEPS)}")
    return _parse(_run(config.NOVAC_BIN, args, source))


_grammar_cache: dict | None = None


def grammar() -> dict:
    global _grammar_cache
    if _grammar_cache is None:
        _grammar_cache = _parse(_run(config.NOVAC_BIN, ["grammar"]))
    return _grammar_cache


def lr_state(n: int) -> dict:
    return _parse(_run(config.NOVAC_BIN, ["state", str(int(n))]))


def pass_list() -> list[dict]:
    return _parse(_run(config.NOVAC_BIN, ["passes"]))


def isa() -> list[dict]:
    return _parse(_run(config.NOVAC_BIN, ["isa"]))


def fuzz(count: int, seed: int, passes: Sequence[str] | None) -> dict:
    p = "all" if passes is None else (",".join(passes) if passes else "none")
    return _parse(
        _run(config.NOVAC_BIN, ["fuzz", f"--count={int(count)}", f"--seed={int(seed)}", f"--passes={p}"], timeout=config.TESTS_TIMEOUT_S)
    )


def unit_tests() -> dict:
    """Run the C++ unit-test binary. It exits non-zero when tests fail but still prints its JSON report."""
    proc = _run(config.NOVA_TESTS_BIN, ["--json"], timeout=config.TESTS_TIMEOUT_S)
    try:
        return json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        raise CompilerError(f"test runner produced invalid JSON: {exc}") from exc
