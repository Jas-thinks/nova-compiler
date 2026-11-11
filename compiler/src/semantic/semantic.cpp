#include "nova/semantic.hpp"
#include <algorithm>
#include <functional>

namespace nova {

namespace {

class Analyzer {
public:
    Analyzer(Program& p, SemanticResult& r) : prog_(p), res_(r) {}

    void run() {
        newScope(-1, "global", "", prog_.loc);
        addBuiltin("to_float", {Type::Int}, Type::Float);
        addBuiltin("to_int", {Type::Float}, Type::Int);
        addBuiltin("len", {Type::Error}, Type::Int);   // polymorphic: string or any array (checked specially in checkCall)
        for (auto& f : prog_.functions) declareFunction(*f);
        const FuncInfo* m = res_.findFunction("main");
        if (!m) {
            err("Missing entry point", "E-SEM-010", "Program has no 'main' function",
                "Execution starts at a function named 'main'.", "Add: fn main() { ... }", prog_.loc);
        } else if (!m->params.empty() || m->ret != Type::Void) {
            err("Invalid entry point", "E-SEM-011", "'main' must take no parameters and return no value",
                "The VM calls main() with no arguments and ignores any result.", "Declare it as: fn main() { ... }", m->loc);
        }
        for (auto& f : prog_.functions) checkFunction(*f);
        // Warnings: unused locals.
        for (auto& s : res_.symbols) {
            if (s->kind == Symbol::Variable && s->reads.empty()) {
                warn("Unused variable", "W-SEM-001", "Variable '" + s->name + "' is declared but never read",
                     "The value is stored but no expression ever uses it. The optimizer's dead-code elimination will remove it.",
                     "Remove the variable or use its value.", s->declLoc);
            }
        }
    }

private:
    Program& prog_;
    SemanticResult& res_;
    int cur_ = 0;
    FuncDecl* curFunc_ = nullptr;
    std::map<std::string, int> uniq_;
    bool hadReturnWarn_ = false;

    void err(const std::string& cat, const std::string& code, const std::string& msg, const std::string& expl,
             const std::string& sugg, SourceLoc loc, Severity sev = Severity::Error) {
        Diagnostic d; d.severity = sev; d.phase = "semantic"; d.category = cat; d.code = code; d.message = msg;
        d.explanation = expl; d.suggestion = sugg; d.loc = loc;
        res_.diagnostics.add(std::move(d));
    }
    void warn(const std::string& cat, const std::string& code, const std::string& msg, const std::string& expl,
              const std::string& sugg, SourceLoc loc) { err(cat, code, msg, expl, sugg, loc, Severity::Warning); }

    int newScope(int parent, const std::string& kind, const std::string& owner, SourceLoc loc) {
        auto s = std::make_unique<Scope>();
        s->id = static_cast<int>(res_.scopes.size()); s->parent = parent; s->kind = kind; s->owner = owner; s->loc = loc;
        if (parent >= 0) res_.scopes[parent]->children.push_back(s->id);
        res_.scopes.push_back(std::move(s));
        return res_.scopes.back()->id;
    }

    void addBuiltin(const std::string& n, std::vector<Type> params, Type ret) {
        auto f = std::make_unique<FuncInfo>();
        f->name = n; f->params = params; f->paramNames = {"x"}; f->ret = ret; f->builtin = true;
        res_.functions.push_back(std::move(f));
    }

    void declareFunction(FuncDecl& fd) {
        if (const FuncInfo* prev = res_.findFunction(fd.name)) {
            err("Duplicate declaration", "E-SEM-003", "Function '" + fd.name + "' is already defined" + (prev->builtin ? " as a built-in" : ""),
                "Function names must be unique in a program (NOVA has no overloading).", "Rename this function.", fd.nameLoc);
            return;
        }
        auto f = std::make_unique<FuncInfo>();
        f->name = fd.name; f->ret = fd.ret; f->loc = fd.nameLoc;
        for (auto& p : fd.params) { f->params.push_back(p->type); f->paramNames.push_back(p->name); }
        res_.functions.push_back(std::move(f));
    }

