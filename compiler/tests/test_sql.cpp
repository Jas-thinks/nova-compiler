// NOVA-SQL unit tests (end-to-end through runSqlScript plus direct plan/optimizer checks).
#include "nova/sql.hpp"
#include "test_framework.hpp"
#include "../src/sql/sql_internal.hpp"

using namespace nova;

namespace {
const std::string kSetup =
    "CREATE TABLE users (id INT PRIMARY KEY, name TEXT NOT NULL, age INT, city TEXT);"
    "CREATE TABLE orders (oid INT PRIMARY KEY, uid INT, amount FLOAT);"
    "INSERT INTO users VALUES (1,'ann',30,'NYC'),(2,'bob',25,'LA'),(3,'cy',NULL,'NYC'),(4,'dee',41,'SF');"
    "INSERT INTO orders VALUES (10,1,9.5),(11,1,20),(12,2,5.25),(13,9,1);";

std::string json(const std::string& sql) { return runSqlScript(sql).dump(); }
bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
// rows of the LAST statement, as JSON text
std::string lastRows(const std::string& sql) {
    std::string j = json(sql);
    size_t p = j.rfind("\"rows\":["); if (p == std::string::npos) return "";
    size_t e = j.find(",\"rowCount\"", p);
    return j.substr(p + 7, e - (p + 7));
}
std::string plan(const std::string& sql) {   // optimized plan text of the last SELECT
    std::string j = json(sql);
    size_t p = j.rfind("\"optimized\":\""); if (p == std::string::npos) return "";
    size_t e = j.find("\",\"optimizedWithStats\"", p);
    return j.substr(p + 13, e - (p + 13));
}
} // namespace

TEST(sql, precedenceAndLiterals) {
    CHECK_EQ(lastRows("SELECT 1 + 2 * 3, 'it''s', 7 / 2, -7 / 2, -7 % 2, 'a' || 'b' || 'c';"), std::string("[[7,\"it's\",3,-3,-1,\"abc\"]]"));
    auto po = sql::parseScript("SELECT a OR b AND c FROM t;");
    CHECK(!po.diags.hasErrors());
    CHECK(has(po.stmts[0].sexpr(), "(a OR (b AND c))"));
    CHECK(has(sql::parseScript("SELECT 1 + 2 * 3;").stmts[0].sexpr(), "(1 + (2 * 3))"));
}

TEST(sql, threeValuedLogicAndNullSemantics) {
    CHECK_EQ(lastRows("SELECT NULL AND 0, NULL AND 1, NULL OR 1, NULL OR 0, NOT NULL, NULL = NULL, NULL IS NULL, 1 IN (2, NULL), 1 IN (1, NULL), 2 NOT IN (1, NULL);"),
             std::string("[[0,null,1,null,null,null,1,null,1,null]]"));
    CHECK_EQ(lastRows("SELECT 1 / 0, 5 % 0, 1.5 / 0, COALESCE(NULL, NULL, 7), ABS(-3), LENGTH('héllo'), UPPER('ab'), 'AbC' LIKE 'a%';"),
             std::string("[[null,null,null,7,3,5,\"AB\",1]]"));
    // WHERE keeps only rows where the condition is TRUE: NULL age fails both x > 20 and NOT (x > 20)
    CHECK_EQ(lastRows(kSetup + "SELECT COUNT(*) FROM users WHERE age > 20;"), std::string("[[3]]"));
    CHECK_EQ(lastRows(kSetup + "SELECT COUNT(*) FROM users WHERE NOT (age > 20);"), std::string("[[0]]"));
    // NULL sorts first ascending (SQLite convention), aggregates ignore NULL, COUNT(*) does not
    CHECK_EQ(lastRows(kSetup + "SELECT age FROM users ORDER BY age;"), std::string("[[null],[25],[30],[41]]"));
    CHECK_EQ(lastRows(kSetup + "SELECT COUNT(*), COUNT(age), SUM(age), MIN(age), MAX(age) FROM users;"), std::string("[[4,3,96,25,41]]"));
    CHECK_EQ(lastRows(kSetup + "SELECT SUM(age), AVG(age), COUNT(*) FROM users WHERE id > 100;"), std::string("[[null,null,0]]"));   // aggregate over zero rows
}

