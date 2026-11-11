// NOVA-SQL binder / type checker, logical planner, rule-based optimizer and plan rendering.
#include <algorithm>
#include <functional>
#include <sstream>
#include "sql_internal.hpp"

namespace nova { namespace sql {

Diagnostic semErr(const std::string& cat, const std::string& code, const std::string& msg, const std::string& expl, const std::string& sugg, SourceLoc loc) {
    Diagnostic d; d.phase = "semantic"; d.category = cat; d.code = code; d.message = msg; d.explanation = expl; d.suggestion = sugg; d.loc = loc; return d;
}

namespace {

bool isNum(ST t) { return t == ST::Int || t == ST::Float; }
bool compat(ST a, ST b) { return a == ST::Null || b == ST::Null || (isNum(a) && isNum(b)) || (a == ST::Text && b == ST::Text); }
ST numResult(ST a, ST b) { return a == ST::Float || b == ST::Float ? ST::Float : (a == ST::Null && b == ST::Null) ? ST::Null : ST::Int; }

struct ScopeCol { std::string alias, name; ST type; int slot; };
struct Scope { std::vector<ScopeCol> cols; };

class Binder {
public:
    Binder(const Scope& s, DiagnosticList& d) : sc_(s), d_(d) {}
    bool ok = true;

    ST bind(Expr& e, bool allowAgg, bool inAgg = false) { return go(e, allowAgg, inAgg); }

private:
    const Scope& sc_; DiagnosticList& d_;

    void err(Diagnostic d) { ok = false; d_.add(std::move(d)); }
    void typeErr(Expr& e, const std::string& msg, const std::string& expl, const std::string& sugg) { err(semErr("Type mismatch", "E-SQL-103", msg, expl, sugg, e.loc)); }

    ST go(Expr& e, bool allowAgg, bool inAgg) { ST t = goKind(e, allowAgg, inAgg); e.type = t; return t; }   // every node records its static type

