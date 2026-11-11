"""API tests run against the real C++ compiler binary (build it first: ./scripts/build.sh)."""
import pytest
from fastapi.testclient import TestClient

from app import config
from app.main import app

pytestmark = pytest.mark.skipif(not config.NOVAC_BIN.exists(), reason="novac binary not built")
client = TestClient(app)

HELLO = "fn main() {\n  let a: int = 10 + 20 * 2;\n  print(a);\n}\n"
LOOP = "fn main() {\n  let i: int = 0;\n  while (i < 3) {\n    print(i);\n    i = i + 1;\n  }\n}\n"
CALL = "fn sq(n: int) -> int {\n  return n * n;\n}\nfn main() {\n  print(sq(4));\n  print(sq(5));\n}\n"


def post(path, **body):
    return client.post(path, json=body)


def test_health():
    r = client.get("/api/health")
    assert r.status_code == 200
    j = r.json()
    assert j["compiler_found"] and j["parser"]["algorithm"] == "Canonical LR(1)" and j["parser"]["conflicts"] == 0


def test_examples_have_sources_and_metadata():
    ex = client.get("/api/examples").json()["examples"]
    assert len(ex) >= 10
    assert all(e["source"] and e["title"] for e in ex)
    assert {"basics", "errors", "runtime", "optimization"} <= {e["category"] for e in ex}


def test_compile_returns_every_stage_artifact():
    j = post("/api/compile", source=HELLO, run=True).json()
    assert j["success"] and [s["status"] for s in j["stages"]] == ["ok"] * 7
    for key in ("tokens", "parser", "ast", "semantic", "ir", "optimization", "bytecode", "execution"):
        assert key in j
    assert j["parser"]["trace"][-1]["action"] == "accept"
    assert j["execution"]["optimized"]["output"] == ["50"]
    assert j["tokens"][0]["lexeme"] == "fn"


def test_compile_syntax_error_has_location_and_suggestion():
    j = post("/api/compile", source="fn main() {\n let a: int = 5\n print(a);\n}").json()
    assert not j["success"] and j["failedStage"] == "parser"
    d = j["diagnostics"][0]
    assert d["category"] == "Missing semicolon" and d["loc"]["line"] == 2 and "';'" in d["suggestion"]
    assert "ir" not in j


def test_semantic_error_suggests_spelling_fix():
    j = post("/api/compile", source="fn main() { let counter: int = 1; print(countr); }").json()
    assert not j["success"]
    assert "counter" in j["diagnostics"][0]["suggestion"]


def test_execute_runtime_error():
    j = post("/api/execute", source="fn main() { let z: int = 0; print(1); print(5 / z); }").json()
    assert j["success"] and j["output"] == ["1"] and j["error"]["message"] == "division by zero"


def test_execute_reports_compile_failure():
    j = post("/api/execute", source="fn main() { x = 1; }").json()
    assert not j["success"] and j["execution"] is None


def test_infinite_loop_is_stopped_by_step_limit():
    j = post("/api/execute", source="fn main() { while (true) { } }", max_steps=5000).json()
    assert j["error"]["message"].startswith("execution step limit")


def test_optimize_metrics_come_from_real_data():
    src = "fn main() {\n let a: int = 2 * 3;\n let b: int = a + 4;\n let dead: int = 99;\n print(b);\n}"
    j = post("/api/optimize", source=src).json()
    m = j["metrics"]
    assert m["ir_after"] < m["ir_before"] and m["semantics_preserved"] and m["matches_reference_interpreter"]
    assert m["vm_steps_after"] < m["vm_steps_before"]
    assert j["execution"]["baseline"]["output"] == j["execution"]["optimized"]["output"] == ["10"]
    assert j["optimization"]["history"] and all(h["explanation"] for h in j["optimization"]["history"])


def test_optimize_with_passes_disabled():
    j = post("/api/optimize", source=HELLO, passes=[]).json()
    assert j["metrics"]["ir_before"] == j["metrics"]["ir_after"] and j["optimization"]["history"] == []


def test_individual_pass_selection():
    j = post("/api/optimize", source=HELLO, passes=["constant_folding"]).json()
    assert {h["pass"] for h in j["optimization"]["history"]} == {"constant_folding"}


def test_unknown_pass_rejected():
    r = post("/api/compile", source=HELLO, passes=["bogus"])
    assert r.status_code == 422 and r.json()["error"] == "invalid_request"


def test_malformed_requests_rejected():
    assert client.post("/api/compile", content="not json", headers={"content-type": "application/json"}).status_code == 422
    assert post("/api/compile").status_code == 422
    assert post("/api/compile", source=123).status_code == 422
    assert post("/api/compile", source="x" * (config.MAX_SOURCE_BYTES + 1)).status_code == 422
    assert post("/api/execute", source=HELLO, max_steps=10**12).status_code == 422
    assert post("/api/compile", source="a\x00b").status_code == 422


