"""Compiler Explorer adapters.

An adapter wraps a *real* external toolchain and returns its real output. NOVA itself is the only language
with a full pipeline here. C is offered as a thin viewer over whichever of clang/gcc is installed:

* clang  -> Clang AST dump, LLVM IR, assembly
* gcc    -> GIMPLE dump, CFG dump, assembly   (used when clang is absent)

The adapter only compiles to text (-S / -fsyntax-only): it never links or runs submitted code. Source is
restricted to a small header allow-list because the preprocessor can otherwise read arbitrary host files.
This is a viewer, not a C/C++ compiler product and not a security sandbox.
"""
from __future__ import annotations

import re
import shutil
import tempfile
from pathlib import Path

from . import config
from .sandbox import limits_available, run_limited

MAX_C_BYTES = 16 * 1024
ALLOWED_HEADERS = {"stdio.h", "stdlib.h", "string.h", "stdint.h", "stdbool.h", "math.h", "stddef.h", "limits.h", "ctype.h"}
_INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])\s*([^>"]*)[>"]', re.M)
_FORBIDDEN = re.compile(r"#\s*(include_next|import|embed|pragma\s+GCC\s+(?:system_header|dependency))|__has_include|__has_embed", re.I)


class ExplorerError(Exception):
    def __init__(self, message: str, status: int = 400):
        super().__init__(message)
        self.status = status


def tools() -> dict[str, str | None]:
    return {"clang": shutil.which("clang"), "gcc": shutil.which("gcc")}


def languages() -> list[dict]:
    t = tools()
    c_tool = "clang" if t["clang"] else "gcc" if t["gcc"] else None
    outputs = {"clang": ["ast", "llvm-ir", "assembly"], "gcc": ["gimple", "cfg", "assembly"]}.get(c_tool or "", [])
    return [
        {"id": "nova", "name": "NOVA", "status": "full", "description": "Custom C++17 compiler: all stages, optimizer, CFG/SSA/data-flow, bytecode VM, debugger.",
         "outputs": ["tokens", "parse-trace", "ast", "symbols", "ir", "cfg", "dataflow", "ssa", "bytecode", "execution"], "endpoint": "/api/compile"},
        {"id": "c", "name": "C (viewer)", "status": "available" if c_tool else "unavailable", "toolchain": c_tool,
         "description": "Shows what the installed system compiler produces. Compile-only, header allow-list, no execution. Not a full C/C++ platform.",
         "outputs": outputs, "endpoint": "/api/explorer/c",
         "note": None if c_tool else "Neither clang nor gcc was found on PATH. Install one (e.g. `sudo apt install clang`) to enable this adapter.",
         "next_step": None if t["clang"] else "Install clang to additionally expose the Clang AST dump and LLVM IR; the adapter switches automatically."},
        {"id": "python", "name": "Python", "status": "not_implemented", "description": "No adapter implemented.", "outputs": []},
        {"id": "java", "name": "Java", "status": "not_implemented", "description": "No adapter implemented.", "outputs": []},
        {"id": "sql", "name": "SQL", "status": "not_implemented", "description": "No adapter implemented.", "outputs": []},
    ]


def _check_source(src: str) -> None:
    if len(src.encode()) > MAX_C_BYTES:
        raise ExplorerError(f"source exceeds {MAX_C_BYTES} bytes")
    if "\x00" in src:
        raise ExplorerError("source contains NUL bytes")
    if _FORBIDDEN.search(src):
        raise ExplorerError("preprocessor directive not allowed in this viewer (#include_next, #import, #embed, __has_include)")
    for _, name in _INCLUDE.findall(src):
        if name not in ALLOWED_HEADERS:
            raise ExplorerError(f"#include of '{name}' is not allowed; allowed headers: {', '.join(sorted(ALLOWED_HEADERS))}")


def compile_c(source: str, opt_level: int = 1) -> dict:
    _check_source(source)
    if opt_level not in (0, 1, 2, 3):
        raise ExplorerError("opt_level must be 0..3")
    t = tools()
    tool = "clang" if t["clang"] else "gcc" if t["gcc"] else None
    if tool is None:
        raise ExplorerError("no C toolchain (clang or gcc) is installed on the server", status=503)
    exe = t[tool]
    result: dict = {"tool": tool, "tool_path": exe, "opt_level": opt_level, "outputs": {}, "diagnostics": "", "resource_limits": limits_available()}
    with tempfile.TemporaryDirectory(prefix="nova-explorer-") as d:
        src = Path(d) / "input.c"
        src.write_text(source)
        base = [exe, f"-O{opt_level}", "-std=c11", "-Wall", "-I/nonexistent", str(src)]

        def run(extra: list[str]):
            r = run_limited(base[:1] + extra + base[1:], cwd=d, timeout=config.COMPILE_TIMEOUT_S, max_output=2 * 1024 * 1024)
            if r.timed_out:
                raise ExplorerError("compiler timed out", status=408)
            if r.output_truncated:
                raise ExplorerError("compiler output exceeded the size limit", status=413)
            return r

        if tool == "clang":
            ast = run(["-Xclang", "-ast-dump", "-fsyntax-only"])
            ll = run(["-S", "-emit-llvm", "-o", "-"])
            asm = run(["-S", "-o", "-", "-fno-asynchronous-unwind-tables"])
            result["outputs"] = {"ast": ast.stdout.decode(errors="replace"), "llvm-ir": ll.stdout.decode(errors="replace"), "assembly": asm.stdout.decode(errors="replace")}
            result["diagnostics"] = (asm.stderr or ll.stderr or ast.stderr).decode(errors="replace")
            result["ok"] = asm.returncode == 0
        else:
            out_s = Path(d) / "out.s"
            g = run(["-S", "-o", str(out_s), "-fno-asynchronous-unwind-tables", "-fdump-tree-gimple=stdout"])
            c = run(["-S", "-o", "/dev/null", "-fdump-tree-cfg=stdout"])
            result["ok"] = g.returncode == 0
            result["outputs"] = {
                "gimple": g.stdout.decode(errors="replace"),
                "cfg": c.stdout.decode(errors="replace"),
                "assembly": out_s.read_text(errors="replace") if out_s.exists() else "",
            }
            result["diagnostics"] = (g.stderr or c.stderr).decode(errors="replace")
    result["note"] = "Real output from the installed toolchain. Compile-only; code is never executed."
    return result