    Symbol* declare(const std::string& name, Type t, Symbol::Kind k, SourceLoc loc) {
        Scope& sc = *res_.scopes[cur_];
        for (Symbol* s : sc.symbols) {
            if (s->name == name) {
                err("Duplicate declaration", "E-SEM-002", "'" + name + "' is already declared in this scope (line " + std::to_string(s->declLoc.line) + ")",
                    "A name can be declared only once per scope. Inner scopes may shadow outer names, but not redeclare within the same scope.",
                    "Use a different name, or assign to the existing variable with '" + name + " = ...;'.", loc);
                return nullptr;
            }
        }
        auto s = std::make_unique<Symbol>();
        s->name = name; s->type = t; s->kind = k; s->declLoc = loc; s->scopeId = cur_; s->function = curFunc_ ? curFunc_->name : "";
        int n = uniq_[name]++;
        s->uniqueName = n == 0 ? name : name + "." + std::to_string(n);
        Symbol* raw = s.get();
        sc.symbols.push_back(raw);
        res_.symbols.push_back(std::move(s));
        return raw;
    }

    Symbol* lookup(const std::string& name) {
        for (int s = cur_; s >= 0; s = res_.scopes[s]->parent)
            for (Symbol* sym : res_.scopes[s]->symbols) if (sym->name == name) return sym;
        return nullptr;
    }
    std::vector<std::string> visibleNames() {
        std::vector<std::string> v;
        for (int s = cur_; s >= 0; s = res_.scopes[s]->parent) for (Symbol* sym : res_.scopes[s]->symbols) v.push_back(sym->name);
        return v;
    }

    void checkFunction(FuncDecl& fd) {
        // Skip duplicate bodies that were rejected at declaration time? Still check them for errors.
        curFunc_ = &fd; uniq_.clear();
        int saved = cur_;
        cur_ = newScope(0, "function", fd.name, fd.loc);
        fd.scopeId = cur_;
        for (auto& p : fd.params) p->sym = declare(p->name, p->type, Symbol::Parameter, p->loc);
        checkBlock(*fd.body, false);  // body shares the function scope
        if (fd.ret != Type::Void && !blockReturns(*fd.body)) {
            SourceLoc l = fd.body->loc; l.line = fd.body->loc.endLine; l.col = fd.body->loc.endCol - 1; l.endLine = l.line; l.endCol = l.col + 1;
            err("Missing return", "E-SEM-009", "Function '" + fd.name + "' may reach its end without returning a value of type " + typeName(fd.ret),
                "Every control-flow path of a function with return type " + std::string(typeName(fd.ret)) + " must end in 'return <value>;'. Loops are assumed to possibly run zero times.",
                "Add 'return <" + std::string(typeName(fd.ret)) + " value>;' at the end of the function.", l);
        }
        cur_ = saved; curFunc_ = nullptr;
    }

    bool blockReturns(Block& b) { for (auto& s : b.stmts) if (stmtReturns(*s)) return true; return false; }
    bool stmtReturns(Stmt& s) {
        switch (s.kind) {
            case NodeKind::Return: return true;
            case NodeKind::Block: return blockReturns(static_cast<Block&>(s));
            case NodeKind::If: { auto& i = static_cast<If&>(s); return i.elseB && blockReturns(*i.thenB) && stmtReturns(*i.elseB); }
            default: return false;
        }
    }

    void checkBlock(Block& b, bool newScopeNeeded = true) {
        int saved = cur_;
        if (newScopeNeeded) { cur_ = newScope(cur_, "block", curFunc_->name, b.loc); }
        b.scopeId = cur_;
        bool returned = false, warned = false;
        for (auto& s : b.stmts) {
            if (returned && !warned) {
                warn("Unreachable code", "W-SEM-002", "Statement is unreachable", "It follows a 'return' in the same block, so it can never execute.",
                     "Remove the statement or move it before the 'return'.", s->loc);
                warned = true;
            }
            checkStmt(*s);
            if (s->kind == NodeKind::Return) returned = true;
        }
        cur_ = saved;
    }

