// NOVA-SQL lexer and recursive-descent parser (precedence climbing for expressions; SQLite operator precedence).
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include "sql_internal.hpp"

namespace nova { namespace sql {

namespace {

const std::set<std::string>& reserved() {
    static const std::set<std::string> r = {"select", "from", "where", "group", "by", "having", "order", "limit", "offset", "join", "inner", "left",
        "outer", "cross", "on", "and", "or", "not", "null", "is", "like", "in", "between", "as", "distinct", "all", "insert", "into", "values",
        "update", "set", "delete", "create", "table", "drop", "primary", "key", "if", "exists", "asc", "desc", "true", "false", "case", "when", "then", "else", "end"};
    return r;
}

struct ParseError { Diagnostic d; };

class Lexer {
public:
    Lexer(const std::string& s, DiagnosticList& dl) : s_(s), diags_(dl) {}
    std::vector<SToken> run() {
        std::vector<SToken> out;
        while (true) {
            skip();
            if (p_ >= s_.size()) break;
            int sl = line_, sc = col_, off = static_cast<int>(p_);
            char c = s_[p_];
            SToken t; t.off = off;
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                std::string id; while (p_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[p_])) || s_[p_] == '_')) id += lower(adv());
                t.k = SToken::Ident; t.text = id;
            } else if (std::isdigit(static_cast<unsigned char>(c))) {
                std::string n; bool fl = false;
                while (p_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[p_]))) n += adv();
                if (p_ + 1 < s_.size() && s_[p_] == '.' && std::isdigit(static_cast<unsigned char>(s_[p_ + 1]))) { fl = true; n += adv(); while (p_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[p_]))) n += adv(); }
                if (p_ < s_.size() && (std::isalpha(static_cast<unsigned char>(s_[p_])) || s_[p_] == '_')) {
                    std::string bad = n; while (p_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[p_])) || s_[p_] == '_')) bad += adv();
                    err("Malformed number", "E-SQL-002", "Invalid literal '" + bad + "'", "Identifiers cannot start with a digit.", "Separate the number from the name.", {sl, sc, line_, col_});
                    continue;
                }
                if (!fl) { errno = 0; std::strtoll(n.c_str(), nullptr, 10); if (errno == ERANGE) { err("Integer too large", "E-SQL-003", "Integer literal '" + n + "' does not fit in 64 bits", "INT is a signed 64-bit integer.", "Use a FLOAT literal such as " + n + ".0.", {sl, sc, line_, col_}); continue; } }
                t.k = fl ? SToken::Float : SToken::Int; t.text = n;
            } else if (c == '\'') {
                adv(); std::string v; bool closed = false;
                while (p_ < s_.size()) {
                    char d = adv();
                    if (d == '\'') { if (p_ < s_.size() && s_[p_] == '\'') { v += '\''; adv(); } else { closed = true; break; } }
                    else v += d;
                }
                if (!closed) { err("Unterminated string", "E-SQL-001", "String literal is never closed", "String literals are enclosed in single quotes; write '' for a quote inside.", "Add the closing '.", {sl, sc, line_, col_}); continue; }
                t.k = SToken::Str; t.text = v;
            } else {
                std::string two = p_ + 1 < s_.size() ? s_.substr(p_, 2) : "";
                if (two == "<>" || two == "!=" || two == "<=" || two == ">=" || two == "||" || two == "==") { adv(); adv(); t.k = SToken::Op; t.text = two == "!=" ? "<>" : two == "==" ? "=" : two; }
                else if (std::string("=<>+-*/%(),.;").find(c) != std::string::npos) { adv(); t.k = SToken::Op; t.text = std::string(1, c); }
                else { adv(); err("Invalid character", "E-SQL-004", std::string("Unexpected character '") + c + "'", "This character is not part of NOVA-SQL.", "Remove it.", {sl, sc, line_, col_}); continue; }
            }
            t.loc = {sl, sc, line_, col_}; t.endOff = static_cast<int>(p_);
            out.push_back(std::move(t));
        }
        SToken e; e.k = SToken::Eof; e.loc = {line_, col_, line_, col_}; e.off = e.endOff = static_cast<int>(p_);
        out.push_back(e);
        return out;
    }
