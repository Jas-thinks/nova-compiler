#include "nova/ir.hpp"

namespace nova {

namespace {

class Gen {
public:
    explicit Gen(Program& p) : prog_(p) {}

    IRProgram run() {
        IRProgram out;
        for (auto& f : prog_.functions) out.functions.push_back(genFunction(*f));
        return out;
    }

private:
    Program& prog_;
    IRFunction* fn_ = nullptr;
    int temps_ = 0, labels_ = 0;
    int line_ = 0, col_ = 0;

    std::string newLabel() { return "L" + std::to_string(++labels_); }
    Operand newTemp(Type t) {
        std::string n = "t" + std::to_string(++temps_);
        fn_->locals.push_back({n, t, true, false});
        return Operand::temp(n, t);
    }
    void at(const Node& n) { line_ = n.loc.line; col_ = n.loc.col; }
    IRInstr& emit(IRInstr i) { i.line = line_; i.col = col_; fn_->code.push_back(std::move(i)); return fn_->code.back(); }
    void label(const std::string& l) { IRInstr i; i.kind = IRInstr::Label; i.target = l; emit(i); }
    void jump(IRInstr::Kind k, const std::string& l, Operand cond = {}) { IRInstr i; i.kind = k; i.target = l; i.a = cond; emit(i); }
    void copy(Operand dst, Operand src) { IRInstr i; i.kind = IRInstr::Copy; i.dst = dst; i.a = src; emit(i); }

    // Emit `dst = callee(args)` (dst only when `result` is not Void) and return dst.
    Operand intrinsic(const std::string& callee, Type result, std::vector<Operand> args) {
        IRInstr i; i.kind = IRInstr::Call; i.callee = callee; i.args = std::move(args);
        Operand t;
        if (result != Type::Void) { t = newTemp(result); i.dst = t; }
        emit(i);
        return t;
    }

    IRFunction genFunction(FuncDecl& fd) {
        IRFunction f; f.name = fd.name; f.ret = fd.ret;
        fn_ = &f; temps_ = 0; labels_ = 0;
        for (auto& p : fd.params) {
            f.params.push_back({p->sym ? p->sym->uniqueName : p->name, p->type});
            f.locals.push_back({p->sym ? p->sym->uniqueName : p->name, p->type, false, true});
        }
        at(fd);
        for (auto& s : fd.body->stmts) genStmt(*s);
        line_ = fd.body->loc.endLine; col_ = fd.body->loc.endCol - 1;
        IRInstr r; r.kind = IRInstr::Return;
        if (fd.ret != Type::Void) {
            if (fd.ret == Type::Str) r.a = Operand::strConst("");
            else if (isArrayType(fd.ret)) { r.a = intrinsic("__newarr", fd.ret, {Operand::cnst(Value::Int(0))}); }   // unreachable: semantic analysis proves every path returns
            else r.a = Operand::cnst(fd.ret == Type::Int ? Value::Int(0) : fd.ret == Type::Float ? Value::Float(0) : Value::Bool(false));
        }
        emit(r);
        fn_ = nullptr;
        return f;
    }

    void genStmt(Stmt& s) {
        at(s);
        switch (s.kind) {
            case NodeKind::Block: for (auto& c : static_cast<Block&>(s).stmts) genStmt(*c); break;
            case NodeKind::VarDecl: {
                auto& d = static_cast<VarDecl&>(s);
                Operand v = genExpr(*d.init);
                fn_->locals.push_back({d.sym->uniqueName, d.declType, false, false});
                at(s); copy(Operand::var(d.sym->uniqueName, d.declType), v);
                break;
            }
            case NodeKind::Assign: {
                auto& a = static_cast<Assign&>(s);
                Operand v = genExpr(*a.value);
                at(s); copy(Operand::var(a.sym->uniqueName, a.sym->type), v);
                break;
            }
            case NodeKind::IndexAssign: {
                auto& a = static_cast<IndexAssign&>(s);
                Operand idx = genExpr(*a.index);
                Operand v = genExpr(*a.value);
                at(s); intrinsic("__astore", Type::Void, {Operand::var(a.sym->uniqueName, a.sym->type), idx, v});
                break;
            }
            case NodeKind::If: {
                auto& i = static_cast<If&>(s);
                Operand c = genExpr(*i.cond);
                std::string lEnd = newLabel();
                std::string lElse = i.elseB ? newLabel() : lEnd;
                at(s); jump(IRInstr::JumpIfFalse, lElse, c);
                genStmt(*i.thenB);
                if (i.elseB) {
                    at(s); jump(IRInstr::Jump, lEnd);
                    label(lElse);
                    genStmt(*i.elseB);
                }
                label(lEnd);
                break;
            }
            case NodeKind::While: {
                auto& w = static_cast<While&>(s);
                std::string lStart = newLabel(), lEnd = newLabel();
                label(lStart);
                Operand c = genExpr(*w.cond);
                at(s); jump(IRInstr::JumpIfFalse, lEnd, c);
                genStmt(*w.body);
                at(s); jump(IRInstr::Jump, lStart);
                label(lEnd);
                break;
            }
            case NodeKind::Return: {
                auto& r = static_cast<Return&>(s);
                IRInstr i; i.kind = IRInstr::Return;
                if (r.value) i.a = genExpr(*r.value);
                at(s); emit(i);
                break;
            }
            case NodeKind::Print: {
                auto& p = static_cast<Print&>(s);
                IRInstr i; i.kind = IRInstr::Print; i.a = genExpr(*p.value);
                at(s); emit(i);
                break;
            }
            case NodeKind::ExprStmt: genExpr(*static_cast<ExprStmt&>(s).expr, true); break;
            default: break;
        }
    }