    void expectBool(Expr& e, const char* what) {
        Type t = checkExpr(e);
        if (t != Type::Bool && t != Type::Error)
            err("Type mismatch", "E-SEM-004", std::string(what) + " must be bool but has type " + typeName(t),
                "Conditions in NOVA are strictly boolean; there is no implicit conversion from numbers.",
                t == Type::Int ? "Compare explicitly, e.g. (x != 0)." : "Use a relational operator such as '>' to produce a bool.", e.loc);
    }

    void checkStmt(Stmt& s) {
        switch (s.kind) {
            case NodeKind::Block: checkBlock(static_cast<Block&>(s)); break;
            case NodeKind::VarDecl: {
                auto& d = static_cast<VarDecl&>(s);
                Type it = checkExpr(*d.init);   // initializer is checked before the name is in scope
                if (it != Type::Error && it != d.declType) {
                    if (it == Type::Void) {}  // reported in checkExpr
                    else err("Type mismatch", "E-SEM-004", "Cannot initialise '" + d.name + "' of type " + typeName(d.declType) + " with a value of type " + typeName(it),
                             "NOVA is statically typed and performs no implicit conversions.",
                             it == Type::Int && d.declType == Type::Float ? "Use to_float(<expr>) or write a float literal like 1.0."
                             : it == Type::Float && d.declType == Type::Int ? "Use to_int(<expr>) to truncate explicitly."
                             : std::string("Change the declared type to ") + typeName(it) + " or the initializer.", d.init->loc);
                }
                d.sym = declare(d.name, d.declType, Symbol::Variable, d.nameLoc);
                break;
            }
            case NodeKind::Assign: {
                auto& a = static_cast<Assign&>(s);
                Symbol* sym = resolveVar(a.name, a.nameLoc);
                Type vt = checkExpr(*a.value);
                if (sym) {
                    a.sym = sym; sym->writes.push_back(a.nameLoc);
                    if (vt != Type::Error && vt != Type::Void && vt != sym->type)
                        err("Type mismatch", "E-SEM-004", "Cannot assign " + std::string(typeName(vt)) + " to '" + a.name + "' of type " + typeName(sym->type),
                            "The type of a variable is fixed at declaration.",
                            vt == Type::Int && sym->type == Type::Float ? "Use to_float(<expr>)." : vt == Type::Float && sym->type == Type::Int ? "Use to_int(<expr>)." : "Assign a value of type " + std::string(typeName(sym->type)) + ".",
                            a.value->loc);
                }
                break;
            }
            case NodeKind::IndexAssign: {
                auto& a = static_cast<IndexAssign&>(s);
                Symbol* sym = resolveVar(a.name, a.nameLoc);
                Type it = checkExpr(*a.index), vt = checkExpr(*a.value);
                if (sym) {
                    a.sym = sym; sym->reads.push_back(a.nameLoc);
                    if (!isArrayType(sym->type))
                        err("Type mismatch", "E-SEM-014", "'" + a.name + "' has type " + typeName(sym->type) + " and cannot be indexed",
                            "Only arrays support 'x[i] = value'. Strings are immutable.", "Declare an array: let " + a.name + ": int[] = [1, 2, 3];", a.nameLoc);
                    else if (vt != Type::Error && vt != Type::Void && vt != elemType(sym->type))
                        err("Type mismatch", "E-SEM-004", "Cannot assign " + std::string(typeName(vt)) + " to an element of " + typeName(sym->type),
                            "Every element of an array has the array's element type.", std::string("Assign a ") + typeName(elemType(sym->type)) + " value.", a.value->loc);
                }
                if (it != Type::Error && it != Type::Int)
                    err("Type mismatch", "E-SEM-015", std::string("Array index must be int but has type ") + typeName(it), "Indices are integers.", "Use an int expression.", a.index->loc);
                break;
            }
            case NodeKind::If: {
                auto& i = static_cast<If&>(s);
                expectBool(*i.cond, "If condition");
                checkBlock(*i.thenB);
                if (i.elseB) checkStmt(*i.elseB);
                break;
            }
            case NodeKind::While: {
                auto& w = static_cast<While&>(s);
                expectBool(*w.cond, "While condition");
                checkBlock(*w.body);
                break;
            }
            case NodeKind::Return: {
                auto& r = static_cast<Return&>(s);
                if (r.value) {
                    Type t = checkExpr(*r.value);
                    if (curFunc_->ret == Type::Void)
                        err("Invalid return", "E-SEM-008", "Function '" + curFunc_->name + "' returns no value but 'return' has an expression",
                            "A function without a '-> type' clause cannot return a value.", "Remove the expression or declare a return type: fn " + curFunc_->name + "(...) -> " + typeName(t) + ".", r.value->loc);
                    else if (t != Type::Error && t != curFunc_->ret)
                        err("Type mismatch", "E-SEM-004", "Returning " + std::string(typeName(t)) + " from function '" + curFunc_->name + "' declared to return " + typeName(curFunc_->ret),
                            "The returned expression must match the declared return type.", "Return a " + std::string(typeName(curFunc_->ret)) + " value.", r.value->loc);
                } else if (curFunc_->ret != Type::Void) {
                    err("Invalid return", "E-SEM-008", "Function '" + curFunc_->name + "' must return a " + typeName(curFunc_->ret),
                        "'return;' with no value is only allowed in functions without a return type.", std::string("Write 'return <") + typeName(curFunc_->ret) + " value>;'.", r.loc);
                }
                break;
            }
            case NodeKind::Print: {
                auto& p = static_cast<Print&>(s);
                Type t = checkExpr(*p.value);
                if (isArrayType(t))
                    err("Type mismatch", "E-SEM-016", std::string("Cannot print a value of type ") + typeName(t), "print accepts int, float, bool and string values.",
                        "Print the elements one by one with a loop.", p.value->loc);
                break;
            }
            case NodeKind::ExprStmt: checkExpr(*static_cast<ExprStmt&>(s).expr); break;
            default: break;
        }
    }

