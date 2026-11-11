#!/usr/bin/env python3
"""Differential fuzzer: NOVA-SQL vs SQLite (independent oracle).

Generates random schemas, data, DML and well-typed queries (joins, GROUP BY/HAVING, DISTINCT, ORDER BY, LIMIT, NULLs,
LIKE/IN/BETWEEN, scalar functions), runs each script through `novac sql` and Python's sqlite3, and compares the results.
Usage: scripts/sql_fuzz.py [--count N] [--seed S] [--show K]
"""
import argparse
import json
import math
import os
import random
import sqlite3
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NOVAC = os.environ.get("NOVAC_BIN", os.path.join(ROOT, "compiler", "build", "novac"))

TEXTS = ["a", "ab", "Abc", "", "zz", "b c", "ba"]
PATTERNS = ["a%", "%b", "_b%", "A%", "%", "z_", "%c"]


class Gen:
    def __init__(self, seed):
        self.r = random.Random(seed)

    def pick(self, xs):
        return self.r.choice(xs)

    def chance(self, p):
        return self.r.random() < p

    # ---------------------------------------------------------------- schema + data
    def schema(self):
        self.tables = {}
        n = self.r.randint(1, 3)
        stmts = []
        for i in range(n):
            name = f"t{i}"
            cols = [("id", "INT"), ("a", "INT"), ("b", "INT"), ("x", "FLOAT"), ("s", "TEXT")]
            self.tables[name] = cols
            stmts.append(f"CREATE TABLE {name} (id INT PRIMARY KEY, a INT, b INT, x FLOAT, s TEXT);")
            rows = []
            for k in range(self.r.randint(0, 8)):
                rows.append("(" + ", ".join([str(k + 1), self.nullable(self.ival), self.nullable(self.ival), self.nullable(self.fval), self.nullable(self.sval)]) + ")")
            if rows:
                stmts.append(f"INSERT INTO {name} VALUES " + ", ".join(rows) + ";")
        return stmts

    def nullable(self, f):
        return "NULL" if self.chance(0.2) else f()

    def ival(self):
        return str(self.r.randint(-3, 6))

    def fval(self):
        return str(self.r.randint(-8, 16) / 4)

    def sval(self):
        return "'" + self.pick(TEXTS) + "'"

    # ---------------------------------------------------------------- expressions by type; `cols` = [(qualified name, type)]
    def col_of(self, cols, ty):
        c = [n for n, t in cols if t == ty]
        return self.pick(c) if c else None

    def int_expr(self, cols, d):
        c = self.col_of(cols, "INT")
        if d <= 0 or self.chance(0.3):
            return c if c and self.chance(0.7) else self.ival()
        k = self.r.randint(0, 9)
        if k <= 4:
            return f"({self.int_expr(cols, d-1)} {self.pick(['+','-','*','/','%'])} {self.int_expr(cols, d-1)})"
        if k == 5:
            return f"(-{self.int_expr(cols, d-1)})"
        if k == 6:
            return f"ABS({self.int_expr(cols, d-1)})"
        if k == 7:
            return f"LENGTH({self.text_expr(cols, d-1)})"
        if k == 8:
            return f"COALESCE({self.int_expr(cols, d-1)}, {self.int_expr(cols, d-1)})"
        return c or self.ival()

    def float_expr(self, cols, d):
        c = self.col_of(cols, "FLOAT")
        if d <= 0 or self.chance(0.3):
            return c if c and self.chance(0.7) else self.fval()
        k = self.r.randint(0, 5)
        if k <= 2:
            return f"({self.float_expr(cols, d-1)} {self.pick(['+','-','*','/'])} {self.num_expr(cols, d-1)})"
        if k == 3:
            return f"ABS({self.float_expr(cols, d-1)})"
        if k == 4:
            return f"COALESCE({self.float_expr(cols, d-1)}, {self.fval()})"
        return f"({self.int_expr(cols, d-1)} + {self.float_expr(cols, d-1)})"

    def num_expr(self, cols, d):
        return self.int_expr(cols, d) if self.chance(0.6) else self.float_expr(cols, d)

    def text_expr(self, cols, d):
        c = self.col_of(cols, "TEXT")
        if d <= 0 or self.chance(0.4):
            return c if c and self.chance(0.7) else self.sval()
        k = self.r.randint(0, 3)
        if k == 0:
            return f"({self.text_expr(cols, d-1)} || {self.text_expr(cols, d-1)})"
        if k == 1:
            return f"UPPER({self.text_expr(cols, d-1)})"
        if k == 2:
            return f"LOWER({self.text_expr(cols, d-1)})"
        return f"COALESCE({self.text_expr(cols, d-1)}, {self.sval()})"

    def bool_expr(self, cols, d):
        k = self.r.randint(0, 11) if d > 0 else self.r.randint(0, 5)
        if k <= 2:
            return f"({self.num_expr(cols, d-1)} {self.pick(['=','<>','<','<=','>','>='])} {self.num_expr(cols, d-1)})"
        if k == 3:
            return f"({self.text_expr(cols, d-1)} {self.pick(['=','<>','<','>='])} {self.text_expr(cols, d-1)})"
        if k == 4:
            e = self.pick([self.num_expr, self.text_expr])(cols, d - 1)
            return f"({e} IS {'NOT ' if self.chance(0.5) else ''}NULL)"
        if k == 5:
            return f"({self.text_expr(cols, d-1)} {'NOT ' if self.chance(0.3) else ''}LIKE '{self.pick(PATTERNS)}')"
        if k == 6:
            return f"({self.int_expr(cols, d-1)} {'NOT ' if self.chance(0.3) else ''}IN ({', '.join(self.pick([self.ival(), 'NULL']) for _ in range(self.r.randint(1,4)))}))"
        if k == 7:
            return f"({self.num_expr(cols, d-1)} {'NOT ' if self.chance(0.3) else ''}BETWEEN {self.ival()} AND {self.ival()})"
        if k == 8:
            return f"(NOT {self.bool_expr(cols, d-1)})"
        return f"({self.bool_expr(cols, d-1)} {self.pick(['AND','OR'])} {self.bool_expr(cols, d-1)})"

    # ---------------------------------------------------------------- statements
    def from_clause(self):
        """Returns (sql, cols) where cols = [(qualified column, type)] visible in the query."""
        names = list(self.tables)
        t1 = self.pick(names)
        cols = [(f"p.{c}", ty) for c, ty in self.tables[t1]]
        sql = f"FROM {t1} p"
        kind = self.pick(["single", "single", "inner", "left", "comma", "cross"])
        if kind == "single":
            return sql, cols
        t2 = self.pick(names)
        cols2 = [(f"q.{c}", ty) for c, ty in self.tables[t2]]
        allc = cols + cols2
        on = self.pick([f"p.a = q.a", f"p.id = q.b", f"p.a = q.b", f"(p.a < q.a)", f"p.s = q.s", f"(p.a = q.a AND p.b = q.b)", self.bool_expr(allc, 1)])
        if kind == "inner":
            return f"{sql} JOIN {t2} q ON {on}", allc
        if kind == "left":
            return f"{sql} LEFT JOIN {t2} q ON {on}", allc
        if kind == "cross":
            return f"{sql} CROSS JOIN {t2} q", allc
        return f"{sql}, {t2} q", allc

    def select(self):
        frm, cols = self.from_clause()
        where = f" WHERE {self.bool_expr(cols, 2)}" if self.chance(0.6) else ""
        if self.chance(0.45):   # aggregate query
            gcols = [c for c, t in cols if t in ("INT", "TEXT")]
            groups = self.r.sample(gcols, self.r.randint(0, min(2, len(gcols)))) if gcols else []
            items = list(groups)
            for _ in range(self.r.randint(1, 3)):
                items.append(self.agg(cols))
            sql = f"SELECT {', '.join(items)} {frm}{where}"
            if groups:
                sql += " GROUP BY " + ", ".join(groups)
            if self.chance(0.3):
                num_agg = lambda: self.pick(["COUNT(*)", f"SUM({self.int_expr(cols, 1)})", f"SUM({self.float_expr(cols, 1)})", f"AVG({self.num_expr(cols, 1)})"])
                sql += f" HAVING {num_agg()} {self.pick(['>','<=','='])} {self.ival()}" if self.chance(0.5) else f" HAVING {self.agg(cols)} IS NOT NULL"
            n = len(items)
        else:
            n = self.r.randint(1, 3)
            items = [self.pick([self.int_expr, self.float_expr, self.text_expr, self.bool_expr])(cols, 2) for _ in range(n)]
            distinct = "DISTINCT " if self.chance(0.2) else ""
            sql = f"SELECT {distinct}{', '.join(items)} {frm}{where}"
        if self.chance(0.7):   # order by every output column => total order => deterministic LIMIT
            sql += " ORDER BY " + ", ".join(f"{i+1}{' DESC' if self.chance(0.4) else ''}" for i in range(n))
            if self.chance(0.4):
                sql += f" LIMIT {self.r.randint(0, 5)}" + (f" OFFSET {self.r.randint(0, 3)}" if self.chance(0.3) else "")
        return sql + ";"

    def agg(self, cols):
        k = self.r.randint(0, 6)
        if k == 0:
            return "COUNT(*)"
        if k == 1:
            return f"COUNT({self.pick([c for c, _ in cols])})"
        if k == 2:
            return f"SUM({self.int_expr(cols, 1)})"
        if k == 3:
            return f"SUM({self.float_expr(cols, 1)})"
        if k == 4:
            return f"AVG({self.num_expr(cols, 1)})"
        return f"{self.pick(['MIN','MAX'])}({self.pick([c for c, _ in cols])})"

    def dml(self):
        t = self.pick(list(self.tables))
        cols = [(c, ty) for c, ty in self.tables[t]]
        k = self.r.randint(0, 2)
        if k == 0:
            return f"UPDATE {t} SET a = {self.int_expr(cols, 1)}, x = {self.float_expr(cols, 1)} WHERE {self.bool_expr(cols, 1)};"
        if k == 1:
            return f"DELETE FROM {t} WHERE {self.bool_expr(cols, 2)};"
        return f"INSERT INTO {t} (id, a, s) VALUES ({self.r.randint(50, 99)}, {self.int_expr([], 1)}, {self.text_expr([], 1)});"


