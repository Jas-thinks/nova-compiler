"""Quick fixes, auto-fix, rule-based runtime analysis and LSP code actions (all verified by the real compiler)."""
import glob
import os

from fastapi.testclient import TestClient

from app import lsp, quickfix
from app.main import app

client = TestClient(app)
EX = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "examples")


def ex(name):
    return open(os.path.join(EX, name)).read()


def test_apply_edits_handles_multiple_edits_right_to_left():
    src = "ab\ncd\n"
    out = quickfix.apply_edits(src, [
        {"range": {"line": 1, "col": 1, "endLine": 1, "endCol": 2}, "newText": "X"},
        {"range": {"line": 2, "col": 2, "endLine": 2, "endCol": 3}, "newText": "YY"},
    ])
    assert out == "Xb\ncYY\n"


def test_autofix_repairs_the_error_examples_it_can():
    for name in ("e01_undefined_variable.nova", "e02_missing_semicolon.nova", "e03_type_mismatch.nova"):
        r = quickfix.autofix(ex(name))
        assert r["status"] == "fixed" and r["remainingErrors"] == 0 and r["steps"], name
        assert r["diff"].startswith("--- original.nova")


def test_autofix_is_honest_when_it_cannot_finish():
    r = quickfix.autofix(ex("e04_wrong_arguments.nova"))     # wrong argument count needs a human decision
    assert r["status"] == "partial" and r["remainingErrors"] >= 1


def test_suggest_marks_only_compiler_verified_fixes():
    r = quickfix.suggest(ex("e03_type_mismatch.nova"))
    assert r["errors"] >= 3 and r["fixes"]
    assert all("diff" in f and "edits" in f for f in r["fixes"])
    v = [f for f in r["fixes"] if f["verified"]]
    assert v and all(f["errorsAfter"] < f["errorsBefore"] or f["severity"] != "error" for f in v)
    assert any("to_float" in f["title"] for f in r["fixes"])


def test_fixed_program_actually_runs():
    fixed = quickfix.autofix(ex("e01_undefined_variable.nova"))["fixedSource"]
    r = client.post("/api/execute", json={"source": fixed}).json()
    assert r["success"] is True


def test_suggest_and_autofix_endpoints():
    s = client.post("/api/suggest", json={"source": ex("e02_missing_semicolon.nova")}).json()
    assert s["fixes"][0]["verified"] and s["fixes"][0]["title"] == "Insert ';'"
    a = client.post("/api/autofix", json={"source": ex("e02_missing_semicolon.nova")}).json()
    assert a["status"] == "fixed" and ";" in a["fixedSource"]


def test_runtime_explanation_division_by_zero_points_at_the_divisor():
    r = client.post("/api/debug/explain", json={"source": ex("r01_division_by_zero.nova")}).json()
    assert r["status"] == "runtime_error" and r["mode"] == "rules"
    assert r["variables"]["zero"] == "0"
    assert "`zero` is 0" in r["analysis"][0] and "!= 0" in r["suggestions"][0]


def test_runtime_explanation_stack_overflow_and_bounds():
    r = client.post("/api/debug/explain", json={"source": ex("r02_stack_overflow.nova")}).json()
    assert "base case" in r["suggestions"][0] and "forever" in r["analysis"][0]
    r = client.post("/api/debug/explain", json={"source": ex("r03_index_out_of_bounds.nova")}).json()
    assert "len(a)" in r["suggestions"][0] and "a[3]" in r["analysis"][0]


def test_explain_reports_clean_programs_and_compile_errors():
    assert client.post("/api/debug/explain", json={"source": ex("08_gcd.nova")}).json()["status"] == "no_runtime_error"
    assert client.post("/api/debug/explain", json={"source": ex("e01_undefined_variable.nova")}).json()["status"] == "compile_error"


def test_lsp_code_action_offers_the_fix_as_a_workspace_edit():
    s = lsp.NovaLanguageServer()
    uri = "file:///t.nova"
    src = ex("e02_missing_semicolon.nova")
    s.handle({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {"uri": uri, "version": 1, "text": src}}})
    acts = s.handle({"jsonrpc": "2.0", "id": 1, "method": "textDocument/codeAction", "params": {
        "textDocument": {"uri": uri}, "range": {"start": {"line": 0, "character": 0}, "end": {"line": 20, "character": 0}}, "context": {"diagnostics": []}}})[0]["result"]
    assert acts and acts[0]["title"] == "Insert ';'" and acts[0]["kind"] == "quickfix"
    edit = acts[0]["edit"]["changes"][uri][0]
    assert edit["newText"] == ";" and edit["range"]["start"] == edit["range"]["end"]