    ST goKind(Expr& e, bool allowAgg, bool inAgg) {
        switch (e.kind) {
            case Expr::Lit: return e.lit.t;
            case Expr::Star: return ST::Null;
            case Expr::Col: return bindCol(e);
            case Expr::Un: {
                ST a = go(*e.args[0], allowAgg, inAgg);
                if (e.op == "-") { if (a == ST::Null || isNum(a)) return a; typeErr(e, std::string("Unary minus cannot be applied to ") + stName(a), "Unary minus needs a numeric operand.", "Use a numeric expression."); return ST::Null; }
                if (a == ST::Text) { typeErr(e, "NOT cannot be applied to TEXT", "NOT needs a boolean (numeric) operand.", "Compare the value first, e.g. NOT (x = 'a')."); return ST::Null; }
                return ST::Int;
            }
            case Expr::Bin: {
                ST a = go(*e.args[0], allowAgg, inAgg), b = go(*e.args[1], allowAgg, inAgg);
                const std::string& op = e.op;
                if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
                    if ((a != ST::Null && !isNum(a)) || (b != ST::Null && !isNum(b))) { typeErr(e, "Operator '" + op + "' cannot be applied to " + stName(a) + " and " + stName(b), "Arithmetic needs numeric operands.", op == "+" ? "Use || to concatenate text." : "Use numeric operands."); return ST::Null; }
                    if (op == "%" && (a == ST::Float || b == ST::Float)) { typeErr(e, "Operator '%' is defined for INT operands only", "Modulo works on integers.", "Cast or restructure the expression."); return ST::Null; }
                    return numResult(a, b);
                }
                if (op == "||") {
                    if ((a != ST::Null && a != ST::Text) || (b != ST::Null && b != ST::Text)) { typeErr(e, std::string("Operator '||' cannot be applied to ") + stName(a) + " and " + stName(b), "Concatenation joins two TEXT values.", "NOVA-SQL has no implicit conversion to text."); return ST::Null; }
                    return ST::Text;
                }
                if (op == "AND" || op == "OR") {
                    if (a == ST::Text || b == ST::Text) { typeErr(e, op + " needs boolean (numeric) operands", "Logical operators work on truth values.", "Compare the text first, e.g. name = 'x'."); return ST::Null; }
                    return ST::Int;
                }
                if (!compat(a, b)) { typeErr(e, std::string("Cannot compare ") + stName(a) + " with " + stName(b), "Comparisons need two numbers or two texts; NOVA-SQL does no implicit conversion between them.", "Compare values of the same type."); return ST::Null; }
                return ST::Int;
            }
            case Expr::IsNull: go(*e.args[0], allowAgg, inAgg); return ST::Int;
            case Expr::Like: {
                ST a = go(*e.args[0], allowAgg, inAgg), b = go(*e.args[1], allowAgg, inAgg);
                if ((a != ST::Null && a != ST::Text) || (b != ST::Null && b != ST::Text)) { typeErr(e, "LIKE needs TEXT operands", "LIKE matches a text against a pattern.", "Use TEXT values."); return ST::Null; }
                return ST::Int;
            }
            case Expr::In: case Expr::Between: {
                ST a = go(*e.args[0], allowAgg, inAgg);
                for (size_t k = 1; k < e.args.size(); ++k) {
                    ST b = go(*e.args[k], allowAgg, inAgg);
                    if (!compat(a, b)) { typeErr(*e.args[k], std::string("Cannot compare ") + stName(a) + " with " + stName(b), "All values compared against the left side must have a compatible type.", "Use values of the same type."); return ST::Null; }
                }
                return ST::Int;
            }
            case Expr::Case: {
                const size_t n = e.args.size() - (e.op == "ELSE" ? 1 : 0);
                ST r = ST::Null;
                auto unify = [&](Expr& v, ST t) {
                    if (t == ST::Null) return;
                    if (r == ST::Null) r = t;
                    else if (!compat(r, t)) typeErr(v, std::string("CASE branches must have compatible types, got ") + stName(r) + " and " + stName(t), "Every THEN/ELSE value of a CASE must be numeric, or all TEXT.", "Make the branches the same type.");
                    else r = numResult(r, t) == ST::Float ? ST::Float : r;
                };
                for (size_t k = 0; k < n; k += 2) {
                    ST c = go(*e.args[k], allowAgg, inAgg);
                    if (c == ST::Text) typeErr(*e.args[k], "CASE WHEN condition must be boolean, not TEXT", "WHEN takes a condition such as x > 0.", "Write a comparison.");
                    unify(*e.args[k + 1], go(*e.args[k + 1], allowAgg, inAgg));
                }
                if (e.op == "ELSE") unify(*e.args.back(), go(*e.args.back(), allowAgg, inAgg));
                return r;
            }
            case Expr::Func: return bindFunc(e, allowAgg, inAgg);
            case Expr::Agg: {
                if (!allowAgg) { err(semErr("Invalid aggregate", "E-SQL-106", "Aggregate function " + e.name + " is not allowed here", "Aggregates are only allowed in the SELECT list, HAVING and ORDER BY — not in WHERE, ON or GROUP BY.", "Move the condition to HAVING.", e.loc)); return ST::Null; }
                if (inAgg) { err(semErr("Invalid aggregate", "E-SQL-106", "Aggregate functions cannot be nested", "An aggregate argument cannot itself contain an aggregate.", "Restructure with a subquery-free formulation.", e.loc)); return ST::Null; }
                if (e.countStar) return ST::Int;
                if (e.args.size() != 1) { err(semErr("Wrong argument count", "E-SQL-105", e.name + " expects exactly 1 argument", "Aggregates take one expression.", "Pass one argument.", e.loc)); return ST::Null; }
                ST a = go(*e.args[0], false, true);
                if (e.name == "COUNT") return ST::Int;
                if ((e.name == "SUM" || e.name == "AVG") && a == ST::Text) { typeErr(e, e.name + " cannot be applied to TEXT", e.name + " needs numeric input.", "Use COUNT, MIN or MAX on text."); return ST::Null; }
                if (e.name == "AVG") return ST::Float;
                return a;
            }
        }
        return ST::Null;
    }

    ST bindCol(Expr& e) {
        std::vector<const ScopeCol*> hits;
        bool aliasKnown = e.table.empty();
        for (auto& c : sc_.cols) {
            if (!e.table.empty() && c.alias == e.table) aliasKnown = true;
            if (c.name == e.name && (e.table.empty() || c.alias == e.table)) hits.push_back(&c);
        }
        if (hits.size() > 1) { err(semErr("Ambiguous column", "E-SQL-102", "Column '" + e.name + "' is ambiguous", "More than one table in FROM has a column with this name.", "Qualify it, e.g. " + hits[0]->alias + "." + e.name + ".", e.loc)); return ST::Null; }
        if (hits.empty()) {
            std::vector<std::string> names; for (auto& c : sc_.cols) names.push_back(c.name);
            if (!aliasKnown) { std::vector<std::string> as; for (auto& c : sc_.cols) as.push_back(c.alias); std::string sg = closestMatch(e.table, as); err(semErr("Unknown table", "E-SQL-100", "Unknown table or alias '" + e.table + "'", "Qualified column names must use a table or alias from the FROM clause.", sg.empty() ? "Check the FROM clause." : "Did you mean '" + sg + "'?", e.loc)); return ST::Null; }
            std::string sg = closestMatch(e.name, names);
            err(semErr("Unknown column", "E-SQL-101", "Unknown column '" + (e.table.empty() ? e.name : e.table + "." + e.name) + "'", "No table in scope has this column.", sg.empty() ? "Check the column names of the table." : "Did you mean '" + sg + "'?", e.loc));
            return ST::Null;
        }
        e.slot = hits[0]->slot; e.table = hits[0]->alias;
        return hits[0]->type;
    }

