"""NOVA-SQL API tests + a differential run against SQLite (independent oracle)."""
import importlib.util
import os
import sys

from fastapi.testclient import TestClient

from app.main import app

client = TestClient(app)
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

SETUP = (
    "CREATE TABLE users (id INT PRIMARY KEY, name TEXT NOT NULL, age INT);"
    "INSERT INTO users VALUES (1,'ann',30),(2,'bob',25),(3,'cy',NULL);"
)


def sql(script):
    r = client.post("/api/sql", json={"script": script})
    assert r.status_code == 200, r.text
    return r.json()


def test_select_returns_rows_columns_and_plan():
    d = sql(SETUP + "SELECT name, age * 2 AS dbl FROM users WHERE age > 20 ORDER BY age DESC LIMIT 5;")
    assert d["success"] is True
    s = d["statements"][-1]
    assert s["columns"] == ["name", "dbl"] and s["rows"] == [["ann", 60], ["bob", 50]]
    assert "Scan users" in s["plan"]["optimized"]
    assert s["bytecode"] and s["stats"]["rowsScanned"] == 3
    assert d["catalog"][0]["name"] == "users" and d["catalog"][0]["rows"] == 3


def test_optimizer_history_is_reported():
    d = sql(SETUP + "SELECT u.name FROM users u, users v WHERE u.id = v.id AND u.age > 1 ORDER BY 1 LIMIT 2;")
    rules = [h["rule"] for h in d["statements"][-1]["plan"]["history"]]
    assert "predicate_pushdown" in rules and "hash_join" in rules and "top_n" in rules


def test_diagnostics_have_location_code_and_suggestion():
    d = sql(SETUP + "SELECT nme FROM users;")
    assert d["success"] is False
    x = d["diagnostics"][0]
    assert x["code"] == "E-SQL-101" and "name" in x["suggestion"] and x["loc"]["line"] == 1


def test_syntax_error_does_not_execute():
    d = sql("SELEC 1;")
    assert d["success"] is False and d["statements"] == [] and d["diagnostics"][0]["phase"] == "syntax"


def test_later_statements_run_after_an_error():
    d = sql(SETUP + "INSERT INTO users VALUES (1,'dup',1); SELECT COUNT(*) FROM users;")
    assert d["success"] is False
    assert d["statements"][-1]["rows"] == [[3]]          # failed INSERT was atomic, script continued


def test_oversized_script_rejected_by_validation():
    r = client.post("/api/sql", json={"script": "SELECT 1;" * 20000})
    assert r.status_code == 422


def test_cross_join_work_limit_is_enforced():
    rows = ",".join(f"({i})" for i in range(3000))
    d = sql(f"CREATE TABLE t (a INT); INSERT INTO t VALUES {rows}; SELECT COUNT(*) FROM t x, t y;")
    assert d["statements"][-1]["ok"] is False
    assert "E-SQL-113" in d["statements"][-1]["diagnostics"][0]["code"]


def test_differential_vs_sqlite():
    spec = importlib.util.spec_from_file_location("sql_fuzz", os.path.join(ROOT, "scripts", "sql_fuzz.py"))
    mod = importlib.util.module_from_spec(spec)
    sys.modules["sql_fuzz"] = mod
    spec.loader.exec_module(mod)
    fails = []
    for seed in range(1, 201):
        status, stmts, why = mod.one_case(seed)
        if status == "fail":
            fails.append((seed, why, stmts[-1]))
    assert not fails, fails[:3]
