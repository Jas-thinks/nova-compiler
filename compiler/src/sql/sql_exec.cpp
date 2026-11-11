// NOVA-SQL executor (materializing, operator-at-a-time) and the script driver that produces the JSON report.
#include <algorithm>
#include <chrono>
#include <numeric>
#include <stdexcept>
#include "nova/sql.hpp"
#include "sql_internal.hpp"

namespace nova { namespace sql {

namespace {

struct Acc { int64_t cnt = 0; bool any = false; int64_t isum = 0; double fsum = 0; SVal best; };

constexpr int64_t kMaxJoinPairs = 5000000;   // protects the service from accidental/hostile cross joins
constexpr size_t kMaxTableRows = 100000;
constexpr size_t kMaxResultRowsReported = 5000;

class Exec {
public:
    Exec(Catalog& c, std::deque<SCode>& code) : cat_(c), code_(code) {}
    int64_t scanned = 0, joinPairs = 0, hashProbes = 0, sortedRows = 0, groups = 0;

    std::vector<Row> run(Plan& p) {
        std::vector<Row> out;
        switch (p.k) {
            case Plan::Scan: { Table* t = cat_.find(p.table); out = t->rows; scanned += static_cast<int64_t>(out.size()); break; }
            case Plan::OneRow: out.push_back(Row()); break;
            case Plan::Filter: {
                auto in = run(*p.kids[0]); const SCode& c = comp(*p.pred);
                for (auto& r : in) if (truthy(runCode(c, r))) out.push_back(std::move(r));
                break;
            }
            case Plan::Join: out = join(p); break;
            case Plan::Aggregate: out = aggregate(p); break;
            case Plan::Project: {
                auto in = run(*p.kids[0]);
                std::vector<const SCode*> cs; for (auto& e : p.exprs) cs.push_back(&comp(*e));
                for (auto& r : in) { Row o; o.reserve(cs.size()); for (auto* c : cs) o.push_back(runCode(*c, r)); out.push_back(std::move(o)); }
                break;
            }
            case Plan::Distinct: { auto in = run(*p.kids[0]); std::set<Row, RowLess> seen; for (auto& r : in) if (seen.insert(r).second) out.push_back(std::move(r)); break; }
            case Plan::Sort: out = sort(p); break;
            case Plan::Limit: {
                auto in = run(*p.kids[0]);
                size_t start = static_cast<size_t>(std::min<int64_t>(p.offset, static_cast<int64_t>(in.size())));
                size_t end = p.limit >= 0 ? static_cast<size_t>(std::min<int64_t>(static_cast<int64_t>(start) + p.limit, static_cast<int64_t>(in.size()))) : in.size();
                for (size_t k = start; k < end; ++k) { Row r = std::move(in[k]); if (p.keep >= 0 && static_cast<int>(r.size()) > p.keep) r.resize(static_cast<size_t>(p.keep)); out.push_back(std::move(r)); }
                break;
            }
        }
        p.rowsOut = static_cast<int64_t>(out.size());
        return out;
    }

private:
    Catalog& cat_; std::deque<SCode>& code_;

    const SCode& comp(const Expr& e) { code_.push_back(compileExpr(e)); return code_.back(); }