    Symbol* resolveVar(const std::string& name, SourceLoc loc) {
        Symbol* s = lookup(name);
        if (s) return s;
        if (res_.findFunction(name)) {
            err("Invalid use of function", "E-SEM-012", "'" + name + "' is a function, not a variable", "Functions can only be called, e.g. " + name + "(...).", "Add an argument list: " + name + "(...).", loc);
            return nullptr;
        }
        std::string sug = closestMatch(name, visibleNames());
        err("Undefined variable", "E-SEM-001", "Variable '" + name + "' is not declared",
            "A variable must be declared with 'let' in the current or an enclosing scope before it is used.",
            sug.empty() ? "Declare it first: let " + name + ": int = 0;" : "Did you mean '" + sug + "'?", loc);
        return nullptr;
    }

    static bool numeric(Type t) { return t == Type::Int || t == Type::Float; }

    Type checkExpr(Expr& e) {
        switch (e.kind) {
            case NodeKind::IntLit: case NodeKind::FloatLit: case NodeKind::BoolLit: case NodeKind::StrLit: return e.type;
            case NodeKind::NewArray: {
                auto& n = static_cast<NewArray&>(e);
                Type st = checkExpr(*n.size);
                if (st != Type::Error && st != Type::Int)
                    err("Type mismatch", "E-SEM-015", std::string("Array size must be int but has type ") + typeName(st), "The size in 'new T[n]' is an integer.", "Use an int expression.", n.size->loc);
                return e.type = arrayOf(n.elem);
            }
            case NodeKind::ArrayLit: {
                auto& a = static_cast<ArrayLit&>(e);
                Type first = Type::Error; bool ok = true;
                for (size_t k = 0; k < a.elems.size(); ++k) {
                    Type t = checkExpr(*a.elems[k]);
                    if (t == Type::Error) { ok = false; continue; }
                    if (k == 0) first = t;
                    else if (t != first) {
                        err("Type mismatch", "E-SEM-004", std::string("Array element has type ") + typeName(t) + " but earlier elements are " + typeName(first),
                            "All elements of an array literal must have the same type.", "Make the elements the same type.", a.elems[k]->loc);
                        ok = false;
                    }
                }
                if (ok && first != Type::Error && arrayOf(first) == Type::Error) {
                    err("Type mismatch", "E-SEM-017", std::string("Arrays of ") + typeName(first) + " are not supported", "Array elements must be int, float or bool.", "Use an element type of int, float or bool.", a.loc);
                    ok = false;
                }
                return e.type = ok ? arrayOf(first) : Type::Error;
            }
            case NodeKind::Index: {
                auto& x = static_cast<Index&>(e);
                Type bt = checkExpr(*x.base), it = checkExpr(*x.index);
                if (it != Type::Error && it != Type::Int)
                    err("Type mismatch", "E-SEM-015", std::string("Array index must be int but has type ") + typeName(it), "Indices are integers.", "Use an int expression.", x.index->loc);
                if (bt == Type::Error) return e.type = Type::Error;
                if (!isArrayType(bt)) {
                    err("Type mismatch", "E-SEM-014", std::string("Cannot index a value of type ") + typeName(bt), "Only arrays can be indexed with '[i]'. Use len(s) for string length.", "Index an array.", x.loc);
                    return e.type = Type::Error;
                }
                return e.type = elemType(bt);
            }
            case NodeKind::VarRef: {
                auto& v = static_cast<VarRef&>(e);
                Symbol* s = resolveVar(v.name, v.loc);
                if (!s) return e.type = Type::Error;
                v.sym = s; s->reads.push_back(v.loc);
                return e.type = s->type;
            }
            case NodeKind::Unary: {
                auto& u = static_cast<Unary&>(e);
                Type t = checkExpr(*u.operand);
                if (t == Type::Error) return e.type = Type::Error;
                if (u.op == "-" && numeric(t)) return e.type = t;
                if (u.op == "!" && t == Type::Bool) return e.type = Type::Bool;
                err("Type mismatch", "E-SEM-005", "Operator '" + u.op + "' cannot be applied to " + typeName(t),
                    u.op == "-" ? "Unary minus needs an int or float operand." : "Logical not needs a bool operand.",
                    u.op == "-" ? "Use a numeric operand." : "Compare first, e.g. !(x > 0).", u.loc);
                return e.type = Type::Error;
            }
            case NodeKind::Binary: return checkBinary(static_cast<Binary&>(e));
            case NodeKind::Call: return checkCall(static_cast<Call&>(e));
            default: return Type::Error;
        }
    }