# -------------------------------------------------------------------- execution + comparison
def run_sqlite(stmts):
    con = sqlite3.connect(":memory:")
    cur = con.cursor()
    last = None
    for s in stmts:
        cur.execute(s)
        if s.lstrip().upper().startswith("SELECT"):
            last = cur.fetchall()
    return last


def run_nova(stmts):
    p = subprocess.run([NOVAC, "sql"], input="\n".join(stmts).encode(), capture_output=True, timeout=60)
    if p.returncode != 0:
        return None, f"novac crashed rc={p.returncode}: {p.stderr.decode()[:200]}"
    d = json.loads(p.stdout)
    if not d["success"]:
        errs = [x["message"] for x in d["diagnostics"] if x["severity"] == "error"]
        return None, "; ".join(errs[:2])
    sel = [s for s in d["statements"] if s["kind"] == "SELECT"]
    return (sel[-1]["rows"] if sel else None), None


def norm(v):
    if v is None:
        return (0, 0)
    if isinstance(v, (int, float)):
        return (1, float(v))
    return (2, v)


def same_value(a, b):
    if a is None or b is None:
        return a is None and b is None
    if isinstance(a, str) or isinstance(b, str):
        return a == b
    if isinstance(a, float) or isinstance(b, float):
        return math.isclose(float(a), float(b), rel_tol=1e-9, abs_tol=1e-9)
    return a == b