    std::vector<Row> join(Plan& p) {
        auto L = run(*p.kids[0]); auto R = run(*p.kids[1]);
        std::vector<Row> out;
        const int rw = p.kids[1]->width;
        const SCode* cond = p.pred ? &comp(*p.pred) : nullptr;
        auto concat = [&](const Row& a, const Row& b) { Row r; r.reserve(a.size() + b.size()); r.insert(r.end(), a.begin(), a.end()); r.insert(r.end(), b.begin(), b.end()); return r; };
        std::map<Row, std::vector<size_t>, RowLess> table;
        if (p.hash) for (size_t k = 0; k < R.size(); ++k) { const SVal& key = R[k][static_cast<size_t>(p.rslot)]; if (!key.isNull()) table[Row{key}].push_back(k); }
        std::vector<size_t> all; if (!p.hash) { all.resize(R.size()); std::iota(all.begin(), all.end(), 0); }
        for (auto& l : L) {
            bool matched = false;
            const std::vector<size_t>* cand = &all;
            static const std::vector<size_t> none;
            if (p.hash) {
                ++hashProbes; const SVal& key = l[static_cast<size_t>(p.lslot)];
                if (key.isNull()) cand = &none;
                else { auto it = table.find(Row{key}); cand = it == table.end() ? &none : &it->second; }
            }
            for (size_t ri : *cand) {
                if (++joinPairs > kMaxJoinPairs) throw std::runtime_error("query exceeds the join work limit (" + std::to_string(kMaxJoinPairs) + " row pairs)");
                Row c = concat(l, R[ri]);
                if (!cond || truthy(runCode(*cond, c))) { matched = true; out.push_back(std::move(c)); }
            }
            if (!matched && p.jk == JoinClause::Left) { Row c = l; c.resize(l.size() + static_cast<size_t>(rw)); out.push_back(std::move(c)); }
        }
        return out;
    }

    std::vector<Row> aggregate(Plan& p) {
        auto in = run(*p.kids[0]);
        std::vector<const SCode*> keyCode; for (auto& e : p.exprs) keyCode.push_back(&comp(*e));
        std::vector<const SCode*> argCode; for (auto& a : p.aggs) argCode.push_back(a.arg ? &comp(*a.arg) : nullptr);
        std::map<Row, std::vector<Acc>, RowLess> groupsMap;
        if (p.exprs.empty()) groupsMap[Row()] = std::vector<Acc>(p.aggs.size());   // aggregate without GROUP BY always yields one row
        for (auto& r : in) {
            Row key; for (auto* c : keyCode) key.push_back(runCode(*c, r));
            auto& accs = groupsMap.try_emplace(std::move(key), std::vector<Acc>(p.aggs.size())).first->second;
            for (size_t a = 0; a < p.aggs.size(); ++a) {
                const AggSpec& sp = p.aggs[a]; Acc& ac = accs[a];
                if (sp.star) { ++ac.cnt; continue; }
                SVal v = runCode(*argCode[a], r);
                if (v.isNull()) continue;
                ++ac.cnt;
                if (sp.fn == "SUM" || sp.fn == "AVG") {
                    ac.any = true;
                    if (v.t == ST::Int) ac.isum = static_cast<int64_t>(static_cast<uint64_t>(ac.isum) + static_cast<uint64_t>(v.i));
                    ac.fsum += v.num();
                } else if (sp.fn == "MIN" || sp.fn == "MAX") {
                    if (!ac.any) { ac.best = v; ac.any = true; }
                    else { int c = cmpTotal(v, ac.best); if ((sp.fn == "MIN" && c < 0) || (sp.fn == "MAX" && c > 0)) ac.best = v; }
                }
            }
        }
        std::vector<Row> out;
        for (auto& [key, accs] : groupsMap) {
            Row r = key;
            for (size_t a = 0; a < p.aggs.size(); ++a) {
                const AggSpec& sp = p.aggs[a]; const Acc& ac = accs[a];
                if (sp.fn == "COUNT") r.push_back(SVal::Int(ac.cnt));
                else if (sp.fn == "SUM") r.push_back(!ac.any ? SVal::null() : sp.type == ST::Float ? SVal::Float(ac.fsum) : SVal::Int(ac.isum));
                else if (sp.fn == "AVG") r.push_back(ac.cnt == 0 ? SVal::null() : SVal::Float(ac.fsum / static_cast<double>(ac.cnt)));
                else r.push_back(ac.any ? ac.best : SVal::null());
            }
            out.push_back(std::move(r));
        }
        groups += static_cast<int64_t>(out.size());
        return out;
    }