def test_source_is_never_interpreted_by_a_shell():
    j = post("/api/compile", source="fn main() { print(1); } ; $(touch /tmp/nova_pwned) `id`").json()
    assert not j["success"]
    import os
    assert not os.path.exists("/tmp/nova_pwned")


def test_grammar_and_state_endpoints():
    g = client.get("/api/grammar").json()
    assert g["algorithm"] == "Canonical LR(1)" and g["conflicts"] == [] and len(g["stateList"]) == g["states"]
    s = client.get("/api/grammar/state/0").json()
    assert s["kernel"] and len(s["closure"]) > len(s["kernel"])
    assert client.get("/api/grammar/state/999999").status_code == 404


def _start(src, **kw):
    j = post("/api/debug/start", source=src, **kw).json()
    assert j["success"], j
    return j


def test_debugger_step_forward_and_back():
    j = _start(HELLO, passes=[])
    sid = j["state"]["session_id"]
    assert j["state"]["step"] == 0 and j["state"]["frames"] == []
    s1 = post("/api/debug/step", session_id=sid).json()
    assert s1["step"] == 1 and s1["event"].startswith("call main")
    assert s1["frames"][0]["function"] == "main"
    # run to the end, then step back
    end = post("/api/debug/continue", session_id=sid).json()
    assert end["finished"] and end["output"] == ["50"] and end["reason"] == "finished"
    back = post("/api/debug/step", session_id=sid, direction="back").json()
    assert back["step"] == end["step"] - 1 and not back["finished"]
    assert post("/api/debug/reset", session_id=sid).json()["step"] == 0


def test_debugger_state_is_real_vm_state():
    sid = _start(CALL, passes=[])["state"]["session_id"]
    seen_depth, locals_seen = 0, {}
    for _ in range(200):
        st = post("/api/debug/step", session_id=sid).json()
        seen_depth = max(seen_depth, len(st["frames"]))
        for f in st["frames"]:
            for l in f["locals"]:
                if f["function"] == "sq" and l["name"] == "n":
                    locals_seen[l["value"]] = True
        if st["finished"]:
            break
    assert seen_depth == 2 and set(locals_seen) == {"4", "5"}
    assert st["output"] == ["16", "25"]


def test_debugger_breakpoints_by_line():
    sid = _start(LOOP, passes=[], breakpoints=[4])["state"]["session_id"]
    hits = []
    for _ in range(10):
        st = post("/api/debug/continue", session_id=sid).json()
        if st["finished"]:
            break
        assert st["reason"] == "breakpoint" and st["instruction"]["line"] == 4
        hits.append(len(st["output"]))
    assert hits == [0, 1, 2]    # the print(i) line is reached three times


def test_debugger_step_over_skips_call_bodies():
    sid = _start(CALL, passes=[], breakpoints=[5])["state"]["session_id"]
    st = post("/api/debug/continue", session_id=sid).json()
    assert st["instruction"]["line"] == 5 and len(st["frames"]) == 1
    st = post("/api/debug/step", session_id=sid, mode="over").json()
    assert st["instruction"]["line"] == 6 and len(st["frames"]) == 1 and st["output"] == ["16"]


def test_debugger_runtime_error_state():
    sid = _start("fn main() { let z: int = 0; print(1 / z); }", passes=[])["state"]["session_id"]
    st = post("/api/debug/continue", session_id=sid).json()
    assert st["finished"] and st["error"]["message"] == "division by zero"


def test_debugger_unknown_session_and_compile_error():
    assert post("/api/debug/step", session_id="deadbeefdeadbeef").status_code == 404
    j = post("/api/debug/start", source="fn main( {").json()
    assert j["success"] is False


def test_tests_endpoint_runs_real_suites():
    j = post("/api/tests/run", fuzz_count=40, seed=3).json()
    assert j["unit"]["failed"] == 0 and j["unit"]["total"] == len(j["unit"]["tests"]) > 50
    d = j["differential"]
    assert d["requested"] == 40 and d["failed"] == 0 and d["passed"] + d["inconclusive"] == d["compiled"]


def test_explain_works_without_api_key(monkeypatch):
    monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
    j = post("/api/explain", source=HELLO, diagnostic={"message": "x"}).json()
    assert j["available"] is False


def test_debugger_seek_and_isa():
    sid = _start(LOOP, passes=[])["state"]["session_id"]
    mid = post("/api/debug/seek", session_id=sid, cursor=20).json()
    assert mid["cursor"] == 20 and mid["step"] == 20
    beyond = post("/api/debug/seek", session_id=sid, cursor=10**6).json()
    assert beyond["finished"]
    isa = client.get("/api/isa").json()["instructions"]
    assert {i["op"] for i in isa} >= {"PUSH", "ADD", "JZ", "CALL", "RET", "HALT"} and all(i["description"] for i in isa)


