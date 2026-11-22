"""Optional natural-language explanation of an *existing* compiler diagnostic.

The language model never produces diagnostics and never changes compilation: it only rephrases a diagnostic that
the C++ compiler already emitted. Works with ANTHROPIC_API_KEY or OPENROUTER_API_KEY (free models available). Without either the endpoint reports that it is unavailable and the
rest of NOVA is unaffected.
"""
from __future__ import annotations

import os

import httpx

OPENROUTER_URL = "https://openrouter.ai/api/v1/chat/completions"
OPENROUTER_MODEL = os.environ.get("NOVA_OPENROUTER_MODEL", "anthropic/claude-haiku-5.5")
MODEL = os.environ.get("NOVA_LLM_MODEL", "claude-haiku-5-5")
API_URL = "https://api.anthropic.com/v1/messages"


def _provider() -> str | None:
    if os.environ.get("ANTHROPIC_API_KEY"):
        return "anthropic"
    if os.environ.get("OPENROUTER_API_KEY"):
        return "openrouter"
    return None


def available() -> bool:
    return _provider() is not None


def complete(prompt: str, max_tokens: int = 1500, model: str | None = None, system: str | None = None, timeout: float = 60) -> str:
    """Single-turn completion. Raises on network/API failure; callers must degrade gracefully."""
    if _provider() == "openrouter":
        msgs = ([{"role": "system", "content": system}] if system else []) + [{"role": "user", "content": prompt}]
        resp = httpx.post(
            OPENROUTER_URL,
            headers={"Authorization": f"Bearer {os.environ['OPENROUTER_API_KEY']}", "content-type": "application/json"},
            json={"model": OPENROUTER_MODEL, "max_tokens": max(max_tokens, 2000), "messages": msgs},
            timeout=timeout,
        )
        resp.raise_for_status()
        return (resp.json()["choices"][0]["message"].get("content") or "").strip()
    body: dict = {"model": model or MODEL, "max_tokens": max_tokens, "messages": [{"role": "user", "content": prompt}]}
    if system:
        body["system"] = system
    resp = httpx.post(
        API_URL,
        headers={"x-api-key": os.environ["ANTHROPIC_API_KEY"], "anthropic-version": "2023-06-01", "content-type": "application/json"},
        json=body,
        timeout=timeout,
    )
    resp.raise_for_status()
    return "".join(p.get("text", "") for p in resp.json().get("content", []) if p.get("type") == "text").strip()


def explain(source: str, diag: dict) -> str:
    loc = diag.get("loc", {})
    lines = source.splitlines()
    line_no = int(loc.get("line", 1))
    snippet = lines[line_no - 1] if 0 < line_no <= len(lines) else ""
    prompt = (
        "You are helping a student understand a compiler error from NOVA, a small statically typed language.\n"
        "Explain the compiler diagnostic below in 2-4 simple sentences and show a corrected line if obvious.\n"
        "Do NOT invent additional errors and do not contradict the compiler.\n\n"
        f"Category: {diag.get('category')}\nMessage: {diag.get('message')}\nCompiler explanation: {diag.get('explanation')}\n"
        f"Compiler suggestion: {diag.get('suggestion')}\nLine {line_no}: {snippet}\n"
    )
    return complete(prompt, max_tokens=300, timeout=20)
