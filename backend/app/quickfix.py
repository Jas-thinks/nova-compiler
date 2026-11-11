"""Compiler-verified quick fixes (no LLM, no API key).

Each rule turns one compiler diagnostic into a concrete text edit. A fix is only offered as `verified` when applying it and
re-running the real compiler removes that diagnostic without adding new errors. `autofix()` applies verified fixes
iteratively until the program compiles (or nothing more can be fixed automatically).
"""
from __future__ import annotations

import difflib
import re
from typing import Any

from . import compiler_client as cc

MAX_AUTOFIX_STEPS = 12


def _offset(lines: list[str], line: int, col: int) -> int:
    """1-based (line, col) -> absolute offset into the text joined with '\\n'."""
    line = max(1, min(line, len(lines)))
    return sum(len(x) + 1 for x in lines[: line - 1]) + max(0, col - 1)


def apply_edits(source: str, edits: list[dict]) -> str:
    lines = source.split("\n")
    spans = []
    for e in edits:
        r = e["range"]
        spans.append((_offset(lines, r["line"], r["col"]), _offset(lines, r.get("endLine", r["line"]), r.get("endCol", r["col"])), e["newText"]))
    for a, b, text in sorted(spans, reverse=True):          # right to left keeps earlier offsets valid
        source = source[:a] + text + source[b:]
    return source


def _slice(source: str, loc: dict) -> str:
    lines = source.split("\n")
    a = _offset(lines, loc["line"], loc["col"])
    b = _offset(lines, loc.get("endLine", loc["line"]), loc.get("endCol", loc["col"]))
    return source[a:b]


def _edit(loc: dict, new: str) -> dict:
    return {"range": {"line": loc["line"], "col": loc["col"], "endLine": loc.get("endLine", loc["line"]), "endCol": loc.get("endCol", loc["col"])}, "newText": new}


