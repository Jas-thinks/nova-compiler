"""Optional natural-language explanation of an *existing* compiler diagnostic.

The language model never produces diagnostics and never changes compilation: it only rephrases a diagnostic that
the C++ compiler already emitted. Without ANTHROPIC_API_KEY the endpoint reports that it is unavailable and the
rest of NOVA is unaffected.
"""
from __future__ import annotations

import os

import httpx

MODEL = os.environ.get("NOVA_LLM_MODEL", "claude-haiku-5-5")
API_URL = "https://api.anthropic.com/v1/messages"


def available() -> bool:
    return bool(os.environ.get("ANTHROPIC_API_KEY"))


def complete(prompt: str, max_tokens: int = 1500, model: str | None = None, system: str | None = None) -> str:
    """Single-turn completion. Raises on network/API failure; callers must degrade gracefully."""
    body: dict = {"model": model or MODEL, "max_tokens": max_tokens, "messages": [{"role": "user", "content": prompt}]}
    if system:
        body["system"] = system
    resp = httpx.post(
        API_URL,
        headers={"x-api-key": os.environ["ANTHROPIC_API_KEY"], "anthropic-version": "2023-06-01", "content-type": "application/json"},
        json=body,
        timeout=60,
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
    resp = httpx.post(
        API_URL,
        headers={"x-api-key": os.environ["ANTHROPIC_API_KEY"], "anthropic-version": "2023-06-01", "content-type": "application/json"},
        json={"model": MODEL, "max_tokens": 300, "messages": [{"role": "user", "content": prompt}]},
        timeout=20,
    )
    resp.raise_for_status()
    parts = resp.json().get("content", [])
    return "".join(p.get("text", "") for p in parts if p.get("type") == "text").strip()
