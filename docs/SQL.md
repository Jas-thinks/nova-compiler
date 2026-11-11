# NOVA-SQL — a second compiler in the project

NOVA-SQL compiles and executes SQL against an in-memory database. It reuses the project's diagnostics/JSON infrastructure but is an independent compiler with its own front end, so the same pipeline ideas (lex → parse → analyse → IR → optimise → bytecode → execute) are applied to a declarative language.

```
script ──► lexer ──► recursive-descent parser ──► AST
                                                 │
        catalog ──► binder / type checker ◄──────┘    (names, types, aggregate rules, "did you mean")
                          │
                          ▼
                    logical plan  (Scan, Filter, Join, Aggregate, Project, Distinct, Sort, Limit)
                          │
                          ▼
            rule-based optimizer (with before/after history)
              constant_folding · remove_trivial_filter · predicate_pushdown · hash_join · top_n
                          │
                          ▼
        expression compiler ──► SQL-VM bytecode (stack machine, per-row)
                          │
                          ▼
                 executor (materializing operators)  ──► rows + per-operator row counts
```

Run it: `novac sql < script.sql` (JSON report), `POST /api/sql`, the **SQL Compiler** page of the studio, or `examples/sql/*.sql`.

## Language
`CREATE TABLE [IF NOT EXISTS]` (INT, FLOAT, TEXT; `PRIMARY KEY`, `NOT NULL`), `DROP TABLE [IF EXISTS]`, `INSERT … VALUES (…),(…)`, `UPDATE … SET … WHERE`, `DELETE FROM … WHERE`, and `SELECT [DISTINCT] … FROM t [alias] [, t | CROSS JOIN | [INNER] JOIN … ON | LEFT [OUTER] JOIN … ON] … WHERE … GROUP BY … HAVING … ORDER BY (expr | alias | ordinal) [ASC|DESC] … LIMIT n [OFFSET m]`.
Expressions: `+ - * / %`, `||`, comparisons, `AND OR NOT`, `IS [NOT] NULL`, `[NOT] LIKE`, `[NOT] IN (…)`, `[NOT] BETWEEN`, `UPPER LOWER LENGTH ABS COALESCE`, aggregates `COUNT(*) COUNT SUM AVG MIN MAX`. Operator precedence follows SQLite.

## Semantics (deliberately SQLite-compatible, then verified)
* **Three-valued logic**: `NULL AND 0 = 0`, `NULL OR 1 = 1`, `NOT NULL = NULL`, `NULL = NULL` is NULL, and `x IN (…, NULL)` yields NULL when nothing matches. `WHERE` keeps only rows whose condition is TRUE.
* Integer `/` truncates toward zero; division or modulo by zero yields NULL; NULLs sort first ascending; aggregates skip NULL (`COUNT(*)` does not); an aggregate without GROUP BY over zero rows yields one row; `LIKE` is case-insensitive for ASCII.
* **Stricter than SQLite on purpose**: no implicit conversion between numbers and text (`age = 'x'` is a type error with a location and suggestion), `%` needs INT operands, non-aggregated select columns must appear in GROUP BY. INT→FLOAT is the only implicit coercion (on INSERT/UPDATE).
* Limits (service protection): ≤100 000 rows per table, ≤5 000 000 join row-pairs per query, ≤5 000 result rows returned to the client.

## Optimizer
Each rewrite is recorded with a description and the plan before/after, and shown in the studio.
* **constant_folding** – literal-only subexpressions are evaluated at compile time with NULL semantics.
* **predicate_pushdown** – WHERE conjuncts move below joins to the input they reference; predicates spanning both sides become join conditions (CROSS → INNER); for LEFT JOIN, right-side WHERE predicates are *not* pushed (that would change results) while right-only ON conjuncts are.
* **hash_join** – a `left.col = right.col` conjunct becomes a hash key (NULL keys never match; the full condition is still evaluated on candidates, so residual predicates and LEFT JOIN padding stay correct).
* **top_n** – `ORDER BY … LIMIT n` uses a partial sort.

## Verification
* `scripts/sql_fuzz.py` generates random schemas, data, DML and well-typed queries (joins, GROUP BY/HAVING, DISTINCT, ORDER BY, LIMIT, NULLs, LIKE/IN/BETWEEN, scalar functions) and compares NOVA-SQL with **SQLite** (Python `sqlite3`) as an independent oracle: 4910/4910 agreed over 5 000 seeds (90 skipped because SQLite itself rejected the generated statement). It found a real bug during development (nested aggregate lost its static FLOAT type).
* `scripts/sql_fuzz.py --mutate` feeds garbled scripts to the compiler (under ASan/UBSan in `scripts/sanitize.sh`): it must always return diagnostics, never crash.
* 9 C++ unit tests (`compiler/tests/test_sql.cpp`), 8 API tests (`backend/tests/test_sql.py`, which also runs 200 differential cases), 3 UI tests.

## Not implemented
Subqueries, `UNION`, window functions, `CASE`, indexes (all scans are full scans; joins use hash or nested loop), transactions, persistence, multi-column hash keys, cost-based join ordering, `BOOL`/date types.