    Type checkBinary(Binary& b) {
        Type l = checkExpr(*b.lhs), r = checkExpr(*b.rhs);
        if (l == Type::Error || r == Type::Error) return b.type = (b.op == "<" || b.op == ">" || b.op == "<=" || b.op == ">=" || b.op == "==" || b.op == "!=" || b.op == "&&" || b.op == "||") ? Type::Bool : Type::Error;
        const std::string& op = b.op;
        bool arith = op == "+" || op == "-" || op == "*" || op == "/";
        bool rel = op == "<" || op == ">" || op == "<=" || op == ">=";
        bool eq = op == "==" || op == "!=";
        bool logic = op == "&&" || op == "||";
        std::string sugg;
        if (l != r && numeric(l) && numeric(r))
            sugg = std::string("NOVA has no implicit int/float conversion. Wrap the int operand in to_float(...) or the float operand in to_int(...).");
        if (logic) {
            if (l == Type::Bool && r == Type::Bool) return b.type = Type::Bool;
            return bad(b, l, r, "'" + op + "' requires two bool operands.", "Compare the operands first, e.g. (x > 0) " + op + " (y > 0).");
        }
        if (l != r) return bad(b, l, r, "Both operands of '" + op + "' must have the same type.", sugg.empty() ? "Make both operands the same type." : sugg);
        if (l == Type::Str) {
            if (op == "+") return b.type = Type::Str;
            if (eq) return b.type = Type::Bool;
            return bad(b, l, r, "Strings support only '+' (concatenation), '==' and '!='.", "Use '+' to join strings.");
        }
        if (isArrayType(l)) return bad(b, l, r, "Arrays cannot be combined or compared with operators.", "Compare or combine elements individually.");
        if (arith) {
            if (numeric(l)) return b.type = l;
            return bad(b, l, r, "Arithmetic needs int or float operands.", "Use numeric operands.");
        }
        if (op == "%") {
            if (l == Type::Int) return b.type = Type::Int;
            return bad(b, l, r, "'%' is defined for int operands only.", "Use int operands.");
        }
        if (rel) {
            if (numeric(l)) return b.type = Type::Bool;
            return bad(b, l, r, "Ordering comparisons need int or float operands.", "Compare numbers, or use '==' / '!=' for bools.");
        }
        if (eq) return b.type = Type::Bool;
        return b.type = Type::Error;
    }