def same_rows(a, b, ordered):
    if len(a) != len(b):
        return False
    if not ordered:
        key = lambda r: [norm(v) if not (isinstance(v, float)) else (1, round(v, 6)) for v in r]
        a, b = sorted(a, key=key), sorted(b, key=key)
    return all(len(x) == len(y) and all(same_value(p, q) for p, q in zip(x, y)) for x, y in zip(a, b))


def one_case(seed):
    g = Gen(seed)
    stmts = g.schema()
    for _ in range(g.r.randint(0, 2)):
        stmts.append(g.dml())
    q = g.select()
    stmts.append(q)
    ordered = "ORDER BY" in q
    try:
        want = run_sqlite(stmts)
    except sqlite3.Error as e:   # generator produced something SQLite rejects (e.g. PK clash): not a NOVA problem
        return "skip", stmts, str(e)
    got, err = run_nova(stmts)
    if err is not None:
        # a NOVA runtime constraint error matching a SQLite error is already skipped above; anything else is a real divergence
        return "fail", stmts, f"NOVA-SQL rejected a query SQLite accepts: {err}"
    if not same_rows(got, want, ordered):
        return "fail", stmts, f"results differ\n  nova  : {got}\n  sqlite: {want}"
    return "pass", stmts, ""


def mutate(text, r):
    t = list(text)
    for _ in range(r.randint(1, 4)):
        if not t:
            break
        i = r.randrange(len(t))
        k = r.randint(0, 3)
        if k == 0:
            del t[i]
        elif k == 1:
            t.insert(i, r.choice("();,'\"*%<>=|.-+ 0aZ"))
        elif k == 2:
            t[i] = r.choice("();,'\"*%<>=|.-+ 0aZ")
        else:
            j = r.randrange(len(t))
            t[i], t[j] = t[j], t[i]
    return "".join(t)


def mutation_fuzz(count, seed):
    """Garbled SQL must never crash the compiler: exit status 0 and parseable JSON with diagnostics, for every input."""
    bad = 0
    for i in range(count):
        g = Gen(seed + i)
        base = "\n".join(g.schema() + [g.select()])
        src = mutate(base, g.r)
        p = subprocess.run([NOVAC, "sql"], input=src.encode(), capture_output=True, timeout=60)
        try:
            d = json.loads(p.stdout)
            ok = p.returncode == 0 and ("success" in d) and (d["success"] or d["errorCount"] > 0)
        except Exception:
            ok = False
        if not ok:
            bad += 1
            if bad <= 3:
                print(f"--- CRASH/MALFORMED seed={seed + i} rc={p.returncode}\n{src}\n{p.stderr.decode()[:300]}")
    print(f"sql mutation fuzz: {count} garbled scripts, crashes/malformed={bad}")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mutate", action="store_true", help="mutation fuzzing for crashes instead of the SQLite differential")
    ap.add_argument("--count", type=int, default=300)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--show", type=int, default=3)
    a = ap.parse_args()
    if a.mutate:
        return mutation_fuzz(a.count, a.seed)
    res = {"pass": 0, "fail": 0, "skip": 0}
    shown = 0
    for i in range(a.count):
        st, stmts, why = one_case(a.seed + i)
        res[st] += 1
        if st == "fail" and shown < a.show:
            shown += 1
            print(f"--- FAIL seed={a.seed + i}: {why}")
            print("\n".join(stmts))
    print(f"sql differential vs SQLite {sqlite3.sqlite_version}: pass={res['pass']} fail={res['fail']} skipped={res['skip']}")
    return 1 if res["fail"] else 0


if __name__ == "__main__":
    sys.exit(main())