    ST bindFunc(Expr& e, bool allowAgg, bool inAgg) {
        static const std::map<std::string, int> arity = {{"UPPER", 1}, {"LOWER", 1}, {"LENGTH", 1}, {"ABS", 1}, {"COALESCE", -1}};
        auto it = arity.find(e.name);
        if (it == arity.end()) {
            std::vector<std::string> known = {"UPPER", "LOWER", "LENGTH", "ABS", "COALESCE", "COUNT", "SUM", "AVG", "MIN", "MAX"};
            std::string sg = closestMatch(e.name, known);
            err(semErr("Unknown function", "E-SQL-104", "Unknown function " + e.name, "NOVA-SQL supports UPPER, LOWER, LENGTH, ABS, COALESCE and the aggregates COUNT, SUM, AVG, MIN, MAX.", sg.empty() ? "Check the function name." : "Did you mean " + sg + "?", e.loc));
            return ST::Null;
        }
        std::vector<ST> at; for (auto& a : e.args) at.push_back(go(*a, allowAgg, inAgg));
        if (it->second >= 0 && static_cast<int>(e.args.size()) != it->second) { err(semErr("Wrong argument count", "E-SQL-105", e.name + " expects " + std::to_string(it->second) + " argument(s) but " + std::to_string(e.args.size()) + " were given", "Check the function signature.", "Pass the right number of arguments.", e.loc)); return ST::Null; }
        if (e.name == "COALESCE") {
            if (e.args.empty()) { err(semErr("Wrong argument count", "E-SQL-105", "COALESCE needs at least 1 argument", "COALESCE returns its first non-NULL argument.", "Add an argument.", e.loc)); return ST::Null; }
            ST r = ST::Null;
            for (size_t k = 0; k < at.size(); ++k) {
                if (at[k] == ST::Null) continue;
                if (r == ST::Null) r = at[k];
                else if (!compat(r, at[k])) { typeErr(*e.args[k], std::string("COALESCE arguments must have compatible types, got ") + stName(r) + " and " + stName(at[k]), "All arguments must be numeric or all TEXT.", "Use values of the same type."); return ST::Null; }
                else r = numResult(r, at[k]) == ST::Float ? ST::Float : r;
            }
            return r;
        }
        if (e.name == "UPPER" || e.name == "LOWER") { if (at[0] != ST::Text && at[0] != ST::Null) { typeErr(e, e.name + " needs TEXT", "String functions work on text.", "Pass a TEXT value."); return ST::Null; } return ST::Text; }
        if (e.name == "LENGTH") { if (at[0] != ST::Text && at[0] != ST::Null) { typeErr(e, "LENGTH needs TEXT", "LENGTH counts characters of a text.", "Pass a TEXT value."); return ST::Null; } return ST::Int; }
        if (!isNum(at[0]) && at[0] != ST::Null) { typeErr(e, "ABS needs a number", "ABS works on numeric values.", "Pass a number."); return ST::Null; }
        return at[0];
    }
};

// ---------------------------------------------------------------- plan helpers
int widthOf(const Plan& p) { return p.width; }
void collectSlots(const Expr& e, std::set<int>& out) { if (e.kind == Expr::Col && e.slot >= 0) out.insert(e.slot); for (auto& a : e.args) collectSlots(*a, out); }
void shiftSlots(Expr& e, int delta) { if (e.kind == Expr::Col && e.slot >= 0) e.slot += delta; for (auto& a : e.args) shiftSlots(*a, delta); }
void splitAnd(ExprP e, std::vector<ExprP>& out) {
    if (e && e->kind == Expr::Bin && e->op == "AND") { splitAnd(std::move(e->args[0]), out); splitAnd(std::move(e->args[1]), out); }
    else if (e) out.push_back(std::move(e));
}
ExprP joinAnd(std::vector<ExprP>& v) {
    ExprP r;
    for (auto& e : v) { if (!r) r = std::move(e); else { auto b = std::make_unique<Expr>(); b->kind = Expr::Bin; b->op = "AND"; b->type = ST::Int; b->loc = r->loc; b->args.push_back(std::move(r)); b->args.push_back(std::move(e)); r = std::move(b); } }
    v.clear(); return r;
}
PlanP mkPlan(Plan::K k) { auto p = std::make_unique<Plan>(); p->k = k; return p; }

} // namespace

bool bindTableExpr(Expr& e, const Table* t, DiagnosticList& d) {
    Scope sc;
    if (t) for (size_t k = 0; k < t->cols.size(); ++k) sc.cols.push_back({t->name, t->cols[k].name, t->cols[k].type, static_cast<int>(k)});
    Binder b(sc, d); b.bind(e, false);
    return b.ok;
}

// ================================================================== planner
BindResult planSelect(const Select& selIn, Catalog& cat) {
    BindResult res;
    DiagnosticList& dl = res.diags;
    auto fail = [&]() { res.plan.reset(); return std::move(res); };

    // ---- FROM / JOIN
    Scope scope; PlanP input; std::set<std::string> aliases;
    auto addTable = [&](const TableRef& tr) -> PlanP {
        Table* t = cat.find(tr.name);
        if (!t) {
            std::vector<std::string> names; for (auto& kv : cat.tables) names.push_back(kv.first);
            std::string sg = closestMatch(tr.name, names);
            dl.add(semErr("Unknown table", "E-SQL-100", "Table '" + tr.name + "' does not exist", "The table must be created with CREATE TABLE before it is used.", sg.empty() ? "CREATE TABLE " + tr.name + " (...);" : "Did you mean '" + sg + "'?", tr.loc));
            return nullptr;
        }
        if (!aliases.insert(tr.alias).second) { dl.add(semErr("Duplicate alias", "E-SQL-108", "Table name or alias '" + tr.alias + "' is used twice in FROM", "Each table in a FROM clause needs a unique name.", "Give one of them an alias: " + tr.name + " AS x.", tr.loc)); return nullptr; }
        auto p = mkPlan(Plan::Scan); p->table = tr.name; p->alias = tr.alias; p->width = static_cast<int>(t->cols.size());
        int base = static_cast<int>(scope.cols.size());
        for (size_t k = 0; k < t->cols.size(); ++k) scope.cols.push_back({tr.alias, t->cols[k].name, t->cols[k].type, base + static_cast<int>(k)});
        return p;
    };
    if (!selIn.hasFrom) { input = mkPlan(Plan::OneRow); input->width = 0; }
    else {
        input = addTable(selIn.from);
        if (!input) return fail();
        for (auto& j : selIn.joins) {
            PlanP right = addTable(j.t);
            if (!right) return fail();
            auto jn = mkPlan(Plan::Join); jn->jk = j.kind;
            jn->width = input->width + right->width;
            if (j.on) {
                jn->pred = j.on->clone();
                Binder b(scope, dl); ST t = b.bind(*jn->pred, false);
                if (!b.ok) return fail();
                if (t == ST::Text) { dl.add(semErr("Type mismatch", "E-SQL-103", "JOIN condition must be boolean, not TEXT", "ON takes a condition such as a.id = b.id.", "Write a comparison.", jn->pred->loc)); return fail(); }
            }
            jn->kids.push_back(std::move(input)); jn->kids.push_back(std::move(right));
            input = std::move(jn);
        }
    }

    // ---- WHERE
    if (selIn.where) {
        auto f = mkPlan(Plan::Filter); f->pred = selIn.where->clone(); f->width = input->width;
        Binder b(scope, dl); ST t = b.bind(*f->pred, false);
        if (!b.ok) return fail();
        if (t == ST::Text) { dl.add(semErr("Type mismatch", "E-SQL-103", "WHERE condition must be boolean, not TEXT", "A WHERE clause filters rows by a truth value.", "Write a comparison, e.g. name = 'x'.", f->pred->loc)); return fail(); }
        f->kids.push_back(std::move(input)); input = std::move(f);
    }

    // ---- select list (expand stars), GROUP BY, HAVING
    struct Out { ExprP e; std::string name; };
    std::vector<Out> outs;
    for (auto& it : selIn.items) {
        if (it.star) {
            bool any = false;
            for (auto& c : scope.cols) if (it.starTable.empty() || c.alias == it.starTable) {
                auto e = std::make_unique<Expr>(); e->kind = Expr::Col; e->name = c.name; e->table = c.alias; e->slot = c.slot; e->type = c.type; e->loc = it.loc;
                outs.push_back({std::move(e), c.name}); any = true;
            }
            if (!any) { dl.add(semErr("Unknown table", "E-SQL-100", it.starTable.empty() ? "SELECT * requires a FROM clause" : "Unknown table or alias '" + it.starTable + "'", "A star expands to the columns of the tables in FROM.", "Check the FROM clause.", it.loc)); return fail(); }
        } else {
            auto e = it.e->clone();
            Binder b(scope, dl); b.bind(*e, true);
            if (!b.ok) return fail();
            std::string name = !it.alias.empty() ? it.alias : e->kind == Expr::Col ? e->name : e->text();
            outs.push_back({std::move(e), name});
        }
    }
    std::vector<ExprP> groupKeys;
    for (auto& g : selIn.groupBy) {
        auto e = g->clone(); Binder b(scope, dl); b.bind(*e, false);
        if (!b.ok) return fail();
        groupKeys.push_back(std::move(e));
    }
    ExprP having;
    if (selIn.having) { having = selIn.having->clone(); Binder b(scope, dl); b.bind(*having, true); if (!b.ok) return fail(); }

    // ---- ORDER BY: ordinal / output alias / expression
    struct OrderRes { int outSlot = -1; ExprP e; bool desc = false; };
    std::vector<OrderRes> orders;
    for (auto& o : selIn.orderBy) {
        OrderRes r; r.desc = o.desc;
        if (o.e->kind == Expr::Lit && o.e->lit.t == ST::Int) {
            int64_t n = o.e->lit.i;
            if (n < 1 || n > static_cast<int64_t>(outs.size())) { dl.add(semErr("Invalid ORDER BY", "E-SQL-112", "ORDER BY position " + std::to_string(n) + " is out of range", "Positions count the SELECT items starting at 1.", "Use a number between 1 and " + std::to_string(outs.size()) + ".", o.e->loc)); return fail(); }
            r.outSlot = static_cast<int>(n - 1);
        } else if (o.e->kind == Expr::Col && o.e->table.empty()) {
            // alias match against the (expanded) output names that were given explicitly
            size_t idx = 0;
            for (auto& it : selIn.items) {
                if (it.star) { idx += static_cast<size_t>(std::count_if(scope.cols.begin(), scope.cols.end(), [&](const ScopeCol& c) { return it.starTable.empty() || c.alias == it.starTable; })); continue; }
                if (!it.alias.empty() && it.alias == o.e->name && r.outSlot < 0) r.outSlot = static_cast<int>(idx);
                ++idx;
            }
        }
        if (r.outSlot < 0) { r.e = o.e->clone(); Binder b(scope, dl); b.bind(*r.e, true); if (!b.ok) return fail(); }
        orders.push_back(std::move(r));
    }

    // ---- aggregation
    bool hasAgg = !groupKeys.empty();
    std::function<bool(const Expr&)> containsAgg = [&](const Expr& e) { if (e.kind == Expr::Agg) return true; for (auto& a : e.args) if (containsAgg(*a)) return true; return false; };
    for (auto& o : outs) if (containsAgg(*o.e)) hasAgg = true;
    if (having && containsAgg(*having)) hasAgg = true;
    for (auto& o : orders) if (o.e && containsAgg(*o.e)) hasAgg = true;
    if (having && !hasAgg) { dl.add(semErr("Invalid HAVING", "E-SQL-106", "HAVING requires GROUP BY or an aggregate", "HAVING filters groups.", "Use WHERE for row filters.", having->loc)); return fail(); }

    if (hasAgg) {
        auto agg = mkPlan(Plan::Aggregate);
        for (auto& g : groupKeys) agg->exprs.push_back(g->clone());
        const int nk = static_cast<int>(agg->exprs.size());
        bool bad = false;
        // Rewrite: group-key subtrees and aggregates become references to the Aggregate operator's output row.
        std::function<void(ExprP&)> rewrite = [&](ExprP& e) {
            if (bad) return;
            std::string txt = e->text();
            for (int k = 0; k < nk; ++k) if (agg->exprs[static_cast<size_t>(k)]->text() == txt) { auto c = std::make_unique<Expr>(); c->kind = Expr::Col; c->op = "post"; c->name = txt; c->slot = k; c->type = e->type; c->loc = e->loc; e = std::move(c); return; }
            if (e->kind == Expr::Agg) {
                int found = -1;
                for (size_t k = 0; k < agg->aggs.size(); ++k) if (agg->aggs[k].text == txt) found = static_cast<int>(k);
                if (found < 0) {
                    AggSpec a; a.fn = e->name; a.star = e->countStar; a.text = txt; a.type = e->type;
                    if (!e->args.empty()) a.arg = e->args[0]->clone();
                    agg->aggs.push_back(std::move(a)); found = static_cast<int>(agg->aggs.size()) - 1;
                }
                auto c = std::make_unique<Expr>(); c->kind = Expr::Col; c->op = "post"; c->name = txt; c->slot = nk + found; c->type = e->type; c->loc = e->loc; e = std::move(c); return;
            }
            if (e->kind == Expr::Col && e->op != "post") {
                dl.add(semErr("Invalid grouping", "E-SQL-107", "Column '" + e->text() + "' must appear in GROUP BY or be used inside an aggregate function", "With GROUP BY or aggregates, every selected column must be a grouping column or aggregated.", "Add it to GROUP BY, or wrap it in MIN()/MAX().", e->loc));
                bad = true; return;
            }
            for (auto& a : e->args) rewrite(a);
        };
        for (auto& o : outs) rewrite(o.e);
        if (having) rewrite(having);
        for (auto& o : orders) if (o.e) rewrite(o.e);
        if (bad) return fail();
        agg->width = nk + static_cast<int>(agg->aggs.size());
        agg->kids.push_back(std::move(input)); input = std::move(agg);
        if (having) { auto f = mkPlan(Plan::Filter); f->pred = std::move(having); f->width = input->width; f->kids.push_back(std::move(input)); input = std::move(f); }
    }

    // ---- projection (+ hidden ORDER BY columns)
    auto proj = mkPlan(Plan::Project);
    for (auto& o : outs) { proj->exprs.push_back(o.e->clone()); proj->names.push_back(o.name); res.types.push_back(o.e->type); res.names.push_back(o.name); }
    const int nOut = static_cast<int>(outs.size());
    std::vector<SortKey> keys;
    bool hidden = false;
    for (auto& o : orders) {
        SortKey k; k.desc = o.desc;
        if (o.outSlot >= 0) k.slot = o.outSlot;
        else {
            std::string txt = o.e->text(); int found = -1;
            for (size_t j = 0; j < proj->exprs.size(); ++j) if (proj->exprs[j]->text() == txt) { found = static_cast<int>(j); break; }
            if (found < 0) { proj->exprs.push_back(std::move(o.e)); proj->names.push_back(txt); found = static_cast<int>(proj->exprs.size()) - 1; hidden = true; }
            k.slot = found;
        }
        keys.push_back(k);
    }
    proj->width = static_cast<int>(proj->exprs.size());
    proj->kids.push_back(std::move(input)); input = std::move(proj);
    if (selIn.distinct) {
        if (hidden) { dl.add(semErr("Invalid ORDER BY", "E-SQL-112", "With SELECT DISTINCT, ORDER BY expressions must appear in the select list", "Sorting by a value that is not selected is ambiguous after duplicate removal.", "Add the expression to the SELECT list.", selIn.loc)); return fail(); }
        auto d = mkPlan(Plan::Distinct); d->width = input->width; d->kids.push_back(std::move(input)); input = std::move(d);
    }
    if (!keys.empty()) { auto s = mkPlan(Plan::Sort); s->keys = keys; s->width = input->width; s->kids.push_back(std::move(input)); input = std::move(s); }
    if (selIn.limit >= 0 || selIn.offset > 0 || input->width != nOut) {
        auto l = mkPlan(Plan::Limit); l->limit = selIn.limit; l->offset = selIn.offset; l->keep = nOut; l->width = nOut; l->kids.push_back(std::move(input)); input = std::move(l);
    }
    res.plan = std::move(input);
    return res;
}

// ================================================================== optimizer
namespace {

std::string snapshot(const Plan& p) { return p.text(0, false); }

// ---- constant folding
bool foldExpr(ExprP& e) {
    bool changed = false;
    for (auto& a : e->args) changed |= foldExpr(a);
    if (e->kind == Expr::Lit || e->kind == Expr::Col || e->kind == Expr::Agg || e->kind == Expr::Star) return changed;
    for (auto& a : e->args) if (a->kind != Expr::Lit) return changed;
    auto l = std::make_unique<Expr>(); l->kind = Expr::Lit; l->lit = evalConst(*e); l->loc = e->loc; l->type = l->lit.t == ST::Null ? e->type : l->lit.t;
    e = std::move(l);
    return true;
}
bool foldPlan(Plan& p) {
    bool changed = false;
    if (p.pred) changed |= foldExpr(p.pred);
    for (auto& e : p.exprs) changed |= foldExpr(e);
    for (auto& a : p.aggs) if (a.arg) changed |= foldExpr(a.arg);
    for (auto& k : p.kids) changed |= foldPlan(*k);
    return changed;
}
bool dropTrivialFilters(PlanP& p) {
    bool changed = false;
    for (auto& k : p->kids) changed |= dropTrivialFilters(k);
    if (p->k == Plan::Filter && p->pred && p->pred->kind == Expr::Lit && truthy(p->pred->lit)) { PlanP child = std::move(p->kids[0]); p = std::move(child); changed = true; }
    return changed;
}

// ---- predicate pushdown
PlanP pushFilter(PlanP node, std::vector<ExprP> conj, bool& changed) {
    if (node->k == Plan::Filter) {                       // merge stacked filters
        splitAnd(std::move(node->pred), conj);
        PlanP child = std::move(node->kids[0]);
        return pushFilter(std::move(child), std::move(conj), changed);
    }
    if (node->k != Plan::Join) {
        if (conj.empty()) return node;
        auto f = mkPlan(Plan::Filter); f->width = node->width; f->pred = joinAnd(conj); f->kids.push_back(std::move(node)); return f;
    }
    const int lw = node->kids[0]->width;
    std::vector<ExprP> L, R, stay, onList;
    const bool innerLike = node->jk != JoinClause::Left;
    if (node->jk == JoinClause::Left && node->pred) {    // ON conjuncts that only touch the right side can filter the right input first
        std::vector<ExprP> parts; splitAnd(std::move(node->pred), parts);
        for (auto& c : parts) {
            std::set<int> s; collectSlots(*c, s);
            if (!s.empty() && *s.begin() >= lw) { shiftSlots(*c, -lw); R.push_back(std::move(c)); changed = true; }
            else onList.push_back(std::move(c));
        }
        node->pred = joinAnd(onList);
    }
    for (auto& c : conj) {
        std::set<int> s; collectSlots(*c, s);
        if (s.empty()) stay.push_back(std::move(c));
        else if (*s.rbegin() < lw) { L.push_back(std::move(c)); changed = true; }
        else if (*s.begin() >= lw) { if (innerLike) { shiftSlots(*c, -lw); R.push_back(std::move(c)); changed = true; } else stay.push_back(std::move(c)); }
        else if (innerLike) { onList.push_back(std::move(c)); changed = true; }
        else stay.push_back(std::move(c));
    }
    node->kids[0] = pushFilter(std::move(node->kids[0]), std::move(L), changed);
    node->kids[1] = pushFilter(std::move(node->kids[1]), std::move(R), changed);
    if (innerLike && !onList.empty()) {
        std::vector<ExprP> all; if (node->pred) all.push_back(std::move(node->pred));
        for (auto& c : onList) all.push_back(std::move(c));
        node->pred = joinAnd(all);
        if (node->jk == JoinClause::Cross) node->jk = JoinClause::Inner;
    }
    if (stay.empty()) return node;
    auto f = mkPlan(Plan::Filter); f->width = node->width; f->pred = joinAnd(stay); f->kids.push_back(std::move(node)); return f;
}
void pushdownWalk(PlanP& n, bool& changed) {
    if (n->k == Plan::Filter || n->k == Plan::Join) {
        Plan::K topKind = n->k;
        bool involvesJoin = topKind == Plan::Join;
        if (topKind == Plan::Filter) { const Plan* c = n->kids[0].get(); while (c->k == Plan::Filter) c = c->kids[0].get(); involvesJoin = c->k == Plan::Join; }
        if (involvesJoin) { n = pushFilter(std::move(n), {}, changed); }
    }
    for (auto& k : n->kids) pushdownWalk(k, changed);
}

// ---- join selection: pick an equi-join conjunct as the hash key
bool selectJoins(Plan& p) {
    bool changed = false;
    for (auto& k : p.kids) changed |= selectJoins(*k);
    if (p.k == Plan::Join && p.pred && !p.hash) {
        const int lw = p.kids[0]->width;
        std::vector<const Expr*> parts;
        std::function<void(const Expr&)> flat = [&](const Expr& e) { if (e.kind == Expr::Bin && e.op == "AND") { flat(*e.args[0]); flat(*e.args[1]); } else parts.push_back(&e); };
        flat(*p.pred);
        for (auto* c : parts) {
            if (c->kind == Expr::Bin && c->op == "=" && c->args[0]->kind == Expr::Col && c->args[1]->kind == Expr::Col) {
                int a = c->args[0]->slot, b = c->args[1]->slot;
                if ((a < lw) != (b < lw) && a >= 0 && b >= 0) { p.hash = true; p.lslot = std::min(a, b); p.rslot = std::max(a, b) - lw; changed = true; break; }
            }
        }
    }
    return changed;
}

bool topN(Plan& p) {
    bool changed = false;
    for (auto& k : p.kids) changed |= topN(*k);
    if (p.k == Plan::Limit && p.limit >= 0 && p.kids[0]->k == Plan::Sort && p.kids[0]->topN < 0) { p.kids[0]->topN = p.offset + p.limit; changed = true; }
    return changed;
}

} // namespace

std::vector<OptStep> optimizePlan(PlanP& plan) {
    std::vector<OptStep> steps;
    auto record = [&](const char* rule, const char* desc, const std::string& before) { steps.push_back({rule, desc, before, snapshot(*plan)}); };
    std::string before = snapshot(*plan);
    if (foldPlan(*plan)) { record("constant_folding", "Sub-expressions whose operands are all literals are evaluated once at compile time (with SQL NULL semantics) instead of once per row.", before); before = snapshot(*plan); }
    if (dropTrivialFilters(plan)) { record("remove_trivial_filter", "A filter whose condition is the constant TRUE keeps every row, so the operator is removed.", before); before = snapshot(*plan); }
    bool pushed = false;
    pushdownWalk(plan, pushed);
    if (pushed) { record("predicate_pushdown", "Conjuncts of WHERE are moved below joins to the input they reference, so fewer rows reach the join; predicates spanning both sides become join conditions (cross join -> inner join).", before); before = snapshot(*plan); }
    if (selectJoins(*plan)) { record("hash_join", "A join condition of the form left.col = right.col lets the engine build a hash table on one input instead of comparing every pair of rows (O(n+m) instead of O(n*m)).", before); before = snapshot(*plan); }
    if (topN(*plan)) { record("top_n", "ORDER BY followed by LIMIT only needs the first N rows, so a partial sort is used instead of sorting everything.", before); }
    return steps;
}

// ================================================================== rendering
std::string Plan::text(int depth, bool withStats) const {
    std::string pad(static_cast<size_t>(depth) * 2, ' '), s = pad;
    auto list = [&](const std::vector<ExprP>& v) { std::string r; for (size_t k = 0; k < v.size(); ++k) r += (k ? ", " : "") + v[k]->text(); return r; };
    switch (k) {
        case Scan: s += "Scan " + table + (alias != table ? " AS " + alias : ""); break;
        case OneRow: s += "OneRow"; break;
        case Filter: s += "Filter " + pred->text(); break;
        case Join: s += std::string(hash ? "HashJoin " : "NestedLoopJoin ") + (jk == JoinClause::Left ? "LEFT" : jk == JoinClause::Cross ? "CROSS" : "INNER");
                   if (hash) s += " key=slot" + std::to_string(lslot) + "=slot" + std::to_string(rslot);
                   if (pred) s += " on " + pred->text(); break;
        case Aggregate: { s += "Aggregate"; if (!exprs.empty()) s += " group by " + list(exprs); s += " compute ["; for (size_t i = 0; i < aggs.size(); ++i) s += (i ? ", " : "") + aggs[i].text; s += "]"; break; }
        case Project: s += "Project " + list(exprs); break;
        case Distinct: s += "Distinct"; break;
        case Sort: { s += "Sort"; for (auto& key : keys) s += " slot" + std::to_string(key.slot) + (key.desc ? " DESC" : " ASC"); if (topN >= 0) s += " (top-" + std::to_string(topN) + ")"; break; }
        case Limit: s += "Limit " + (limit >= 0 ? std::to_string(limit) : std::string("ALL")) + " offset " + std::to_string(offset) + (keep >= 0 ? " keep " + std::to_string(keep) + " cols" : ""); break;
    }
    if (withStats && rowsOut >= 0) s += "   [rows=" + std::to_string(rowsOut) + "]";
    s += "\n";
    for (auto& c : kids) s += c->text(depth + 1, withStats);
    return s;
}

Json Plan::toJson() const {
    Json j = Json::object();
    std::string t = text(0, false); t = t.substr(0, t.find('\n'));
    static const char* names[] = {"Scan", "OneRow", "Filter", "Join", "Aggregate", "Project", "Distinct", "Sort", "Limit"};
    j.set("op", names[k]).set("detail", t).set("rows", static_cast<long long>(rowsOut)).set("width", width);
    Json ch = Json::array(); for (auto& c : kids) ch.push(c->toJson());
    j.set("children", ch);
    return j;
}

}} // namespace nova::sql