private:
    const std::string& s_; DiagnosticList& diags_;
    size_t p_ = 0; int line_ = 1, col_ = 1;
    static char lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    char adv() { char c = s_[p_++]; if (c == '\n') { ++line_; col_ = 1; } else ++col_; return c; }
    void skip() {
        while (p_ < s_.size()) {
            char c = s_[p_];
            if (std::isspace(static_cast<unsigned char>(c))) { adv(); continue; }
            if (c == '-' && p_ + 1 < s_.size() && s_[p_ + 1] == '-') { while (p_ < s_.size() && s_[p_] != '\n') adv(); continue; }
            if (c == '/' && p_ + 1 < s_.size() && s_[p_ + 1] == '*') {
                int sl = line_, sc = col_; adv(); adv(); bool closed = false;
                while (p_ < s_.size()) { if (s_[p_] == '*' && p_ + 1 < s_.size() && s_[p_ + 1] == '/') { adv(); adv(); closed = true; break; } adv(); }
                if (!closed) err("Unterminated comment", "E-SQL-005", "Block comment is never closed", "Close it with */.", "Add */.", {sl, sc, sl, sc + 2});
                continue;
            }
            break;
        }
    }
    void err(const std::string& cat, const std::string& code, const std::string& msg, const std::string& ex, const std::string& sg, SourceLoc loc) {
        Diagnostic d; d.phase = "lexical"; d.category = cat; d.code = code; d.message = msg; d.explanation = ex; d.suggestion = sg; d.loc = loc; diags_.add(std::move(d));
    }
};

class Parser {
public:
    Parser(const std::string& src, std::vector<SToken>& t, DiagnosticList& d) : src_(src), t_(t), diags_(d) {}

    std::vector<Stmt> script() {
        std::vector<Stmt> out;
        while (!at(SToken::Eof)) {
            if (isOp(";")) { ++p_; continue; }
            try {
                size_t startTok = p_;
                Stmt s = statement();
                s.text = src_.substr(t_[startTok].off, t_[p_ - 1].endOff - t_[startTok].off);
                if (!at(SToken::Eof) && !isOp(";")) fail("Expected ';' or end of input", cur());
                out.push_back(std::move(s));
            } catch (ParseError& e) {
                diags_.add(std::move(e.d));
                while (!at(SToken::Eof) && !isOp(";")) ++p_;   // recover at the next statement boundary
            }
        }
        return out;
    }

private:
    const std::string& src_; std::vector<SToken>& t_; DiagnosticList& diags_; size_t p_ = 0;

    const SToken& cur() const { return t_[p_]; }
    const SToken& peek(size_t k = 1) const { return t_[std::min(p_ + k, t_.size() - 1)]; }
    bool at(SToken::K k) const { return cur().k == k; }
    bool isOp(const char* o) const { return cur().k == SToken::Op && cur().text == o; }
    bool isKw(const char* kw) const { return cur().k == SToken::Ident && cur().text == kw; }
    bool isKwAt(size_t k, const char* kw) const { const SToken& x = peek(k); return x.k == SToken::Ident && x.text == kw; }
    bool acceptKw(const char* kw) { if (isKw(kw)) { ++p_; return true; } return false; }
    bool acceptOp(const char* o) { if (isOp(o)) { ++p_; return true; } return false; }
    static std::string describe(const SToken& t) {
        switch (t.k) { case SToken::Eof: return "end of input"; case SToken::Str: return "string '" + t.text + "'"; case SToken::Ident: return "'" + t.text + "'"; default: return "'" + t.text + "'"; }
    }
    [[noreturn]] void fail(const std::string& expected, const SToken& at_) {
        Diagnostic d; d.phase = "syntax"; d.category = "Syntax error"; d.code = "E-SQL-010";
        d.message = expected + " but found " + describe(at_);
        d.explanation = "The statement does not match the NOVA-SQL grammar at this point.";
        d.suggestion = "Check the keyword order: SELECT … FROM … WHERE … GROUP BY … HAVING … ORDER BY … LIMIT …";
        d.loc = at_.loc; throw ParseError{d};
    }
    void expectKw(const char* kw) { if (!acceptKw(kw)) { std::string up = kw; for (auto& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); fail("Expected " + up, cur()); } }
    void expectOp(const char* o) { if (!acceptOp(o)) fail(std::string("Expected '") + o + "'", cur()); }
    std::string ident(const char* what) {
        if (!at(SToken::Ident) || reserved().count(cur().text)) fail(std::string("Expected ") + what, cur());
        return t_[p_++].text;
    }