    Type bad(Binary& b, Type l, Type r, const std::string& expl, const std::string& sugg) {
        err("Type mismatch", "E-SEM-005", "Operator '" + b.op + "' cannot be applied to " + typeName(l) + " and " + typeName(r), expl, sugg, b.loc);
        bool boolResult = b.op == "<" || b.op == ">" || b.op == "<=" || b.op == ">=" || b.op == "==" || b.op == "!=" || b.op == "&&" || b.op == "||";
        return b.type = boolResult ? Type::Bool : Type::Error;
    }

    Type checkCall(Call& c) {
        std::vector<Type> at;
        for (auto& a : c.args) at.push_back(checkExpr(*a));
        FuncInfo* f = nullptr;
        for (auto& fi : res_.functions) if (fi->name == c.name) f = fi.get();
        if (!f) {
            std::vector<std::string> names;
            for (auto& fi : res_.functions) names.push_back(fi->name);
            std::string sug = closestMatch(c.name, names);
            if (lookup(c.name)) err("Invalid call", "E-SEM-012", "'" + c.name + "' is a variable, not a function", "Only functions can be called.", "Remove the parentheses.", c.nameLoc);
            else err("Undefined function", "E-SEM-006", "Function '" + c.name + "' is not defined", "Calls must refer to a function declared somewhere in the program (declaration order does not matter).",
                     sug.empty() ? "Define it: fn " + c.name + "(...) { ... }" : "Did you mean '" + sug + "'?", c.nameLoc);
            return c.type = Type::Error;
        }
        c.callee = f; f->callCount++; c.builtin = f->builtin;
        if (f->builtin && f->name == "len") {
            if (c.args.size() != 1) err("Wrong argument count", "E-SEM-007", "Function 'len' expects 1 argument(s) but " + std::to_string(c.args.size()) + " were given",
                                        "len(x) returns the length of a string or array.", "Pass exactly one string or array.", c.loc);
            else if (at[0] != Type::Error && at[0] != Type::Str && !isArrayType(at[0]))
                err("Type mismatch", "E-SEM-004", std::string("Argument 1 of 'len' has type ") + typeName(at[0]) + " but a string or array is required",
                    "len works on strings and arrays.", "Pass a string or an array.", c.args[0]->loc);
            return c.type = Type::Int;
        }
        if (c.args.size() != f->params.size()) {
            std::string sig = f->name + "(";
            for (size_t i = 0; i < f->params.size(); ++i) sig += (i ? ", " : "") + std::string(typeName(f->params[i]));
            sig += ")";
            err("Wrong argument count", "E-SEM-007", "Function '" + c.name + "' expects " + std::to_string(f->params.size()) + " argument(s) but " + std::to_string(c.args.size()) + " were given",
                "The number of arguments must equal the number of declared parameters: " + sig + ".", c.args.size() < f->params.size() ? "Add the missing argument(s)." : "Remove the extra argument(s).", c.loc);
        } else {
            for (size_t i = 0; i < c.args.size(); ++i)
                if (at[i] != Type::Error && at[i] != f->params[i])
                    err("Type mismatch", "E-SEM-004", "Argument " + std::to_string(i + 1) + " of '" + c.name + "' has type " + typeName(at[i]) + " but parameter '" + f->paramNames[i] + "' is " + typeName(f->params[i]),
                        "Argument types must match parameter types exactly.",
                        at[i] == Type::Int && f->params[i] == Type::Float ? "Use to_float(<expr>)." : "Pass a " + std::string(typeName(f->params[i])) + " value.", c.args[i]->loc);
        }
        return c.type = f->ret;
    }

public:
    // Statement-position void calls are fine; void used as a value is an error. Done after typing.
    void checkVoidUses() {
        std::function<void(Node*, bool)> go = [&](Node* n, bool valueUse) {
            if (!n) return;
            if (n->kind == NodeKind::Call && valueUse) {
                auto* c = static_cast<Call*>(n);
                if (c->type == Type::Void)
                    err("Void value used", "E-SEM-013", "Function '" + c->name + "' returns no value, but its result is used",
                        "Functions declared without '-> type' produce no value.", "Call it as a statement, or give it a return type.", c->loc);
            }
            bool isStmtCall = n->kind == NodeKind::ExprStmt;
            for (Node* ch : n->children()) go(ch, !isStmtCall && n->kind != NodeKind::Block && n->kind != NodeKind::Function && n->kind != NodeKind::Program ? true : false);
        };
        go(&prog_, false);
    }
};

} // namespace

std::unique_ptr<SemanticResult> analyze(Program& program) {
    auto res = std::make_unique<SemanticResult>();
    Analyzer a(program, *res);
    a.run();
    a.checkVoidUses();
    return res;
}

Json SemanticResult::toJson() const {
    Json j = Json::object();
    Json sc = Json::array();
    for (auto& s : scopes) {
        Json o = Json::object();
        o.set("id", s->id).set("parent", s->parent).set("kind", s->kind).set("owner", s->owner).set("loc", s->loc.toJson());
        Json syms = Json::array();
        for (Symbol* y : s->symbols) {
            Json sy = Json::object();
            sy.set("name", y->name).set("uniqueName", y->uniqueName).set("type", typeName(y->type))
              .set("kind", y->kind == Symbol::Parameter ? "parameter" : "variable").set("declLoc", y->declLoc.toJson());
            Json rd = Json::array(), wr = Json::array();
            for (auto& l : y->reads) rd.push(l.toJson());
            for (auto& l : y->writes) wr.push(l.toJson());
            sy.set("reads", rd).set("writes", wr).set("readCount", static_cast<int>(y->reads.size())).set("writeCount", static_cast<int>(y->writes.size()));
            syms.push(sy);
        }
        Json ch = Json::array();
        for (int c : s->children) ch.push(c);
        o.set("symbols", syms).set("children", ch);
        sc.push(o);
    }
    j.set("scopes", sc);
    Json fs = Json::array();
    for (auto& f : functions) {
        Json o = Json::object();
        Json ps = Json::array();
        for (size_t i = 0; i < f->params.size(); ++i) { Json p = Json::object(); p.set("name", f->paramNames[i]).set("type", typeName(f->params[i])); ps.push(p); }
        o.set("name", f->name).set("params", ps).set("returnType", typeName(f->ret)).set("builtin", f->builtin).set("calls", f->callCount);
        if (!f->builtin) o.set("loc", f->loc.toJson());
        fs.push(o);
    }
    j.set("functions", fs);
    return j;
}

} // namespace nova
