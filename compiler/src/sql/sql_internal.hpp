// Internal types shared by the NOVA-SQL lexer/parser, binder, optimizer and executor.
#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "nova/diagnostics.hpp"
#include "nova/json.hpp"

namespace nova { namespace sql {

// ------------------------------------------------------------------ values
enum class ST { Null, Int, Float, Text };   // static types and runtime value tags
const char* stName(ST t);

struct SVal {
    ST t = ST::Null;
    int64_t i = 0;
    double f = 0;
    std::string s;
    static SVal null() { return {}; }
    static SVal Int(int64_t v) { SVal x; x.t = ST::Int; x.i = v; return x; }
    static SVal Float(double v) { SVal x; x.t = ST::Float; x.f = v; return x; }
    static SVal Text(std::string v) { SVal x; x.t = ST::Text; x.s = std::move(v); return x; }
    bool isNull() const { return t == ST::Null; }
    bool numeric() const { return t == ST::Int || t == ST::Float; }
    double num() const { return t == ST::Int ? static_cast<double>(i) : f; }
    Json toJson() const;
    std::string text() const;      // SQL-ish literal text
};
// Total order used for ORDER BY / GROUP BY / DISTINCT / MIN / MAX: NULL < numbers < text (SQLite).
int cmpTotal(const SVal& a, const SVal& b);
using Row = std::vector<SVal>;
struct RowLess { bool operator()(const Row& a, const Row& b) const; };

// ------------------------------------------------------------------ expression AST
struct Expr;
using ExprP = std::unique_ptr<Expr>;
struct Expr {
    enum Kind { Lit, Col, Un, Bin, Func, Agg, Star, IsNull, Like, In, Between } kind = Lit;
    std::string op;               // Bin: + - * / % || = <> < <= > >= AND OR | Un: - NOT | IsNull: "IS NULL"/"IS NOT NULL" | Like/In/Between: with optional NOT prefix
    SVal lit;
    std::string table, name;      // Col (lower-case); Func/Agg name (upper-case)
    std::vector<ExprP> args;
    bool countStar = false;       // COUNT(*)
    SourceLoc loc;
    int slot = -1;                // Col after binding: index into the operator's input row
    ST type = ST::Null;           // static type after binding
    ExprP clone() const;
    std::string text() const;     // canonical SQL text (also used for structural equality)
};

struct SelectItem { ExprP e; std::string alias; bool star = false; std::string starTable; SourceLoc loc; };
struct TableRef { std::string name, alias; SourceLoc loc; };
struct JoinClause { enum K { Inner, Left, Cross } kind = Inner; TableRef t; ExprP on; };
struct OrderItem { ExprP e; bool desc = false; };
struct Select {
    bool distinct = false;
    std::vector<SelectItem> items;
    bool hasFrom = false;
    TableRef from;
    std::vector<JoinClause> joins;
    ExprP where, having;
    std::vector<ExprP> groupBy;
    std::vector<OrderItem> orderBy;
    int64_t limit = -1, offset = 0;
    SourceLoc loc;
};
struct ColDef { std::string name; ST type = ST::Int; bool notNull = false, primaryKey = false; SourceLoc loc; };
struct Stmt {
    enum K { Create, Insert, Select_, Update, Delete, Drop } kind = Select_;
    SourceLoc loc;
    std::string text;                              // source slice
    std::string table; bool ifFlag = false;        // IF [NOT] EXISTS
    std::vector<ColDef> cols;                      // Create
    std::vector<std::string> insCols;              // Insert
    std::vector<std::vector<ExprP>> rows;          // Insert
    Select sel;                                    // Select_
    std::vector<std::pair<std::string, ExprP>> sets; // Update
    ExprP where;                                   // Update / Delete
    std::string sexpr() const;
};

// ------------------------------------------------------------------ lexer / parser
struct SToken { enum K { Ident, Int, Float, Str, Op, Eof } k = Eof; std::string text; SourceLoc loc; int off = 0, endOff = 0; };
struct ParseOutput { std::vector<Stmt> stmts; DiagnosticList diags; size_t tokenCount = 0; std::vector<SToken> tokens; };
ParseOutput parseScript(const std::string& src);

// ------------------------------------------------------------------ catalog
struct Table { std::string name; std::vector<ColDef> cols; std::vector<Row> rows; int colIndex(const std::string& n) const; };
struct Catalog {
    std::map<std::string, Table> tables;
    Table* find(const std::string& n) { auto it = tables.find(n); return it == tables.end() ? nullptr : &it->second; }
};

// ------------------------------------------------------------------ logical plan
struct AggSpec { std::string fn; ExprP arg; bool star = false; std::string text; ST type = ST::Int; };
struct SortKey { int slot = 0; bool desc = false; };
struct Plan;
using PlanP = std::unique_ptr<Plan>;
struct Plan {
    enum K { Scan, OneRow, Filter, Join, Aggregate, Project, Distinct, Sort, Limit } k = Scan;
    std::vector<PlanP> kids;
    std::string table, alias;                 // Scan
    ExprP pred;                               // Filter / Join condition (may be null for cross joins)
    JoinClause::K jk = JoinClause::Inner;     // Join
    bool hash = false; int lslot = -1, rslot = -1;   // hash join: key slots local to each side
    std::vector<ExprP> exprs;                 // Project outputs / Aggregate group keys
    std::vector<AggSpec> aggs;                // Aggregate
    std::vector<std::string> names;           // output names (Project)
    std::vector<SortKey> keys;                // Sort
    int64_t limit = -1, offset = 0; int keep = -1;   // Limit (+ trailing trim to `keep` columns)
    int64_t topN = -1;                        // Sort: only the first N rows are needed
    int width = 0;                            // number of output slots
    int64_t rowsOut = -1;                     // filled by the executor
    std::string text(int depth, bool withStats) const;   // indented plan rendering
    Json toJson() const;
};

struct OptStep { std::string rule, description, before, after; };

// ------------------------------------------------------------------ expression bytecode (SQL-VM)
enum class SOp { PUSH, LOAD, ADD, SUB, MUL, DIV, MOD, NEG, NOT, CAT, EQ, NE, LT, LE, GT, GE, AND, OR, ISNULL, NOTNULL, LIKE,
                 UPPER, LOWER, LENGTH, ABS, COALESCE };
struct SInstr { SOp op; int a = 0; SVal k; };
struct SCode { std::vector<SInstr> code; std::string source; std::vector<std::string> disasm() const; };
SCode compileExpr(const Expr& e);
SVal runCode(const SCode& c, const Row& row);
bool truthy(const SVal& v);               // WHERE semantics: only non-NULL, non-zero numbers pass
SVal evalConst(const Expr& e);            // evaluate an expression without columns

// ------------------------------------------------------------------ binder / planner / optimizer / executor
struct BindResult { PlanP plan; std::vector<std::string> names; std::vector<ST> types; DiagnosticList diags; };
BindResult planSelect(const Select& sel, Catalog& cat);
std::vector<OptStep> optimizePlan(PlanP& plan);
std::vector<Row> executePlan(Plan& plan, Catalog& cat, std::deque<SCode>& compiled, Json& stats);   // deque: compiled code is referenced while more is appended
// Type-check an expression against a single table (UPDATE/DELETE) or no table (INSERT VALUES). Returns false on error.
bool bindTableExpr(Expr& e, const Table* t, DiagnosticList& d);
Diagnostic semErr(const std::string& cat, const std::string& code, const std::string& msg, const std::string& expl, const std::string& sugg, SourceLoc loc);

}} // namespace nova::sql