# ------------------------------------------------------------------ advanced analysis, explorer, sandbox
LOOPY = "fn main() {\n  let i: int = 0;\n  let s: int = 0;\n  while (i < 4) {\n    s = s + i;\n    i = i + 1;\n  }\n  print(s);\n}\n"


def test_compile_returns_cfg_dataflow_and_ssa():
    j = post("/api/compile", source=LOOPY, passes=[]).json()
    fn = j["analysis"]["original"][0]
    assert fn["summary"]["loops"] == 1 and fn["summary"]["blocks"] >= 4
    assert any(e["back"] for e in fn["edges"]) and any(b["loopHeader"] for b in fn["blocks"])
    live, rd = fn["liveness"], fn["reachingDefinitions"]
    assert live["converged"] and rd["converged"] and live["iterations"] >= 2
    assert len(live["history"]) == live["iterations"] and "use" in live and "def" in live and "gen" in rd
    header = next(b["id"] for b in fn["blocks"] if b["loopHeader"])
    assert {"i", "s"} <= set(live["in"][header])
    phis = fn["ssa"]["blocks"][header]["phis"]
    assert {p["var"] for p in phis} == {"i", "s"} and all(len(p["args"]) == 2 for p in phis)
    assert fn["ssa"]["phiCount"] == 2
    assert j["analysis"]["optimized"]


def test_optimized_cfg_is_smaller_for_dead_branch():
    src = "fn main() { let d: bool = false; if (d) { print(1); } print(2); }"
    j = post("/api/compile", source=src).json()
    assert len(j["analysis"]["optimized"][0]["blocks"]) < len(j["analysis"]["original"][0]["blocks"])


def test_capabilities_are_discovered_not_hardcoded():
    j = client.get("/api/capabilities").json()
    n = j["nova"]
    assert n["stages_ok"] and n["parser"]["conflicts"] == 0 and len(n["optimization_passes"]) == 6
    assert all(n["analyses"].values()) and n["execution_verified_against_reference"]
    langs = {l["id"]: l for l in j["explorer"]}
    assert langs["nova"]["status"] == "full"
    assert langs["python"]["status"] == langs["java"]["status"] == langs["sql"]["status"] == "not_implemented"


def test_explorer_c_returns_real_toolchain_output_or_clear_error():
    from app import explorer
    if not any(explorer.tools().values()):
        r = post("/api/explorer/c", source="int main(){return 0;}")
        assert r.status_code == 503
        return
    r = post("/api/explorer/c", source="int sq(int x){ return x*x; }", opt_level=1)
    assert r.status_code == 200
    j = r.json()
    assert j["ok"] and "sq" in j["outputs"]["assembly"]
    assert ("gimple" in j["outputs"] and "sq" in j["outputs"]["gimple"]) or ("llvm-ir" in j["outputs"] and "define" in j["outputs"]["llvm-ir"])


def test_explorer_rejects_dangerous_preprocessor_and_headers():
    for src in ('#include "/etc/passwd"\nint x;', "#include <../../etc/shadow>\nint x;", "#include_next <stdio.h>\n", "#embed \"/etc/passwd\"\nint x[]={};"):
        assert post("/api/explorer/c", source=src).status_code == 400
    assert post("/api/explorer/c", source="x" * 20000).status_code == 422


def test_explorer_syntax_error_is_reported_not_hidden():
    from app import explorer
    if not any(explorer.tools().values()):
        return
    j = post("/api/explorer/c", source="int main( { return 0 }").json()
    assert j["ok"] is False and "error" in j["diagnostics"]


def test_sandbox_enforces_timeout_and_output_cap():
    from app.sandbox import run_limited
    r = run_limited(["/bin/sleep", "5"], timeout=0.5)
    assert r.timed_out
    r = run_limited(["/usr/bin/yes"], timeout=5, max_output=100_000)
    assert r.output_truncated and len(r.stdout) <= 100_000


def test_sandbox_does_not_use_shell_or_leak_environment(monkeypatch):
    monkeypatch.setenv("SECRET_TOKEN", "hunter2")
    from app.sandbox import run_limited
    r = run_limited(["/usr/bin/env"])
    assert b"SECRET_TOKEN" not in r.stdout and b"PATH=/usr/bin:/bin" in r.stdout
    r = run_limited(["/bin/echo", "$(id) `id` ; echo pwned"])
    assert r.stdout.strip() == b"$(id) `id` ; echo pwned"


def test_health_reports_sandbox_honestly():
    s = client.get("/api/health").json()["sandbox"]
    assert s["process_timeout"] and "none" in s["isolation"]


def test_last_test_run_is_recorded_after_a_real_run():
    post("/api/tests/run", fuzz_count=5, seed=1)
    j = client.get("/api/tests/last").json()
    assert j["ran"] and j["result"]["unit"]["failed"] == 0
