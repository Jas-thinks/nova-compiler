"""AI-assisted fixing and debugging whose output is *verified by the real compiler*.

Principle: the language model never decides what is correct. The C++ compiler (and VM) is the oracle.
  * fix_program(): the model proposes a corrected program; we re-compile it and only return a fix that the compiler
    accepts. If it still fails, the new diagnostics are fed back (agent loop, bounded by MAX_ATTEMPTS).
  * debug_runtime(): we run the program on the VM with tracing, hand the model the *recorded* failure + last trace
    steps, and the model explains the root cause. Its claims are grounded in compiler output, never in guesses.
Both functions take an injectable `complete` callable so they are unit-testable without network access.
"""
from __future__ import annotations

import difflib
import json
import re
from typing import Callable

from . import compiler_client as cc
from . import llm

MAX_ATTEMPTS = 3
SYSTEM = (
    "You are an expert compiler engineer helping with NOVA, a small statically typed language "
    "(fn, let, int/float/bool, if/while, print, to_int/to_float, no implicit conversions). "
    "The compiler output you are given is ground truth; never contradict it."
)


def _diag_summary(diags: list[dict]) -> str:
    return "\n".join(
        f"- line {d.get('loc', {}).get('line')}, col {d.get('loc', {}).get('col')}: [{d.get('code')}] {d.get('message')}"
        f" (hint: {d.get('suggestion')})"
        for d in diags
        if d.get("severity") == "error"
    )


def _extract_program(text: str) -> str | None:
    m = re.search(r"```(?:nova)?\n(.*?)```", text, re.S)
    code = (m.group(1) if m else text).strip("\n")
    return code + "\n" if code.strip() else None


def make_diff(old: str, new: str) -> str:
    return "".join(difflib.unified_diff(old.splitlines(True), new.splitlines(True), "original.nova", "fixed.nova"))


def fix_program(source: str, complete: Callable[..., str] | None = None, max_attempts: int = MAX_ATTEMPTS) -> dict:
    complete = complete or llm.complete
    first = cc.compile_source(source, run=False)
    if first.get("success"):
        return {"status": "nothing_to_fix", "attempts": [], "fixedSource": None, "diff": None}
    current_diags = first.get("diagnostics", [])
    attempts: list[dict] = []
    feedback = ""
    for n in range(1, max_attempts + 1):
        prompt = (
            "Fix the NOVA program below so it compiles. Change as little as possible and keep its intent.\n"
            "Reply with ONLY the complete corrected program in one ```nova code block, then one sentence on what you changed.\n\n"
            f"Compiler errors:\n{_diag_summary(current_diags)}\n{feedback}\n"
            f"Program:\n```nova\n{source}\n```\n"
        )
        reply = complete(prompt, system=SYSTEM)
        candidate = _extract_program(reply)
        if not candidate:
            attempts.append({"attempt": n, "verified": False, "reason": "model returned no program"})
            continue
        check = cc.compile_source(candidate, run=False)
        ok = bool(check.get("success"))
        attempts.append({"attempt": n, "verified": ok, "errorCount": check.get("errorCount", 0)})
        if ok:
            note = re.sub(r"```.*?```", "", reply, flags=re.S).strip()
            return {"status": "fixed", "attempts": attempts, "fixedSource": candidate, "diff": make_diff(source, candidate), "note": note}
        current_diags = check.get("diagnostics", [])
        feedback = f"\nYour previous attempt still failed with:\n{_diag_summary(current_diags)}\nTry again.\n"
    return {"status": "unverified", "attempts": attempts, "fixedSource": None, "diff": None,
            "note": f"No candidate passed the compiler after {max_attempts} attempts; nothing was applied."}


def debug_runtime(source: str, complete: Callable[..., str] | None = None, tail: int = 12) -> dict:
    complete = complete or llm.complete
    res = cc.compile_source(source, run=True, trace=True, target="baseline")
    if not res.get("success"):
        return {"status": "compile_error", "diagnostics": res.get("diagnostics", []), "analysis": None}
    ex = (res.get("execution") or {}).get("baseline") or {}
    err = ex.get("error")
    if not err:
        return {"status": "no_runtime_error", "output": ex.get("output", []), "analysis": None}
    lines = source.splitlines()
    ln = int(err.get("line", 0) or 0)
    trace = ex.get("trace") or []
    steps = [f"step {t['step']}: {t['event']}  frames={json.dumps(t.get('frames'))}" for t in trace[-tail:]]
    context = {"error": err, "sourceLine": lines[ln - 1] if 0 < ln <= len(lines) else "", "output": ex.get("output", []),
               "lastSteps": steps}
    prompt = (
        "A NOVA program failed at runtime on the VM. Using ONLY the recorded evidence, explain the root cause "
        "(which value was wrong and where it came from), then give a corrected snippet. Be concise (max 8 lines).\n\n"
        f"Program:\n```nova\n{source}\n```\n\nRecorded failure:\n{json.dumps(context, indent=1)}\n"
    )
    return {"status": "runtime_error", "error": err, "evidence": steps, "analysis": complete(prompt, system=SYSTEM)}