TEST(sql, joinsGroupingAndOrdering) {
    CHECK_EQ(lastRows(kSetup + "SELECT u.name, o.amount FROM users u LEFT JOIN orders o ON u.id = o.uid ORDER BY 1, 2;"),
             std::string("[[\"ann\",9.5],[\"ann\",20],[\"bob\",5.25],[\"cy\",null],[\"dee\",null]]"));
    CHECK_EQ(lastRows(kSetup + "SELECT u.name, SUM(o.amount) AS total FROM users u JOIN orders o ON u.id = o.uid GROUP BY u.name HAVING SUM(o.amount) > 5 ORDER BY total DESC;"),
             std::string("[[\"ann\",29.5],[\"bob\",5.25]]"));
    CHECK_EQ(lastRows(kSetup + "SELECT DISTINCT city FROM users ORDER BY 1 DESC LIMIT 2 OFFSET 1;"), std::string("[[\"NYC\"],[\"LA\"]]"));
    CHECK_EQ(lastRows(kSetup + "SELECT name FROM users ORDER BY age DESC, name;"), std::string("[[\"dee\"],[\"ann\"],[\"bob\"],[\"cy\"]]"));   // ORDER BY an unselected column
    CHECK_EQ(lastRows(kSetup + "SELECT COUNT(*) FROM users, orders;"), std::string("[[16]]"));
}

TEST(sql, optimizerRules) {
    // predicate pushdown: the single-table conjuncts end up below the join, the cross join becomes a hash join
    std::string p = plan(kSetup + "SELECT u.name FROM users u, orders o WHERE u.id = o.uid AND u.age > 20 AND o.amount < 10;");
    CHECK(has(p, "HashJoin INNER"));
    CHECK(!has(p, "CROSS"));
    size_t join = p.find("HashJoin"), f1 = p.find("Filter (u.age > 20)"), f2 = p.find("Filter (o.amount < 10)");
    CHECK(f1 != std::string::npos && f2 != std::string::npos && f1 > join && f2 > join);   // both filters are children of the join
    // LEFT JOIN: a WHERE condition on the right side must NOT be pushed below the join (it would change the result)
    std::string lp = plan(kSetup + "SELECT u.name FROM users u LEFT JOIN orders o ON u.id = o.uid WHERE o.amount IS NULL;");
    size_t lj = lp.find("LEFT"), lf = lp.find("Filter (o.amount IS NULL)");
    CHECK(lf != std::string::npos && lf < lj);
    CHECK_EQ(lastRows(kSetup + "SELECT u.name FROM users u LEFT JOIN orders o ON u.id = o.uid WHERE o.amount IS NULL ORDER BY 1;"), std::string("[[\"cy\"],[\"dee\"]]"));
    // constant folding + trivial filter removal + top-N
    std::string j = json(kSetup + "SELECT name FROM users WHERE 1 = 1 AND age > 2 * 10 ORDER BY age LIMIT 2;");
    CHECK(has(j, "\"rule\":\"constant_folding\""));
    CHECK(has(j, "\"rule\":\"top_n\""));
    CHECK(has(plan(kSetup + "SELECT name FROM users WHERE age > 2 * 10 ORDER BY age LIMIT 2;"), "(age > 20)") || has(plan(kSetup + "SELECT name FROM users WHERE age > 2 * 10 ORDER BY age LIMIT 2;"), "(users.age > 20)"));
    CHECK(has(json(kSetup + "SELECT name FROM users WHERE 1 = 1;"), "\"rule\":\"remove_trivial_filter\""));
}

TEST(sql, hashJoinMatchesNestedLoop) {
    // same query planned with and without the optimizer must return the same rows (multiset)
    std::string a = lastRows(kSetup + "SELECT u.id, o.oid FROM users u LEFT JOIN orders o ON u.id = o.uid AND o.amount > 6 ORDER BY 1, 2;");
    std::string b = lastRows(kSetup + "SELECT u.id, o.oid FROM users u LEFT JOIN orders o ON o.amount > 6 AND u.id + 0 = o.uid + 0 ORDER BY 1, 2;");   // no plain col=col key -> nested loop
    CHECK_EQ(a, b);
    CHECK_EQ(a, std::string("[[1,10],[1,11],[2,null],[3,null],[4,null]]"));
    CHECK(has(plan(kSetup + "SELECT u.id FROM users u LEFT JOIN orders o ON o.amount > 6 AND u.id + 0 = o.uid + 0;"), "NestedLoopJoin LEFT"));
}