    Operand genExpr(Expr& e, bool discard = false) {
        switch (e.kind) {
            case NodeKind::IntLit: return Operand::cnst(Value::Int(static_cast<IntLit&>(e).value));
            case NodeKind::FloatLit: return Operand::cnst(Value::Float(static_cast<FloatLit&>(e).value));
            case NodeKind::BoolLit: return Operand::cnst(Value::Bool(static_cast<BoolLit&>(e).value));
            case NodeKind::StrLit: return Operand::strConst(static_cast<StrLit&>(e).value);
            case NodeKind::NewArray: {
                auto& n = static_cast<NewArray&>(e);
                Operand sz = genExpr(*n.size);
                at(e); return intrinsic("__newarr", e.type, {sz});
            }
            case NodeKind::ArrayLit: {
                auto& a = static_cast<ArrayLit&>(e);
                at(e);
                Operand arr = intrinsic("__newarr", e.type, {Operand::cnst(Value::Int(static_cast<int64_t>(a.elems.size())))});
                for (size_t k = 0; k < a.elems.size(); ++k) {
                    Operand v = genExpr(*a.elems[k]);
                    at(*a.elems[k]); intrinsic("__astore", Type::Void, {arr, Operand::cnst(Value::Int(static_cast<int64_t>(k))), v});
                }
                return arr;
            }
            case NodeKind::Index: {
                auto& x = static_cast<Index&>(e);
                Operand b = genExpr(*x.base), i = genExpr(*x.index);
                at(e); return intrinsic("__aload", e.type, {b, i});
            }
            case NodeKind::VarRef: { auto& v = static_cast<VarRef&>(e); return Operand::var(v.sym->uniqueName, v.sym->type); }
            case NodeKind::Unary: {
                auto& u = static_cast<Unary&>(e);
                Operand a = genExpr(*u.operand);
                Operand t = newTemp(e.type);
                IRInstr i; i.kind = IRInstr::Un; i.uop = u.op == "-" ? UnOp::Neg : UnOp::Not; i.dst = t; i.a = a;
                at(e); emit(i); return t;
            }
            case NodeKind::Binary: {
                auto& b = static_cast<Binary&>(e);
                if (b.op == "&&" || b.op == "||") {
                    Operand t = newTemp(Type::Bool);
                    std::string lEnd = newLabel();
                    Operand l = genExpr(*b.lhs);
                    at(e); copy(t, l);
                    jump(b.op == "&&" ? IRInstr::JumpIfFalse : IRInstr::JumpIfTrue, lEnd, t);
                    Operand r = genExpr(*b.rhs);
                    at(e); copy(t, r);
                    label(lEnd);
                    return t;
                }
                Operand l = genExpr(*b.lhs), r = genExpr(*b.rhs);
                Operand t = newTemp(e.type);
                IRInstr i; i.kind = IRInstr::Bin; binOpFromText(b.op, i.bop); i.dst = t; i.a = l; i.b = r;
                at(e); emit(i); return t;
            }
            case NodeKind::Call: {
                auto& c = static_cast<Call&>(e);
                std::vector<Operand> args;
                for (auto& a : c.args) args.push_back(genExpr(*a));
                if (c.builtin && c.name == "len") { at(e); return intrinsic("__len", Type::Int, {args[0]}); }
                if (c.builtin) {
                    Operand t = newTemp(e.type);
                    IRInstr i; i.kind = IRInstr::Un; i.uop = c.name == "to_float" ? UnOp::ToFloat : UnOp::ToInt; i.dst = t; i.a = args[0];
                    at(e); emit(i); return t;
                }
                IRInstr i; i.kind = IRInstr::Call; i.callee = c.name; i.args = args;
                Operand t;
                if (e.type != Type::Void) { t = newTemp(e.type); i.dst = t; }
                at(e); emit(i);
                return t;
            }
            default: return Operand::none();
        }
    }
};

} // namespace

IRProgram generateIR(Program& program) { return Gen(program).run(); }

} // namespace nova