    std::vector<Row> sort(Plan& p) {
        auto in = run(*p.kids[0]);
        sortedRows += static_cast<int64_t>(in.size());
        std::vector<size_t> idx(in.size()); std::iota(idx.begin(), idx.end(), 0);
        auto less = [&](size_t a, size_t b) {
            for (auto& k : p.keys) { int c = cmpTotal(in[a][static_cast<size_t>(k.slot)], in[b][static_cast<size_t>(k.slot)]); if (c) return k.desc ? c > 0 : c < 0; }
            return a < b;     // stable tie-break keeps results deterministic (and makes partial_sort equivalent to a full sort)
        };
        if (p.topN >= 0 && static_cast<size_t>(p.topN) < idx.size()) { std::partial_sort(idx.begin(), idx.begin() + p.topN, idx.end(), less); idx.resize(static_cast<size_t>(p.topN)); }
        else std::sort(idx.begin(), idx.end(), less);
        std::vector<Row> out; out.reserve(idx.size());
        for (size_t i : idx) out.push_back(std::move(in[i]));
        return out;
    }
};

Json codeJson(const std::deque<SCode>& v) {
    Json a = Json::array();
    for (auto& c : v) { Json o = Json::object(); Json lines = Json::array(); for (auto& l : c.disasm()) lines.push(l); o.set("source", c.source).set("code", lines); a.push(o); }
    return a;
}

const char* kindName(Stmt::K k) { switch (k) { case Stmt::Create: return "CREATE TABLE"; case Stmt::Insert: return "INSERT"; case Stmt::Select_: return "SELECT"; case Stmt::Update: return "UPDATE"; case Stmt::Delete: return "DELETE"; case Stmt::Drop: return "DROP TABLE"; } return "?"; }

Diagnostic runtimeErr(const std::string& cat, const std::string& code, const std::string& msg, const std::string& expl, const std::string& sugg, SourceLoc loc) {
    Diagnostic d = semErr(cat, code, msg, expl, sugg, loc); d.phase = "runtime"; return d;
}

// Coerce a value for storage in a column; returns false and fills `d` on a type error.
bool coerce(SVal& v, const ColDef& c, SourceLoc loc, Diagnostic& d) {
    if (v.isNull()) return true;
    if (c.type == ST::Float && v.t == ST::Int) { v = SVal::Float(static_cast<double>(v.i)); return true; }
    if (v.t == c.type) return true;
    d = semErr("Type mismatch", "E-SQL-103", std::string("Cannot store a ") + stName(v.t) + " value in column '" + c.name + "' of type " + stName(c.type), "NOVA-SQL does no implicit conversion except INT -> FLOAT.", "Provide a value of type " + std::string(stName(c.type)) + ".", loc);
    return false;
}

bool checkConstraints(const Table& t, const std::vector<Row>& rows, Diagnostic& d, SourceLoc loc) {
    for (size_t ci = 0; ci < t.cols.size(); ++ci) {
        const ColDef& c = t.cols[ci];
        if (c.notNull) for (auto& r : rows) if (r[ci].isNull()) { d = runtimeErr("Constraint violation", "E-SQL-111", "NOT NULL constraint failed: " + t.name + "." + c.name, "Column '" + c.name + "' does not accept NULL.", "Provide a value.", loc); return false; }
        if (c.primaryKey) {
            std::set<Row, RowLess> seen;
            for (auto& r : rows) if (!seen.insert(Row{r[ci]}).second) { d = runtimeErr("Constraint violation", "E-SQL-111", "UNIQUE constraint failed: " + t.name + "." + c.name, "PRIMARY KEY values must be unique.", "Use a different key value.", loc); return false; }
        }
    }
    return true;
}

} // namespace

std::vector<Row> executePlan(Plan& plan, Catalog& cat, std::deque<SCode>& compiled, Json& stats) {
    Exec ex(cat, compiled);
    auto rows = ex.run(plan);
    stats.set("rowsScanned", static_cast<long long>(ex.scanned)).set("joinPairsCompared", static_cast<long long>(ex.joinPairs)).set("hashProbes", static_cast<long long>(ex.hashProbes))
         .set("rowsSorted", static_cast<long long>(ex.sortedRows)).set("groups", static_cast<long long>(ex.groups)).set("rowsReturned", static_cast<long long>(rows.size()));
    return rows;
}

}} // namespace nova::sql