TEST(sql, diagnosticsWithSuggestions) {
    CHECK(has(json(kSetup + "SELECT nme FROM users;"), "Did you mean 'name'?"));
    CHECK(has(json(kSetup + "SELECT * FROM userz;"), "Did you mean 'users'?"));
    CHECK(has(json(kSetup + "SELECT zzz FROM users;"), "E-SQL-101"));
    CHECK(has(json(kSetup + "SELECT x.id FROM users u;"), "Unknown table or alias 'x'"));
    CHECK(has(json(kSetup + "SELECT id FROM users a JOIN users b ON a.id = b.id;"), "E-SQL-102"));           // ambiguous column
    CHECK(has(json(kSetup + "SELECT name FROM users WHERE age = 'x';"), "Cannot compare INT with TEXT"));      // no implicit conversion
    CHECK(has(json(kSetup + "SELECT name, COUNT(*) FROM users;"), "E-SQL-107"));                                // must appear in GROUP BY
    CHECK(has(json(kSetup + "SELECT name FROM users WHERE COUNT(*) > 1;"), "E-SQL-106"));                       // aggregate in WHERE
    CHECK(has(json(kSetup + "SELECT UPPERR(name) FROM users;"), "Did you mean UPPER?"));
    CHECK(has(json(kSetup + "SELECT name FROM users ORDER BY 9;"), "E-SQL-112"));
}

TEST(sql, syntaxErrorsAreLocatedAndRecovered) {
    std::string j = json("SELEC 1; SELECT 2;");
    CHECK(has(j, "E-SQL-010") && has(j, "\"success\":false"));
    CHECK(has(json("SELECT 1 +;"), "Expected an expression"));
    CHECK(has(json("SELECT 'abc;"), "E-SQL-001"));                       // unterminated string
    CHECK(has(json("SELECT 1 FROM"), "Expected a table name"));
    auto po = sql::parseScript("SELECT 1 +;\nSELECT 2 FROM ;");
    CHECK_EQ(po.diags.errorCount(), size_t(2));                          // panic-mode recovery reports both statements
    CHECK_EQ(po.diags.items()[1].loc.line, 2);
}

TEST(sql, dmlAndConstraints) {
    CHECK_EQ(lastRows(kSetup + "UPDATE users SET age = age + 1 WHERE city = 'NYC'; SELECT age FROM users ORDER BY id;"), std::string("[[31],[25],[null],[41]]"));
    CHECK_EQ(lastRows(kSetup + "DELETE FROM users WHERE age > 28; SELECT COUNT(*) FROM users;"), std::string("[[2]]"));
    CHECK(has(json(kSetup + "INSERT INTO users VALUES (1,'dup',1,'x');"), "UNIQUE constraint failed: users.id"));
    CHECK(has(json(kSetup + "INSERT INTO users (id, age) VALUES (9, 1);"), "NOT NULL constraint failed: users.name"));
    CHECK(has(json(kSetup + "INSERT INTO users VALUES (9, 'x', 'old', 'y');"), "Cannot store a TEXT value in column 'age'"));
    CHECK(has(json(kSetup + "INSERT INTO users (id, nme) VALUES (9, 'x');"), "Did you mean 'name'?"));
    // a failing INSERT is atomic: nothing from the statement is stored
    CHECK_EQ(lastRows(kSetup + "INSERT INTO users VALUES (20,'ok',1,'x'),(1,'dup',1,'x'); SELECT COUNT(*) FROM users;"), std::string("[[4]]"));
    CHECK_EQ(lastRows(kSetup + "INSERT INTO orders (oid, amount) VALUES (50, 3); SELECT amount FROM orders WHERE oid = 50;"), std::string("[[3]]"));   // INT -> FLOAT coercion
}

TEST(sql, bytecodeIsReportedPerExpression) {
    std::string j = json(kSetup + "SELECT age * 2 + 1 FROM users WHERE age BETWEEN 20 AND 40;");
    CHECK(has(j, "\"bytecode\""));
    CHECK(has(j, "LOAD slot2") && has(j, "MUL") && has(j, "GE") && has(j, "LE") && has(j, "AND"));   // BETWEEN is desugared to >= AND <=
}