# ---------------------------------------------------------------------------------------------------------------------
# Rule-based runtime failure analysis (no LLM needed): evidence comes from the VM's recorded trace, reasoning is a fixed
# set of rules per trap kind. The LLM variant above is optional and only adds free-form explanation.
# ---------------------------------------------------------------------------------------------------------------------
def explain_runtime(source: str) -> dict:
    res = cc.compile_source(source, run=True, trace=True, target="baseline")
    if not res.get("success"):
        return {"status": "compile_error", "diagnostics": res.get("diagnostics", []), "mode": "rules"}
    ex = (res.get("execution") or {}).get("baseline") or {}
    err = ex.get("error")
    if not err:
        return {"status": "no_runtime_error", "output": ex.get("output", []), "steps": ex.get("steps"), "mode": "rules"}
    lines = source.split("\n")
    ln = int(err.get("line") or 0)
    text = lines[ln - 1] if 0 < ln <= len(lines) else ""
    trace = ex.get("trace") or []
    funcs = (res.get("bytecode") or {}).get("baseline", {}).get("functions", [])
    final = (trace[-1]["frames"] if trace else [])
    variables: dict[str, str] = {}
    fname = "main"
    if final:
        fr = final[-1]
        if 0 <= fr["fn"] < len(funcs):
            f = funcs[fr["fn"]]
            fname = f["name"]
            for n, v, tmp in zip(f["slotNames"], fr["locals"], f["slotIsTemp"]):
                if not tmp:
                    variables[n] = v
    msg = err["message"]
    notes: list[str] = []
    fixes: list[str] = []
    if "division by zero" in msg or "modulo by zero" in msg:
        m = re.search(r"[/%]\s*([A-Za-z_]\w*|\(.*?\)|\d+)", text)
        div = m.group(1) if m else None
        zeros = [n for n, v in variables.items() if v == "0"]
        if div and div in variables:
            notes.append(f"The divisor `{div}` is {variables[div]} when line {ln} runs (`{text.strip()}`).")
            fixes.append(f"Guard the operation: `if ({div} != 0) {{ … }}` or handle the zero case explicitly before dividing.")
        elif zeros:
            notes.append(f"Variables equal to 0 at the failure: {', '.join('`'+z+'`' for z in zeros)} — one of them is the divisor in `{text.strip()}`.")
            fixes.append("Check the divisor is non-zero before dividing.")
        else:
            notes.append(f"The divisor evaluates to 0 in `{text.strip()}` (it is computed from an expression, not a plain variable).")
            fixes.append("Check the divisor expression is non-zero before dividing.")
    elif "index out of bounds" in msg:
        m = re.search(r"([A-Za-z_]\w*)\s*\[\s*([^\]]+?)\s*\]", text)
        if m:
            arr, idx = m.groups()
            val = variables.get(idx.strip())
            notes.append(f"`{arr}[{idx}]` accessed an element outside the array" + (f" (`{idx}` = {val})." if val is not None else "."))
            fixes.append(f"Check the index first: `if ({idx} >= 0 && {idx} < len({arr})) {{ … }}`. Valid indices are 0 … len({arr}) - 1.")
        else:
            notes.append(f"An array access on line {ln} used an index outside 0 … len-1.")
            fixes.append("Bounds-check the index with len(array).")
    elif "negative array size" in msg:
        m = re.search(r"new\s+\w+\s*\[\s*([^\]]+?)\s*\]", text)
        v = variables.get(m.group(1).strip()) if m else None
        notes.append("`new T[n]` was called with a negative size" + (f" (`{m.group(1)}` = {v})." if m and v is not None else "."))
        fixes.append("Validate the size is >= 0 before allocating.")
    elif "stack overflow" in msg:
        notes.append(f"Call depth exceeded the limit while running `{fname}` — the recursion never reaches a base case for these arguments." if fname else "Call depth exceeded the limit.")
        shown = ", ".join(f"{k}={v}" for k, v in list(variables.items())[:4])
        if shown:
            notes.append(f"Arguments/locals in the deepest frame: {shown}.")
        fixes.append("Add or fix the base case (e.g. `if (n <= 0) { return … }`) and make sure the argument moves toward it on every call.")
    elif "step limit" in msg:
        notes.append("The program executed too many instructions — most likely an infinite loop.")
        if variables:
            notes.append("Variables when it was stopped: " + ", ".join(f"{k}={v}" for k, v in list(variables.items())[:6]) + ".")
        fixes.append("Check the loop condition changes: every iteration must update a variable that the condition tests.")
    else:
        notes.append(f"Runtime trap: {msg}.")
    return {"status": "runtime_error", "mode": "rules", "error": err, "function": fname, "sourceLine": text.strip(),
            "variables": variables, "analysis": notes, "suggestions": fixes, "output": ex.get("output", []),
            "evidence": [f"step {t['step']}: {t['event']}" for t in trace[-6:]]}