namespace nova {

using namespace sql;

Json runSqlScript(const std::string& script) {
    auto t0 = std::chrono::steady_clock::now();
    Json out = Json::object();
    ParseOutput po = parseScript(script);
    out.set("tokenCount", static_cast<long long>(po.tokenCount));
    Json toks = Json::array();
    for (size_t k = 0; k < po.tokens.size() && k < 400; ++k) {
        const SToken& t = po.tokens[k]; if (t.k == SToken::Eof) continue;
        Json o = Json::object(); static const char* kn[] = {"IDENTIFIER", "INT", "FLOAT", "STRING", "OPERATOR", "EOF"};
        o.set("category", kn[t.k]).set("lexeme", t.text).set("loc", t.loc.toJson()); toks.push(o);
    }
    out.set("tokens", toks);
    DiagnosticList all = po.diags;
    Json stmts = Json::array();
    bool success = !po.diags.hasErrors();
    Catalog cat;
    bool stopped = false;
    if (success) for (size_t si = 0; si < po.stmts.size(); ++si) {
        Stmt& s = po.stmts[si];
        Json js = Json::object();
        js.set("index", static_cast<int>(si)).set("kind", kindName(s.kind)).set("text", s.text).set("ast", s.sexpr());
        if (stopped) { js.set("ok", false).set("skipped", true); stmts.push(js); continue; }
        auto s0 = std::chrono::steady_clock::now();
        DiagnosticList dl; std::deque<SCode> compiled; Json stats = Json::object(); bool ok = true;
        Json cols = Json::array(), types = Json::array(), rowsJ = Json::array(); long long affected = 0;
        switch (s.kind) {
            case Stmt::Create: {
                if (cat.find(s.table)) { if (!s.ifFlag) { dl.add(semErr("Duplicate table", "E-SQL-108", "Table '" + s.table + "' already exists", "Table names must be unique.", "Use CREATE TABLE IF NOT EXISTS or DROP TABLE first.", s.loc)); ok = false; } break; }
                std::set<std::string> names;
                for (auto& c : s.cols) if (!names.insert(c.name).second) { dl.add(semErr("Duplicate column", "E-SQL-109", "Column '" + c.name + "' is declared twice", "Column names must be unique within a table.", "Rename one of them.", c.loc)); ok = false; }
                if (!ok) break;
                Table t; t.name = s.table; t.cols = s.cols; cat.tables[s.table] = std::move(t);
                break;
            }
            case Stmt::Drop: {
                if (!cat.find(s.table)) { if (!s.ifFlag) { dl.add(semErr("Unknown table", "E-SQL-100", "Table '" + s.table + "' does not exist", "Nothing to drop.", "Use DROP TABLE IF EXISTS.", s.loc)); ok = false; } break; }
                cat.tables.erase(s.table); break;
            }
            case Stmt::Insert: {
                Table* t = cat.find(s.table);
                if (!t) { std::vector<std::string> ns; for (auto& kv : cat.tables) ns.push_back(kv.first); std::string sg = closestMatch(s.table, ns); dl.add(semErr("Unknown table", "E-SQL-100", "Table '" + s.table + "' does not exist", "Create it first.", sg.empty() ? "CREATE TABLE ...;" : "Did you mean '" + sg + "'?", s.loc)); ok = false; break; }
                std::vector<int> map;
                if (s.insCols.empty()) for (size_t k = 0; k < t->cols.size(); ++k) map.push_back(static_cast<int>(k));
                else for (auto& n : s.insCols) {
                    int ci = t->colIndex(n);
                    if (ci < 0) { std::vector<std::string> ns; for (auto& c : t->cols) ns.push_back(c.name); std::string sg = closestMatch(n, ns); dl.add(semErr("Unknown column", "E-SQL-101", "Table '" + t->name + "' has no column '" + n + "'", "INSERT column names must exist in the table.", sg.empty() ? "Check the table definition." : "Did you mean '" + sg + "'?", s.loc)); ok = false; break; }
                    if (std::find(map.begin(), map.end(), ci) != map.end()) { dl.add(semErr("Duplicate column", "E-SQL-109", "Column '" + n + "' is listed twice", "Each column can be given once.", "Remove the duplicate.", s.loc)); ok = false; break; }
                    map.push_back(ci);
                }
                if (!ok) break;
                std::vector<Row> staged;
                for (auto& row : s.rows) {
                    if (row.size() != map.size()) { dl.add(semErr("Wrong value count", "E-SQL-110", "Expected " + std::to_string(map.size()) + " value(s) but got " + std::to_string(row.size()), "Each VALUES row needs one value per target column.", "Match the number of columns.", row.empty() ? s.loc : row[0]->loc)); ok = false; break; }
                    Row r(t->cols.size());
                    for (size_t k = 0; k < row.size() && ok; ++k) {
                        if (!bindTableExpr(*row[k], nullptr, dl)) { ok = false; break; }
                        compiled.push_back(compileExpr(*row[k]));
                        SVal v = runCode(compiled.back(), Row());
                        Diagnostic d; if (!coerce(v, t->cols[static_cast<size_t>(map[k])], row[k]->loc, d)) { dl.add(d); ok = false; break; }
                        r[static_cast<size_t>(map[k])] = std::move(v);
                    }
                    if (!ok) break;
                    staged.push_back(std::move(r));
                }
                if (!ok) break;
                if (t->rows.size() + staged.size() > kMaxTableRows) { dl.add(runtimeErr("Resource limit", "E-SQL-113", "Table '" + t->name + "' would exceed " + std::to_string(kMaxTableRows) + " rows", "The playground caps table size.", "Insert fewer rows.", s.loc)); ok = false; break; }
                std::vector<Row> merged = t->rows; for (auto& r : staged) merged.push_back(r);
                Diagnostic d; if (!checkConstraints(*t, merged, d, s.loc)) { dl.add(d); ok = false; break; }
                t->rows = std::move(merged); affected = static_cast<long long>(staged.size());
                break;
            }
            case Stmt::Update: case Stmt::Delete: {
                Table* t = cat.find(s.table);
                if (!t) { std::vector<std::string> ns; for (auto& kv : cat.tables) ns.push_back(kv.first); std::string sg = closestMatch(s.table, ns); dl.add(semErr("Unknown table", "E-SQL-100", "Table '" + s.table + "' does not exist", "Create it first.", sg.empty() ? "CREATE TABLE ...;" : "Did you mean '" + sg + "'?", s.loc)); ok = false; break; }
                if (s.where) { if (!bindTableExpr(*s.where, t, dl)) { ok = false; break; } if (s.where->type == ST::Text) { dl.add(semErr("Type mismatch", "E-SQL-103", "WHERE condition must be boolean, not TEXT", "A WHERE clause filters rows by a truth value.", "Write a comparison.", s.where->loc)); ok = false; break; } compiled.push_back(compileExpr(*s.where)); }
                const SCode* wc = s.where ? &compiled.back() : nullptr;
                if (s.kind == Stmt::Delete) {
                    std::vector<Row> keep; for (auto& r : t->rows) { if (!wc || truthy(runCode(*wc, r))) ++affected; else keep.push_back(r); }
                    t->rows = std::move(keep); break;
                }
                std::vector<std::pair<int, const SCode*>> setCode;
                for (auto& st : s.sets) {
                    int ci = t->colIndex(st.first);
                    if (ci < 0) { std::vector<std::string> ns; for (auto& c : t->cols) ns.push_back(c.name); std::string sg = closestMatch(st.first, ns); dl.add(semErr("Unknown column", "E-SQL-101", "Table '" + t->name + "' has no column '" + st.first + "'", "SET targets must be columns of the table.", sg.empty() ? "Check the table definition." : "Did you mean '" + sg + "'?", s.loc)); ok = false; break; }
                    if (!bindTableExpr(*st.second, t, dl)) { ok = false; break; }
                    ST vt = st.second->type; ST ct = t->cols[static_cast<size_t>(ci)].type;
                    if (vt != ST::Null && vt != ct && !(ct == ST::Float && vt == ST::Int)) { dl.add(semErr("Type mismatch", "E-SQL-103", std::string("Cannot assign ") + stName(vt) + " to column '" + st.first + "' of type " + stName(ct), "NOVA-SQL does no implicit conversion except INT -> FLOAT.", "Assign a value of the column's type.", st.second->loc)); ok = false; break; }
                    compiled.push_back(compileExpr(*st.second)); setCode.push_back({ci, nullptr});
                }
                if (!ok) break;
                { size_t base = compiled.size() - setCode.size(); for (size_t k = 0; k < setCode.size(); ++k) setCode[k].second = &compiled[base + k]; }
                std::vector<Row> next = t->rows;
                for (size_t ri = 0; ri < t->rows.size(); ++ri) {
                    if (wc && !truthy(runCode(*wc, t->rows[ri]))) continue;
                    ++affected;
                    for (auto& sc : setCode) { SVal v = runCode(*sc.second, t->rows[ri]); Diagnostic d; if (!coerce(v, t->cols[static_cast<size_t>(sc.first)], s.loc, d)) { dl.add(d); ok = false; break; } next[ri][static_cast<size_t>(sc.first)] = std::move(v); }
                    if (!ok) break;
                }
                if (!ok) break;
                Diagnostic d; if (!checkConstraints(*t, next, d, s.loc)) { dl.add(d); ok = false; break; }
                t->rows = std::move(next);
                break;
            }
            case Stmt::Select_: {
                BindResult br = planSelect(s.sel, cat);
                for (auto& d : br.diags.items()) dl.add(d);
                if (!br.plan) { ok = false; break; }
                Json plan = Json::object();
                plan.set("original", br.plan->text(0, false));
                auto history = optimizePlan(br.plan);
                Json hj = Json::array();
                for (auto& h : history) { Json o = Json::object(); o.set("rule", h.rule).set("description", h.description).set("before", h.before).set("after", h.after); hj.push(o); }
                std::vector<Row> rows;
                try { rows = executePlan(*br.plan, cat, compiled, stats); }
                catch (const std::runtime_error& ex) { dl.add(runtimeErr("Resource limit", "E-SQL-113", ex.what(), "The engine stops queries that would do an unreasonable amount of work.", "Add a join condition or filter.", s.loc)); ok = false; break; }
                plan.set("optimized", br.plan->text(0, false)).set("optimizedWithStats", br.plan->text(0, true)).set("tree", br.plan->toJson()).set("history", hj);
                js.set("plan", plan);
                for (auto& n : br.names) cols.push(n);
                for (auto t : br.types) types.push(stName(t));
                for (auto& r : rows) { if (rowsJ.size() >= kMaxResultRowsReported) break; Json jr = Json::array(); for (auto& v : r) jr.push(v.toJson()); rowsJ.push(jr); }
                affected = static_cast<long long>(rows.size());
                js.set("truncated", rows.size() > kMaxResultRowsReported);
                break;
            }
        }
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s0).count();
        js.set("ok", ok).set("columns", cols).set("types", types).set("rows", rowsJ).set("rowCount", static_cast<long long>(rowsJ.size()))
          .set("rowsAffected", affected).set("bytecode", codeJson(compiled)).set("stats", stats).set("elapsedMs", ms).set("diagnostics", dl.toJson());
        for (auto& d : dl.items()) all.add(d);
        if (!ok) success = false;      // later statements still run (like psql / sqlite3), each reporting its own result
        stmts.push(js);
    }
    Json cj = Json::array();
    for (auto& kv : cat.tables) {
        Json t = Json::object(); Json cs = Json::array();
        for (auto& c : kv.second.cols) { Json o = Json::object(); o.set("name", c.name).set("type", stName(c.type)).set("notNull", c.notNull).set("primaryKey", c.primaryKey); cs.push(o); }
        t.set("name", kv.first).set("columns", cs).set("rows", static_cast<long long>(kv.second.rows.size())); cj.push(t);
    }
    out.set("success", success).set("statements", stmts).set("catalog", cj).set("diagnostics", all.toJson())
       .set("errorCount", static_cast<long long>(all.errorCount()))
       .set("elapsedMs", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    return out;
}

} // namespace nova