def _split_top_level(expr: str, op: str):
    """Split `a OP b` at the first operator occurrence outside parentheses (and not part of ==, <=, ->, ...)."""
    depth = 0
    i = 0
    while i < len(expr):
        c = expr[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        elif depth == 0 and expr.startswith(op, i) and i > 0:
            prev, nxt = expr[i - 1], expr[i + len(op): i + len(op) + 1]
            if not (op in "<>=!" and nxt == "=") and not (op in "+-" and prev in "eE") and nxt != "=" or op in ("==", "!=", "<=", ">=", "&&", "||"):
                return expr[:i].strip(), expr[i + len(op):].strip()
        i += 1
    return None


def _wrap(expr: str, fn: str) -> str:
    return f"{fn}({expr.strip()})"


def fixes_for(source: str, d: dict) -> list[dict]:
    """Candidate fixes for one diagnostic (unverified)."""
    code, msg, loc = d.get("code", ""), d.get("message", ""), d["loc"]
    out: list[dict] = []

    def add(title, edits, kind="quickfix"):
        out.append({"title": title, "code": code, "line": loc["line"], "edits": edits, "kind": kind})

    if code == "E-SYN-001":                                                           # missing semicolon
        add("Insert ';'", [_edit({"line": loc["line"], "col": loc["col"], "endLine": loc["line"], "endCol": loc["col"]}, ";")])
    elif code in ("E-SEM-001", "E-SEM-006"):                                           # undefined name with a close match
        m = re.search(r"Did you mean '([^']+)'", d.get("suggestion", ""))
        if m:
            add(f"Rename to '{m.group(1)}'", [_edit(loc, m.group(1))])
    elif code == "E-SEM-004":
        text = _slice(source, loc)
        if "Cannot initialise" in msg or "Cannot assign" in msg or "Returning" in msg or "Argument" in msg:
            to_int = re.search(r"(?:value of type|assign|Returning|has type) float", msg) and re.search(r"\bint\b", msg)
            to_float = re.search(r"(?:value of type|assign|Returning|has type) int", msg) and re.search(r"\bfloat\b", msg)
            if to_int and not to_float:
                add("Convert with to_int(…) (truncates)", [_edit(loc, _wrap(text, "to_int"))])
            elif to_float:
                add("Convert with to_float(…)", [_edit(loc, _wrap(text, "to_float"))])
        elif "must be bool but has type int" in msg:
            add(f"Compare explicitly: ({text.strip()} != 0)", [_edit(loc, f"({text.strip()} != 0)")])
    elif code == "E-SEM-005":                                                          # int/float operator mix
        m = re.search(r"Operator '([^']+)' cannot be applied to (int|float) and (int|float)", msg)
        if m and m.group(2) != m.group(3):
            op, lt, rt = m.groups()
            parts = _split_top_level(_slice(source, loc), op)
            if parts:
                l, r = parts
                if lt == "int":
                    add(f"Convert the left operand: to_float({l}) {op} {r}", [_edit(loc, f"{_wrap(l, 'to_float')} {op} {r}")])
                    add(f"Truncate the right operand: {l} {op} to_int({r})", [_edit(loc, f"{l} {op} {_wrap(r, 'to_int')}")])
                else:
                    add(f"Convert the right operand: {l} {op} to_float({r})", [_edit(loc, f"{l} {op} {_wrap(r, 'to_float')}")])
                    add(f"Truncate the left operand: to_int({l}) {op} {r}", [_edit(loc, f"{_wrap(l, 'to_int')} {op} {r}")])
    elif code == "E-SEM-009":                                                          # missing return
        m = re.search(r"value of type (\w+)", msg)
        default = {"int": "0", "float": "0.0", "bool": "false"}.get(m.group(1) if m else "", "0")
        lines = source.split("\n")
        indent = re.match(r"\s*", lines[loc["line"] - 1]).group(0) + "    "
        add(f"Add 'return {default};' at the end of the function", [_edit({"line": loc["line"], "col": 1, "endLine": loc["line"], "endCol": 1}, f"{indent}return {default};\n")])
    elif code == "W-SEM-001":                                                          # unused variable
        lines = source.split("\n")
        text = lines[loc["line"] - 1] if loc["line"] - 1 < len(lines) else ""
        if re.match(r"\s*let\s+\w+\s*:\s*\w+\s*=.*;\s*$", text):
            add(f"Remove the unused variable declaration", [_edit({"line": loc["line"], "col": 1, "endLine": loc["line"] + 1, "endCol": 1}, "")], kind="cleanup")
    return out


def _errors(res: dict) -> list[dict]:
    return [x for x in res.get("diagnostics", []) if x.get("severity") == "error"]


def suggest(source: str, include_warnings: bool = True) -> dict:
    """All fixes for the current diagnostics; each is verified by recompiling the patched source."""
    res = cc.compile_source(source, run=False)
    base_errors = _errors(res)
    base_sig = {(x["code"], x["loc"]["line"], x["loc"]["col"]) for x in res.get("diagnostics", [])}
    fixes = []
    for d in res.get("diagnostics", []):
        if d["severity"] != "error" and not include_warnings:
            continue
        for f in fixes_for(source, d):
            patched = apply_edits(source, f["edits"])
            after = cc.compile_source(patched, run=False)
            after_errors = _errors(after)
            gone = (d["code"], d["loc"]["line"], d["loc"]["col"]) not in {(x["code"], x["loc"]["line"], x["loc"]["col"]) for x in after.get("diagnostics", [])}
            f["diagnostic"] = d["message"]
            f["severity"] = d["severity"]
            f["errorsBefore"], f["errorsAfter"] = len(base_errors), len(after_errors)
            f["verified"] = bool(gone and len(after_errors) <= len(base_errors) - (1 if d["severity"] == "error" else 0))
            f["compilesAfter"] = bool(after.get("success"))
            f["diff"] = "".join(difflib.unified_diff(source.splitlines(True), patched.splitlines(True), "original.nova", "fixed.nova"))
            fixes.append(f)
    fixes.sort(key=lambda f: (not f["verified"], f["severity"] != "error", f["line"]))
    return {"errors": len(base_errors), "compiles": bool(res.get("success")), "fixes": fixes}


def autofix(source: str) -> dict:
    """Iteratively apply the first verified fix for the first error until the program compiles."""
    cur = source
    steps: list[dict[str, Any]] = []
    for _ in range(MAX_AUTOFIX_STEPS):
        res = cc.compile_source(cur, run=False)
        if res.get("success"):
            break
        applied = False
        for d in _errors(res):
            for f in fixes_for(cur, d):
                patched = apply_edits(cur, f["edits"])
                after = cc.compile_source(patched, run=False)
                if len(_errors(after)) < len(_errors(res)):
                    steps.append({"title": f["title"], "code": f["code"], "line": f["line"], "errorsBefore": len(_errors(res)), "errorsAfter": len(_errors(after))})
                    cur, applied = patched, True
                    break
            if applied:
                break
        if not applied:
            break
    final = cc.compile_source(cur, run=False)
    return {
        "status": "fixed" if final.get("success") else ("partial" if steps else "no_automatic_fix"),
        "steps": steps, "fixedSource": cur if steps else None,
        "remainingErrors": len(_errors(final)),
        "diff": "".join(difflib.unified_diff(source.splitlines(True), cur.splitlines(True), "original.nova", "fixed.nova")),
    }
