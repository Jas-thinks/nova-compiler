#include "nova/ast.hpp"
#include <cstdlib>
#include <functional>

namespace nova {

const char* typeName(Type t) {
    switch (t) { case Type::Int: return "int"; case Type::Float: return "float"; case Type::Bool: return "bool";
                 case Type::Void: return "void"; case Type::Error: return "<error>"; }
    return "?";
}

const char* nodeKindName(NodeKind k) {
    switch (k) {
        case NodeKind::Program: return "Program"; case NodeKind::Function: return "Function"; case NodeKind::Param: return "Param";
        case NodeKind::Block: return "Block"; case NodeKind::VarDecl: return "VarDecl"; case NodeKind::Assign: return "Assign";
        case NodeKind::If: return "If"; case NodeKind::While: return "While"; case NodeKind::Return: return "Return";
        case NodeKind::Print: return "Print"; case NodeKind::ExprStmt: return "ExprStmt"; case NodeKind::IntLit: return "IntLit";
        case NodeKind::FloatLit: return "FloatLit"; case NodeKind::BoolLit: return "BoolLit"; case NodeKind::VarRef: return "VarRef";
        case NodeKind::Binary: return "Binary"; case NodeKind::Unary: return "Unary"; case NodeKind::Call: return "Call";
    }
    return "?";
}

Json Node::toJson(int& nextId) const {
    Json j = Json::object();
    j.set("id", nextId++).set("kind", nodeKindName(kind)).set("label", label()).set("loc", loc.toJson());
    if (dynamic_cast<const Expr*>(this)) j.set("type", typeName(exprType()));
    Json ch = Json::array();
    for (Node* c : const_cast<Node*>(this)->children()) if (c) ch.push(c->toJson(nextId));
    j.set("children", ch);
    return j;
}

namespace {

using PN = ParseNode;

const PN& ch(const PN& n, size_t i) { return *n.children[i]; }

Type typeOf(const PN& t) {
    const std::string& l = ch(t, 0).lexeme;
    return l == "int" ? Type::Int : l == "float" ? Type::Float : Type::Bool;
}

ExprP expr(const PN& n);
std::unique_ptr<Block> block(const PN& n);
StmtP stmt(const PN& n);

std::unique_ptr<Call> call(const PN& n) {  // Call -> ID ( ArgsOpt )
    auto c = std::make_unique<Call>();
    c->loc = n.loc; c->name = ch(n, 0).lexeme; c->nameLoc = ch(n, 0).loc;
    const PN& opt = ch(n, 2);
    if (!opt.children.empty()) {
        std::vector<const PN*> args;
        const PN* cur = &ch(opt, 0);               // Args
        while (true) {
            if (cur->children.size() == 3) { args.push_back(&ch(*cur, 2)); cur = &ch(*cur, 0); }
            else { args.push_back(&ch(*cur, 0)); break; }
        }
        for (auto it = args.rbegin(); it != args.rend(); ++it) c->args.push_back(expr(**it));
    }
    return c;
}

ExprP expr(const PN& n) {
    const std::string& s = n.symbol;
    if (s == "Expr" || (n.children.size() == 1 && s != "Primary" && s != "Call")) return expr(ch(n, 0));
    if (s == "Primary") {
        const PN& f = ch(n, 0);
        if (f.isToken) {
            if (f.symbol == "(") return expr(ch(n, 1));
            if (f.symbol == "INT_LIT") { auto e = std::make_unique<IntLit>(); e->loc = n.loc; e->value = std::strtoll(f.lexeme.c_str(), nullptr, 10); return e; }
            if (f.symbol == "FLOAT_LIT") { auto e = std::make_unique<FloatLit>(); e->loc = n.loc; e->text = f.lexeme; e->value = std::strtod(f.lexeme.c_str(), nullptr); return e; }
            if (f.symbol == "true" || f.symbol == "false") { auto e = std::make_unique<BoolLit>(); e->loc = n.loc; e->value = f.symbol == "true"; return e; }
            auto e = std::make_unique<VarRef>(); e->loc = n.loc; e->name = f.lexeme; return e;
        }
        return expr(f);  // Call
    }
    if (s == "Call") { return call(n); }
    if (s == "UnaryExpr") {  // - UnaryExpr | ! UnaryExpr
        auto e = std::make_unique<Unary>(); e->loc = n.loc; e->op = ch(n, 0).lexeme; e->operand = expr(ch(n, 1)); return e;
    }
    // left-recursive binary levels
    auto e = std::make_unique<Binary>(); e->loc = n.loc; e->op = ch(n, 1).lexeme;
    e->lhs = expr(ch(n, 0)); e->rhs = expr(ch(n, 2));
    return e;
}

std::unique_ptr<Block> block(const PN& n) {  // Block -> { StmtList }
    auto b = std::make_unique<Block>(); b->loc = n.loc;
    std::vector<const PN*> list;
    const PN* cur = &ch(n, 1);
    while (!cur->children.empty()) { list.push_back(&ch(*cur, 1)); cur = &ch(*cur, 0); }
    for (auto it = list.rbegin(); it != list.rend(); ++it) b->stmts.push_back(stmt(**it));
    return b;
}

std::unique_ptr<If> ifStmt(const PN& n) {  // IfStmt -> if ( Expr ) Block ElsePart
    auto s = std::make_unique<If>(); s->loc = n.loc;
    s->cond = expr(ch(n, 2)); s->thenB = block(ch(n, 4));
    const PN& e = ch(n, 5);
    if (!e.children.empty()) {
        if (ch(e, 1).symbol == "Block") s->elseB = block(ch(e, 1)); else s->elseB = ifStmt(ch(e, 1));
    }
    return s;
}

StmtP stmt(const PN& n) {  // Stmt -> X
    const PN& x = ch(n, 0);
    const std::string& s = x.symbol;
    if (s == "Block") return block(x);
    if (s == "VarDecl") {
        auto d = std::make_unique<VarDecl>(); d->loc = x.loc; d->name = ch(x, 1).lexeme; d->nameLoc = ch(x, 1).loc;
        d->declType = typeOf(ch(x, 3)); d->init = expr(ch(x, 5)); return d;
    }
    if (s == "Assign") {
        auto a = std::make_unique<Assign>(); a->loc = x.loc; a->name = ch(x, 0).lexeme; a->nameLoc = ch(x, 0).loc; a->value = expr(ch(x, 2)); return a;
    }
    if (s == "IfStmt") return ifStmt(x);
    if (s == "WhileStmt") {
        auto w = std::make_unique<While>(); w->loc = x.loc; w->cond = expr(ch(x, 2)); w->body = block(ch(x, 4)); return w;
    }
    if (s == "ReturnStmt") {
        auto r = std::make_unique<Return>(); r->loc = x.loc;
        const PN& o = ch(x, 1);
        if (!o.children.empty()) r->value = expr(ch(o, 0));
        return r;
    }
    if (s == "PrintStmt") { auto p = std::make_unique<Print>(); p->loc = x.loc; p->value = expr(ch(x, 2)); return p; }
    // CallStmt
    auto e = std::make_unique<ExprStmt>(); e->loc = x.loc; e->expr = call(ch(x, 0)); return e;
}

std::unique_ptr<FuncDecl> func(const PN& n) {  // fn ID ( ParamsOpt ) RetOpt Block
    auto f = std::make_unique<FuncDecl>(); f->loc = n.loc; f->name = ch(n, 1).lexeme; f->nameLoc = ch(n, 1).loc;
    const PN& po = ch(n, 3);
    if (!po.children.empty()) {
        std::vector<const PN*> ps;
        const PN* cur = &ch(po, 0);
        while (true) {
            if (cur->children.size() == 3) { ps.push_back(&ch(*cur, 2)); cur = &ch(*cur, 0); }
            else { ps.push_back(&ch(*cur, 0)); break; }
        }
        for (auto it = ps.rbegin(); it != ps.rend(); ++it) {
            auto p = std::make_unique<Param>(); p->loc = (*it)->loc; p->name = ch(**it, 0).lexeme; p->type = typeOf(ch(**it, 2));
            f->params.push_back(std::move(p));
        }
    }
    const PN& ro = ch(n, 5);
    f->ret = ro.children.empty() ? Type::Void : typeOf(ch(ro, 1));
    f->body = block(ch(n, 6));
    return f;
}

} // namespace

std::unique_ptr<Program> buildAst(const ParseNode& root) {
    auto p = std::make_unique<Program>(); p->loc = root.loc;
    std::vector<const PN*> fs;
    const PN* cur = &ch(root, 0);  // FuncList
    while (true) {
        if (cur->children.size() == 2) { fs.push_back(&ch(*cur, 1)); cur = &ch(*cur, 0); }
        else { fs.push_back(&ch(*cur, 0)); break; }
    }
    for (auto it = fs.rbegin(); it != fs.rend(); ++it) p->functions.push_back(func(**it));
    return p;
}


std::vector<std::string> validateAst(Node* root, bool requireTypes) {
    std::vector<std::string> problems;
    auto before = [](int l1, int c1, int l2, int c2) { return l1 < l2 || (l1 == l2 && c1 <= c2); };
    std::function<void(Node*, Node*)> go = [&](Node* n, Node* parent) {
        if (!n) { problems.push_back("null child under " + (parent ? parent->label() : std::string("<root>"))); return; }
        const SourceLoc& l = n->loc;
        if (l.line < 1 || l.col < 1) problems.push_back("invalid start location on " + n->label());
        else if (!before(l.line, l.col, l.endLine, l.endCol)) problems.push_back("end before start on " + n->label());
        if (parent && l.line >= 1 && parent->loc.line >= 1) {
            const SourceLoc& p = parent->loc;
            if (!before(p.line, p.col, l.line, l.col) || !before(l.endLine, l.endCol, p.endLine, p.endCol))
                problems.push_back(n->label() + " lies outside its parent " + parent->label());
        }
        if (requireTypes) if (auto* e = dynamic_cast<Expr*>(n)) { if (e->type == Type::Error) problems.push_back("untyped expression " + n->label()); }
        for (Node* c : n->children()) go(c, n);
    };
    go(root, nullptr);
    return problems;
}

std::string astToSexpr(Node* n) {
    if (!n) return "(null)";
    std::string s = "(" + n->label();
    for (Node* c : n->children()) s += " " + astToSexpr(c);
    return s + ")";
}

} // namespace nova
