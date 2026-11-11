// NOVA-SQL values, expression text, expression compilation to SQL-VM bytecode, and the bytecode interpreter.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include "sql_internal.hpp"

namespace nova { namespace sql {

const char* stName(ST t) { switch (t) { case ST::Null: return "NULL"; case ST::Int: return "INT"; case ST::Float: return "FLOAT"; case ST::Text: return "TEXT"; } return "?"; }

static std::string fmtFloat(double d) {
    if (std::isnan(d)) return "nan";
    if (std::isinf(d)) return d < 0 ? "-inf" : "inf";
    char buf[48]; std::snprintf(buf, sizeof buf, "%.15g", d);
    std::string s = buf;
    if (s.find_first_of(".en") == std::string::npos) s += ".0";
    return s;
}

Json SVal::toJson() const {
    switch (t) { case ST::Null: return Json(); case ST::Int: return Json(static_cast<long long>(i)); case ST::Float: return Json(f); case ST::Text: return Json(s); }
    return Json();
}
std::string SVal::text() const {
    switch (t) {
        case ST::Null: return "NULL"; case ST::Int: return std::to_string(i); case ST::Float: return fmtFloat(f);
        case ST::Text: { std::string o = "'"; for (char c : s) { if (c == '\'') o += "''"; else o += c; } return o + "'"; }
    }
    return "?";
}

static int rank(const SVal& v) { return v.t == ST::Null ? 0 : v.numeric() ? 1 : 2; }
int cmpTotal(const SVal& a, const SVal& b) {
    int ra = rank(a), rb = rank(b);
    if (ra != rb) return ra < rb ? -1 : 1;
    if (ra == 0) return 0;
    if (ra == 2) return a.s < b.s ? -1 : a.s > b.s ? 1 : 0;
    if (a.t == ST::Int && b.t == ST::Int) return a.i < b.i ? -1 : a.i > b.i ? 1 : 0;
    double x = a.num(), y = b.num();
    return x < y ? -1 : x > y ? 1 : 0;
}
bool RowLess::operator()(const Row& a, const Row& b) const {
    for (size_t k = 0; k < a.size() && k < b.size(); ++k) { int c = cmpTotal(a[k], b[k]); if (c) return c < 0; }
    return a.size() < b.size();
}

int Table::colIndex(const std::string& n) const { for (size_t k = 0; k < cols.size(); ++k) if (cols[k].name == n) return static_cast<int>(k); return -1; }

// ------------------------------------------------------------------ expression text / clone
ExprP Expr::clone() const {
    auto e = std::make_unique<Expr>();
    e->kind = kind; e->op = op; e->lit = lit; e->table = table; e->name = name; e->countStar = countStar; e->loc = loc; e->slot = slot; e->type = type;
    for (auto& a : args) e->args.push_back(a->clone());
    return e;
}

std::string Expr::text() const {
    auto list = [&](size_t from) { std::string s; for (size_t k = from; k < args.size(); ++k) s += (k > from ? ", " : "") + args[k]->text(); return s; };
    switch (kind) {
        case Lit: return lit.text();
        case Col: return table.empty() ? name : table + "." + name;
        case Un: return op == "NOT" ? "NOT " + args[0]->text() : "-" + args[0]->text();
        case Bin: return "(" + args[0]->text() + " " + op + " " + args[1]->text() + ")";
        case Func: case Agg: return name + "(" + (countStar ? std::string("*") : list(0)) + ")";
        case Star: return "*";
        case IsNull: return "(" + args[0]->text() + " " + op + ")";
        case Like: return "(" + args[0]->text() + " " + op + " " + args[1]->text() + ")";
        case In: return "(" + args[0]->text() + " " + op + " (" + list(1) + "))";
        case Between: return "(" + args[0]->text() + " " + op + " " + args[1]->text() + " AND " + args[2]->text() + ")";
    }
    return "?";
}

std::string Stmt::sexpr() const {
    auto ex = [](const ExprP& e) { return e ? e->text() : std::string("-"); };
    switch (kind) {
        case Create: { std::string s = "(create-table " + table; for (auto& c : cols) s += " (" + c.name + " " + stName(c.type) + (c.primaryKey ? " pk" : c.notNull ? " not-null" : "") + ")"; return s + ")"; }
        case Drop: return "(drop-table " + table + ")";
        case Insert: return "(insert " + table + " rows:" + std::to_string(rows.size()) + ")";
        case Update: { std::string s = "(update " + table; for (auto& p : sets) s += " (set " + p.first + " " + p.second->text() + ")"; return s + " (where " + ex(where) + "))"; }
        case Delete: return "(delete " + table + " (where " + ex(where) + "))";
        case Select_: {
            std::string s = std::string("(select") + (sel.distinct ? " distinct" : "") + " (items";
            for (auto& it : sel.items) s += " " + (it.star ? std::string(it.starTable.empty() ? "*" : it.starTable + ".*") : it.e->text() + (it.alias.empty() ? "" : " AS " + it.alias));
            s += ")";
            if (sel.hasFrom) {
                s += " (from " + sel.from.name;
                for (auto& j : sel.joins) s += std::string(" (") + (j.kind == JoinClause::Left ? "left-join " : j.kind == JoinClause::Cross ? "cross-join " : "join ") + j.t.name + (j.on ? " on " + j.on->text() : "") + ")";
                s += ")";
            }
            if (sel.where) s += " (where " + sel.where->text() + ")";
            if (!sel.groupBy.empty()) { s += " (group-by"; for (auto& g : sel.groupBy) s += " " + g->text(); s += ")"; }
            if (sel.having) s += " (having " + sel.having->text() + ")";
            if (!sel.orderBy.empty()) { s += " (order-by"; for (auto& o : sel.orderBy) s += " " + o.e->text() + (o.desc ? " DESC" : ""); s += ")"; }
            if (sel.limit >= 0) s += " (limit " + std::to_string(sel.limit) + " offset " + std::to_string(sel.offset) + ")";
            return s + ")";
        }
    }
    return "?";
}

// ------------------------------------------------------------------ compilation
namespace {
void emit(SCode& c, SOp op, int a = 0, SVal k = SVal()) { SInstr i; i.op = op; i.a = a; i.k = std::move(k); c.code.push_back(std::move(i)); }

void gen(SCode& c, const Expr& e) {
    switch (e.kind) {
        case Expr::Lit: emit(c, SOp::PUSH, 0, e.lit); break;
        case Expr::Col: emit(c, SOp::LOAD, e.slot); break;
        case Expr::Un: gen(c, *e.args[0]); emit(c, e.op == "NOT" ? SOp::NOT : SOp::NEG); break;
        case Expr::Bin: {
            gen(c, *e.args[0]); gen(c, *e.args[1]);
            static const std::map<std::string, SOp> m = {{"+", SOp::ADD}, {"-", SOp::SUB}, {"*", SOp::MUL}, {"/", SOp::DIV}, {"%", SOp::MOD}, {"||", SOp::CAT},
                {"=", SOp::EQ}, {"<>", SOp::NE}, {"<", SOp::LT}, {"<=", SOp::LE}, {">", SOp::GT}, {">=", SOp::GE}, {"AND", SOp::AND}, {"OR", SOp::OR}};
            emit(c, m.at(e.op)); break;
        }
        case Expr::IsNull: gen(c, *e.args[0]); emit(c, e.op == "IS NULL" ? SOp::ISNULL : SOp::NOTNULL); break;
        case Expr::Like: gen(c, *e.args[0]); gen(c, *e.args[1]); emit(c, SOp::LIKE); if (e.op == "NOT LIKE") emit(c, SOp::NOT); break;
        case Expr::In:   // x IN (a, b, c)  ==  x = a OR x = b OR x = c   (three-valued: yields NULL when no match and a NULL was seen)
            for (size_t k = 1; k < e.args.size(); ++k) { gen(c, *e.args[0]); gen(c, *e.args[k]); emit(c, SOp::EQ); if (k > 1) emit(c, SOp::OR); }
            if (e.op == "NOT IN") emit(c, SOp::NOT);
            break;
        case Expr::Between:   // x BETWEEN lo AND hi  ==  x >= lo AND x <= hi
            gen(c, *e.args[0]); gen(c, *e.args[1]); emit(c, SOp::GE);
            gen(c, *e.args[0]); gen(c, *e.args[2]); emit(c, SOp::LE);
            emit(c, SOp::AND);
            if (e.op == "NOT BETWEEN") emit(c, SOp::NOT);
            break;
        case Expr::Func:
            for (auto& a : e.args) gen(c, *a);
            if (e.name == "UPPER") emit(c, SOp::UPPER); else if (e.name == "LOWER") emit(c, SOp::LOWER); else if (e.name == "LENGTH") emit(c, SOp::LENGTH);
            else if (e.name == "ABS") emit(c, SOp::ABS); else emit(c, SOp::COALESCE, static_cast<int>(e.args.size()));
            break;
        case Expr::Agg: case Expr::Star: emit(c, SOp::PUSH, 0, SVal()); break;   // replaced by slot references before compilation
    }
}

SVal arith(char op, const SVal& a, const SVal& b) {
    if (a.isNull() || b.isNull()) return SVal::null();
    if (a.t == ST::Int && b.t == ST::Int) {
        uint64_t x = static_cast<uint64_t>(a.i), y = static_cast<uint64_t>(b.i);
        switch (op) {
            case '+': return SVal::Int(static_cast<int64_t>(x + y));
            case '-': return SVal::Int(static_cast<int64_t>(x - y));
            case '*': return SVal::Int(static_cast<int64_t>(x * y));
            case '/': if (b.i == 0) return SVal::null(); return SVal::Int(b.i == -1 ? static_cast<int64_t>(0 - x) : a.i / b.i);
            default: if (b.i == 0) return SVal::null(); return SVal::Int(b.i == -1 ? 0 : a.i % b.i);
        }
    }
    if (!a.numeric() || !b.numeric()) return SVal::null();
    double x = a.num(), y = b.num();
    switch (op) {
        case '+': return SVal::Float(x + y); case '-': return SVal::Float(x - y); case '*': return SVal::Float(x * y);
        case '/': if (y == 0) return SVal::null(); return SVal::Float(x / y);
        default: { if (a.t == ST::Float || b.t == ST::Float) return SVal::null(); return SVal::null(); }
    }
}

SVal compare(SOp op, const SVal& a, const SVal& b) {
    if (a.isNull() || b.isNull()) return SVal::null();
    int c = cmpTotal(a, b);
    bool r = op == SOp::EQ ? c == 0 : op == SOp::NE ? c != 0 : op == SOp::LT ? c < 0 : op == SOp::LE ? c <= 0 : op == SOp::GT ? c > 0 : c >= 0;
    return SVal::Int(r);
}

bool likeMatch(const std::string& s, const std::string& p) {   // % and _ ; case-insensitive for ASCII (SQLite default)
    size_t si = 0, pi = 0, star = std::string::npos, mark = 0;
    auto eq = [](char x, char y) { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); };
    while (si < s.size()) {
        if (pi < p.size() && (p[pi] == '_' || (p[pi] != '%' && eq(p[pi], s[si])))) { ++si; ++pi; }
        else if (pi < p.size() && p[pi] == '%') { star = pi++; mark = si; }
        else if (star != std::string::npos) { pi = star + 1; si = ++mark; }
        else return false;
    }
    while (pi < p.size() && p[pi] == '%') ++pi;
    return pi == p.size();
}

int utf8Len(const std::string& s) { int n = 0; for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n; return n; }
} // namespace

bool truthy(const SVal& v) { return v.t == ST::Int ? v.i != 0 : v.t == ST::Float ? v.f != 0 : false; }

SCode compileExpr(const Expr& e) { SCode c; c.source = e.text(); gen(c, e); return c; }

SVal runCode(const SCode& c, const Row& row) {
    std::vector<SVal> st; st.reserve(8);
    auto pop = [&]() { SVal v = std::move(st.back()); st.pop_back(); return v; };
    for (const SInstr& in : c.code) {
        switch (in.op) {
            case SOp::PUSH: st.push_back(in.k); break;
            case SOp::LOAD: st.push_back(row[static_cast<size_t>(in.a)]); break;
            case SOp::ADD: case SOp::SUB: case SOp::MUL: case SOp::DIV: case SOp::MOD: {
                SVal b = pop(), a = pop();
                st.push_back(arith(in.op == SOp::ADD ? '+' : in.op == SOp::SUB ? '-' : in.op == SOp::MUL ? '*' : in.op == SOp::DIV ? '/' : '%', a, b)); break;
            }
            case SOp::NEG: { SVal a = pop(); st.push_back(a.t == ST::Int ? SVal::Int(static_cast<int64_t>(0 - static_cast<uint64_t>(a.i))) : a.t == ST::Float ? SVal::Float(-a.f) : SVal::null()); break; }
            case SOp::NOT: { SVal a = pop(); st.push_back(a.isNull() ? SVal::null() : SVal::Int(!truthy(a))); break; }
            case SOp::CAT: { SVal b = pop(), a = pop(); st.push_back(a.isNull() || b.isNull() ? SVal::null() : SVal::Text(a.s + b.s)); break; }
            case SOp::EQ: case SOp::NE: case SOp::LT: case SOp::LE: case SOp::GT: case SOp::GE: { SVal b = pop(), a = pop(); st.push_back(compare(in.op, a, b)); break; }
            case SOp::AND: {
                SVal b = pop(), a = pop();
                bool fa = !a.isNull() && !truthy(a), fb = !b.isNull() && !truthy(b);
                st.push_back(fa || fb ? SVal::Int(0) : (a.isNull() || b.isNull()) ? SVal::null() : SVal::Int(1)); break;
            }
            case SOp::OR: {
                SVal b = pop(), a = pop();
                bool ta = !a.isNull() && truthy(a), tb = !b.isNull() && truthy(b);
                st.push_back(ta || tb ? SVal::Int(1) : (a.isNull() || b.isNull()) ? SVal::null() : SVal::Int(0)); break;
            }
            case SOp::ISNULL: { SVal a = pop(); st.push_back(SVal::Int(a.isNull())); break; }
            case SOp::NOTNULL: { SVal a = pop(); st.push_back(SVal::Int(!a.isNull())); break; }
            case SOp::LIKE: { SVal p = pop(), a = pop(); st.push_back(a.isNull() || p.isNull() ? SVal::null() : SVal::Int(likeMatch(a.s, p.s))); break; }
            case SOp::UPPER: case SOp::LOWER: {
                SVal a = pop(); if (a.isNull()) { st.push_back(a); break; }
                for (auto& ch : a.s) ch = static_cast<char>(in.op == SOp::UPPER ? std::toupper(static_cast<unsigned char>(ch)) : std::tolower(static_cast<unsigned char>(ch)));
                st.push_back(std::move(a)); break;
            }
            case SOp::LENGTH: { SVal a = pop(); st.push_back(a.isNull() ? SVal::null() : SVal::Int(utf8Len(a.s))); break; }
            case SOp::ABS: { SVal a = pop(); st.push_back(a.t == ST::Int ? SVal::Int(a.i < 0 ? static_cast<int64_t>(0 - static_cast<uint64_t>(a.i)) : a.i) : a.t == ST::Float ? SVal::Float(std::fabs(a.f)) : SVal::null()); break; }
            case SOp::COALESCE: {
                std::vector<SVal> args(static_cast<size_t>(in.a));
                for (int k = in.a - 1; k >= 0; --k) args[static_cast<size_t>(k)] = pop();
                SVal r; for (auto& a : args) if (!a.isNull()) { r = a; break; }
                st.push_back(std::move(r)); break;
            }
        }
    }
    return st.empty() ? SVal::null() : st.back();
}

SVal evalConst(const Expr& e) { return runCode(compileExpr(e), Row()); }

std::vector<std::string> SCode::disasm() const {
    static const char* names[] = {"PUSH", "LOAD", "ADD", "SUB", "MUL", "DIV", "MOD", "NEG", "NOT", "CAT", "EQ", "NE", "LT", "LE", "GT", "GE", "AND", "OR",
                                  "ISNULL", "NOTNULL", "LIKE", "UPPER", "LOWER", "LENGTH", "ABS", "COALESCE"};
    std::vector<std::string> out;
    for (size_t k = 0; k < code.size(); ++k) {
        const SInstr& i = code[k];
        char buf[16]; std::snprintf(buf, sizeof buf, "%03zu  ", k);
        std::string s = std::string(buf) + names[static_cast<int>(i.op)];
        if (i.op == SOp::PUSH) s += " " + i.k.text(); else if (i.op == SOp::LOAD) s += " slot" + std::to_string(i.a); else if (i.op == SOp::COALESCE) s += " " + std::to_string(i.a);
        out.push_back(s);
    }
    return out;
}

}} // namespace nova::sql
