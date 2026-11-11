"""AI fix/debug loop tests with a scripted fake model (no network). The real compiler is the verifier."""
import pytest
from fastapi.testclient import TestClient

from app import ai_assist, llm
from app.main import app

BAD = "fn main() {\n    let a: int = 5;\n    let b: float = 2.5;\n    let c: float = a + b;\n    print(c);\n}\n"
GOOD = "fn main() {\n    let a: int = 5;\n    let b: float = 2.5;\n    let c: float = to_float(a) + b;\n    print(c);\n}\n"
STILL_BAD = "fn main() {\n    let c: int = 2.5;\n}\n"
DIV0 = "fn main() {\n    print(5);\n    let z: int = 0;\n    print(10 / z);\n}\n"


def scripted(*replies):
    it = iter(replies)
    calls = []

    def fake(prompt, **kw):
        calls.append(prompt)
        return next(it)
    fake.calls = calls
    return fake


def test_fix_verified_by_compiler():
    f = scripted(f"```nova\n{GOOD}```\nWrapped a in to_float.")
    r = ai_assist.fix_program(BAD, complete=f)
    assert r["status"] == "fixed" and r["fixedSource"] == GOOD
    assert "+    let c: float = to_float(a) + b;" in r["diff"]
    assert r["attempts"] == [{"attempt": 1, "verified": True, "errorCount": 0}]


def test_fix_retries_with_new_diagnostics_then_succeeds():
    f = scripted(f"```nova\n{STILL_BAD}```", f"```nova\n{GOOD}```")
    r = ai_assist.fix_program(BAD, complete=f)
    assert r["status"] == "fixed" and len(r["attempts"]) == 2
    assert "still failed" in f.calls[1]  # compiler feedback was fed back to the model


def test_unverified_fix_is_never_returned():
    f = scripted(*[f"```nova\n{STILL_BAD}```"] * 3)
    r = ai_assist.fix_program(BAD, complete=f)
    assert r["status"] == "unverified" and r["fixedSource"] is None


def test_valid_program_needs_no_model_call():
    f = scripted()
    assert ai_assist.fix_program(GOOD, complete=f)["status"] == "nothing_to_fix"
    assert f.calls == []


def test_debug_uses_recorded_trace_as_evidence():
    f = scripted("z is 0 so the division fails.")
    r = ai_assist.debug_runtime(DIV0, complete=f)
    assert r["status"] == "runtime_error" and r["error"]["message"] == "division by zero"
    assert "division by zero" in f.calls[0] and "print(10 / z)" in f.calls[0]
    assert r["evidence"], "trace steps must be attached"


def test_debug_skips_model_when_no_runtime_error():
    f = scripted()
    assert ai_assist.debug_runtime(GOOD, complete=f)["status"] == "no_runtime_error"
    assert f.calls == []


def test_endpoints_degrade_without_key(monkeypatch):
    monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
    c = TestClient(app)
    for path in ("/api/ai/fix", "/api/ai/debug"):
        r = c.post(path, json={"source": BAD})
        assert r.status_code == 200 and r.json()["available"] is False


def test_endpoint_with_mocked_model(monkeypatch):
    monkeypatch.setenv("ANTHROPIC_API_KEY", "x")
    monkeypatch.setattr(llm, "complete", lambda *a, **k: f"```nova\n{GOOD}```")
    r = TestClient(app).post("/api/ai/fix", json={"source": BAD}).json()
    assert r["status"] == "fixed"