    Stmt statement() {
        Stmt s; s.loc = cur().loc;
        if (isKw("select")) { s.kind = Stmt::Select_; s.sel = select(); }
        else if (acceptKw("insert")) {
            s.kind = Stmt::Insert; expectKw("into"); s.table = ident("a table name");
            if (acceptOp("(")) { do s.insCols.push_back(ident("a column name")); while (acceptOp(",")); expectOp(")"); }
            expectKw("values");
            do { expectOp("("); std::vector<ExprP> row; do row.push_back(expr()); while (acceptOp(",")); expectOp(")"); s.rows.push_back(std::move(row)); } while (acceptOp(","));
        } else if (acceptKw("update")) {
            s.kind = Stmt::Update; s.table = ident("a table name"); expectKw("set");
            do { std::string c = ident("a column name"); expectOp("="); s.sets.emplace_back(c, expr()); } while (acceptOp(","));
            if (acceptKw("where")) s.where = expr();
        } else if (acceptKw("delete")) {
            s.kind = Stmt::Delete; expectKw("from"); s.table = ident("a table name");
            if (acceptKw("where")) s.where = expr();
        } else if (acceptKw("create")) {
            s.kind = Stmt::Create; expectKw("table");
            if (isKw("if") && isKwAt(1, "not") && isKwAt(2, "exists")) { p_ += 3; s.ifFlag = true; }
            s.table = ident("a table name"); expectOp("(");
            do {
                ColDef c; c.loc = cur().loc; c.name = ident("a column name");
                if (!at(SToken::Ident)) fail("Expected a column type (INT, FLOAT or TEXT)", cur());
                std::string ty = t_[p_++].text;
                if (ty == "int" || ty == "integer" || ty == "bigint") c.type = ST::Int;
                else if (ty == "float" || ty == "real" || ty == "double") c.type = ST::Float;
                else if (ty == "text" || ty == "varchar" || ty == "char") c.type = ST::Text;
                else { --p_; fail("Expected a column type (INT, FLOAT or TEXT)", cur()); }
                if (acceptOp("(")) { if (!at(SToken::Int)) fail("Expected a length", cur()); ++p_; expectOp(")"); }
                while (true) {
                    if (isKw("not") && isKwAt(1, "null")) { p_ += 2; c.notNull = true; }
                    else if (isKw("primary") && isKwAt(1, "key")) { p_ += 2; c.primaryKey = true; c.notNull = true; }
                    else break;
                }
                s.cols.push_back(std::move(c));
            } while (acceptOp(","));
            expectOp(")");
        } else if (acceptKw("drop")) {
            s.kind = Stmt::Drop; expectKw("table");
            if (isKw("if") && isKwAt(1, "exists")) { p_ += 2; s.ifFlag = true; }
            s.table = ident("a table name");
        } else fail("Expected SELECT, INSERT, UPDATE, DELETE, CREATE or DROP", cur());
        return s;
    }

    TableRef tableRef() {
        TableRef r; r.loc = cur().loc; r.name = ident("a table name"); r.alias = r.name;
        if (acceptKw("as")) r.alias = ident("an alias");
        else if (at(SToken::Ident) && !reserved().count(cur().text)) r.alias = t_[p_++].text;
        return r;
    }

    Select select() {
        Select s; s.loc = cur().loc; expectKw("select");
        if (acceptKw("distinct")) s.distinct = true; else acceptKw("all");
        do {
            SelectItem it; it.loc = cur().loc;
            if (isOp("*")) { ++p_; it.star = true; }
            else if (at(SToken::Ident) && peek().k == SToken::Op && peek().text == "." && peek(2).k == SToken::Op && peek(2).text == "*") { it.star = true; it.starTable = t_[p_].text; p_ += 3; }
            else {
                it.e = expr();
                if (acceptKw("as")) it.alias = ident("an alias");
                else if (at(SToken::Ident) && !reserved().count(cur().text)) it.alias = t_[p_++].text;
            }
            s.items.push_back(std::move(it));
        } while (acceptOp(","));
        if (acceptKw("from")) {
            s.hasFrom = true; s.from = tableRef();
            while (true) {
                JoinClause j;
                if (acceptOp(",")) { j.kind = JoinClause::Cross; j.t = tableRef(); }
                else if (isKw("cross") && isKwAt(1, "join")) { p_ += 2; j.kind = JoinClause::Cross; j.t = tableRef(); }
                else if (isKw("join") || (isKw("inner") && isKwAt(1, "join"))) { if (isKw("inner")) ++p_; ++p_; j.kind = JoinClause::Inner; j.t = tableRef(); expectKw("on"); j.on = expr(); }
                else if (isKw("left")) { ++p_; acceptKw("outer"); expectKw("join"); j.kind = JoinClause::Left; j.t = tableRef(); expectKw("on"); j.on = expr(); }
                else break;
                s.joins.push_back(std::move(j));
            }
        }
        if (acceptKw("where")) s.where = expr();
        if (isKw("group")) { ++p_; expectKw("by"); do s.groupBy.push_back(expr()); while (acceptOp(",")); }
        if (acceptKw("having")) s.having = expr();
        if (isKw("order")) { ++p_; expectKw("by"); do { OrderItem o; o.e = expr(); if (acceptKw("desc")) o.desc = true; else acceptKw("asc"); s.orderBy.push_back(std::move(o)); } while (acceptOp(",")); }
        if (acceptKw("limit")) {
            if (!at(SToken::Int)) fail("Expected an integer after LIMIT", cur());
            s.limit = std::strtoll(t_[p_++].text.c_str(), nullptr, 10);
            if (acceptKw("offset")) { if (!at(SToken::Int)) fail("Expected an integer after OFFSET", cur()); s.offset = std::strtoll(t_[p_++].text.c_str(), nullptr, 10); }
        }
        return s;
    }

    // ---------------- expressions
    ExprP mk(Expr::Kind k, const std::string& op, SourceLoc loc) { auto e = std::make_unique<Expr>(); e->kind = k; e->op = op; e->loc = loc; return e; }
    static void span(Expr& e, const SourceLoc& a, const SourceLoc& b) { e.loc = {a.line, a.col, b.endLine, b.endCol}; }
    ExprP bin(const std::string& op, ExprP l, ExprP r) { auto e = mk(Expr::Bin, op, l->loc); span(*e, l->loc, r->loc); e->args.push_back(std::move(l)); e->args.push_back(std::move(r)); return e; }

    ExprP expr() { return orExpr(); }
    ExprP orExpr() { auto l = andExpr(); while (isKw("or")) { ++p_; l = bin("OR", std::move(l), andExpr()); } return l; }
    ExprP andExpr() { auto l = notExpr(); while (isKw("and")) { ++p_; l = bin("AND", std::move(l), notExpr()); } return l; }
    ExprP notExpr() {
        if (isKw("not")) { SourceLoc l = cur().loc; ++p_; auto e = mk(Expr::Un, "NOT", l); e->args.push_back(notExpr()); span(*e, l, e->args[0]->loc); return e; }
        return equality();
    }
    ExprP equality() {
        auto l = rel();
        while (true) {
            if (isOp("=") || isOp("<>")) { std::string op = cur().text; ++p_; l = bin(op, std::move(l), rel()); }
            else if (isKw("is")) {
                ++p_; bool neg = acceptKw("not"); SourceLoc nl = cur().loc; expectKw("null");
                auto e = mk(Expr::IsNull, neg ? "IS NOT NULL" : "IS NULL", l->loc); span(*e, l->loc, nl); e->args.push_back(std::move(l)); l = std::move(e);
            } else {
                bool neg = false; size_t save = p_;
                if (isKw("not") && (isKwAt(1, "like") || isKwAt(1, "in") || isKwAt(1, "between"))) { neg = true; ++p_; }
                if (isKw("like")) { ++p_; auto r = rel(); auto e = mk(Expr::Like, neg ? "NOT LIKE" : "LIKE", l->loc); span(*e, l->loc, r->loc); e->args.push_back(std::move(l)); e->args.push_back(std::move(r)); l = std::move(e); }
                else if (isKw("in")) {
                    ++p_; expectOp("("); auto e = mk(Expr::In, neg ? "NOT IN" : "IN", l->loc); e->args.push_back(std::move(l));
                    do e->args.push_back(expr()); while (acceptOp(","));
                    SourceLoc rp = cur().loc; expectOp(")"); span(*e, e->args[0]->loc, rp); l = std::move(e);
                } else if (isKw("between")) {
                    ++p_; auto lo = rel(); expectKw("and"); auto hi = rel();
                    auto e = mk(Expr::Between, neg ? "NOT BETWEEN" : "BETWEEN", l->loc); span(*e, l->loc, hi->loc);
                    e->args.push_back(std::move(l)); e->args.push_back(std::move(lo)); e->args.push_back(std::move(hi)); l = std::move(e);
                } else { p_ = save; break; }
            }
        }
        return l;
    }
    ExprP rel() { auto l = add(); while (isOp("<") || isOp("<=") || isOp(">") || isOp(">=")) { std::string op = cur().text; ++p_; l = bin(op, std::move(l), add()); } return l; }
    ExprP add() { auto l = mul(); while (isOp("+") || isOp("-")) { std::string op = cur().text; ++p_; l = bin(op, std::move(l), mul()); } return l; }
    ExprP mul() { auto l = concat(); while (isOp("*") || isOp("/") || isOp("%")) { std::string op = cur().text; ++p_; l = bin(op, std::move(l), concat()); } return l; }
    ExprP concat() { auto l = unary(); while (isOp("||")) { ++p_; l = bin("||", std::move(l), unary()); } return l; }
    ExprP unary() {
        if (isOp("-") || isOp("+")) {
            bool neg = isOp("-"); SourceLoc l = cur().loc; ++p_;
            auto o = unary();
            if (!neg) return o;
            if (o->kind == Expr::Lit && o->lit.t == ST::Int) { o->lit.i = static_cast<int64_t>(0 - static_cast<uint64_t>(o->lit.i)); span(*o, l, o->loc); return o; }
            if (o->kind == Expr::Lit && o->lit.t == ST::Float) { o->lit.f = -o->lit.f; span(*o, l, o->loc); return o; }
            auto e = mk(Expr::Un, "-", l); span(*e, l, o->loc); e->args.push_back(std::move(o)); return e;
        }
        return primary();
    }
    ExprP primary() {
        const SToken& t = cur(); SourceLoc loc = t.loc;
        if (t.k == SToken::Int) { auto e = mk(Expr::Lit, "", loc); e->lit = SVal::Int(std::strtoll(t.text.c_str(), nullptr, 10)); ++p_; return e; }
        if (t.k == SToken::Float) { auto e = mk(Expr::Lit, "", loc); e->lit = SVal::Float(std::strtod(t.text.c_str(), nullptr)); ++p_; return e; }
        if (t.k == SToken::Str) { auto e = mk(Expr::Lit, "", loc); e->lit = SVal::Text(t.text); ++p_; return e; }
        if (isOp("(")) { ++p_; auto e = expr(); expectOp(")"); return e; }
        if (t.k == SToken::Ident) {
            if (t.text == "null") { ++p_; return mk(Expr::Lit, "", loc); }
            if (t.text == "case") {
                ++p_; auto e = mk(Expr::Case, "", loc);
                ExprP operand; if (!isKw("when") && !isKw("end")) operand = expr();
                if (!isKw("when")) fail("Expected WHEN", cur());
                while (acceptKw("when")) {
                    ExprP c = expr(); expectKw("then"); ExprP v = expr();
                    if (operand) c = bin("=", operand->clone(), std::move(c));   // simple CASE: CASE x WHEN a  ==  CASE WHEN x = a
                    e->args.push_back(std::move(c)); e->args.push_back(std::move(v));
                }
                if (acceptKw("else")) { e->op = "ELSE"; e->args.push_back(expr()); }
                SourceLoc en = cur().loc; expectKw("end"); span(*e, loc, en); return e;
            }
            if (t.text == "true" || t.text == "false") { auto e = mk(Expr::Lit, "", loc); e->lit = SVal::Int(t.text == "true"); ++p_; return e; }
            if (reserved().count(t.text)) fail("Expected an expression", t);
            std::string name = t.text; ++p_;
            if (isOp("(")) {
                ++p_; std::string up = name; for (auto& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                static const std::set<std::string> aggs = {"COUNT", "SUM", "AVG", "MIN", "MAX"};
                auto e = mk(aggs.count(up) ? Expr::Agg : Expr::Func, "", loc); e->name = up;
                if (isOp("*")) { if (up != "COUNT") fail("Expected an expression", cur()); ++p_; e->countStar = true; }
                else if (!isOp(")")) do e->args.push_back(expr()); while (acceptOp(","));
                SourceLoc rp = cur().loc; expectOp(")"); span(*e, loc, rp); return e;
            }
            auto e = mk(Expr::Col, "", loc); e->name = name;
            if (isOp(".")) { ++p_; if (!at(SToken::Ident)) fail("Expected a column name", cur()); e->table = name; e->name = t_[p_].text; span(*e, loc, cur().loc); ++p_; }
            return e;
        }
        fail("Expected an expression", t);
    }
};

} // namespace

ParseOutput parseScript(const std::string& src) {
    ParseOutput out;
    Lexer lx(src, out.diags);
    out.tokens = lx.run();
    out.tokenCount = out.tokens.size() - 1;
    if (out.diags.hasErrors()) return out;       // lexical errors: do not attempt to parse
    Parser p(src, out.tokens, out.diags);
    out.stmts = p.script();
    return out;
}

}} // namespace nova::sql
