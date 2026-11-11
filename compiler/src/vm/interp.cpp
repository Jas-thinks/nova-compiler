#include "nova/interp.hpp"
#include <map>
#include <unordered_map>

#include <sstream>

namespace nova {

namespace {

// ---------------------------------------------------------------------------------------------
// Independent re-implementation of NOVA's primitive semantics (deliberately NOT using value.hpp's
// evalBin/evalUn/valueToString): integers go through 128-bit arithmetic and are truncated to 64 bits,
// formatting uses iostreams. Float +,-,*,/ and comparisons are IEEE-754 double operations by definition
// of the language, so they are necessarily the same hardware operations in every engine.
// ---------------------------------------------------------------------------------------------
using i128 = __int128;
int64_t wrap64(i128 v) { return static_cast<int64_t>(static_cast<uint64_t>(static_cast<unsigned __int128>(v) & 0xFFFFFFFFFFFFFFFFull)); }

std::string refFormat(const Value& v) {
    if (v.type == Type::Bool) return v.b ? "true" : "false";
    if (v.type == Type::Int) { std::ostringstream o; o << v.i; return o.str(); }
    double d = v.f;
    if (d != d) return "nan";
    if (d > 1.7976931348623157e308) return "inf";
    if (d < -1.7976931348623157e308) return "-inf";
    std::ostringstream o; o.precision(15); o << d;
    std::string s = o.str();
    bool plain = true;
    for (char c : s) if (c == '.' || c == 'e') plain = false;
    return plain ? s + ".0" : s;
}

struct RuntimeFail { std::string msg; bool limit; };

Value refUnary(bool minus, const Value& a) {
    if (!minus) return Value::Bool(!a.b);
    if (a.type == Type::Float) return Value::Float(-a.f);
    return Value::Int(wrap64(-static_cast<i128>(a.i)));
}
Value refCast(bool toFloat, const Value& a) {
    if (toFloat) return Value::Float(static_cast<double>(a.i));
    double d = a.f;
    if (d != d) return Value::Int(0);
    if (d >= 9223372036854775808.0) return Value::Int(INT64_MAX);
    if (d < -9223372036854775808.0) return Value::Int(INT64_MIN);
    return Value::Int(static_cast<int64_t>(d));      // in range: C++ conversion truncates toward zero
}
Value refBinary(const std::string& op, const Value& l, const Value& r) {
    if (l.type == Type::Int) {
        i128 a = l.i, b = r.i;
        if (op == "+") return Value::Int(wrap64(a + b));
        if (op == "-") return Value::Int(wrap64(a - b));
        if (op == "*") return Value::Int(wrap64(a * b));
        if (op == "/") { if (b == 0) throw RuntimeFail{"division by zero", false}; return Value::Int(wrap64(a / b)); }
        if (op == "%") { if (b == 0) throw RuntimeFail{"modulo by zero", false}; return Value::Int(wrap64(a % b)); }
        if (op == "<") return Value::Bool(a < b);
        if (op == "<=") return Value::Bool(a <= b);
        if (op == ">") return Value::Bool(a > b);
        if (op == ">=") return Value::Bool(a >= b);
        if (op == "==") return Value::Bool(a == b);
        return Value::Bool(a != b);
    }
    if (l.type == Type::Float) {
        double a = l.f, b = r.f;
        if (op == "+") return Value::Float(a + b);
        if (op == "-") return Value::Float(a - b);
        if (op == "*") return Value::Float(a * b);
        if (op == "/") return Value::Float(a / b);
        if (op == "<") return Value::Bool(a < b);
        if (op == "<=") return Value::Bool(a <= b);
        if (op == ">") return Value::Bool(a > b);
        if (op == ">=") return Value::Bool(a >= b);
        if (op == "==") return Value::Bool(a == b);
        return Value::Bool(a != b);
    }
    return Value::Bool(op == "==" ? l.b == r.b : l.b != r.b);
}

class Interp {
public:
    Interp(Program& p, int64_t maxSteps, int maxDepth) : prog_(p), maxSteps_(maxSteps), maxDepth_(maxDepth) {}

    InterpResult run() {
        InterpResult r;
        try {
            FuncDecl* m = find("main");
            if (!m) throw RuntimeFail{"no main", false};
            callFunction(*m, {});
        } catch (RuntimeFail& f) {
            r.ok = false; r.error = f.msg; r.limitHit = f.limit;
        }
        r.output = std::move(out_);
        return r;
    }

private:
    Program& prog_;
    int64_t maxSteps_, steps_ = 0;
    int maxDepth_, depth_ = 0;
    std::vector<std::string> out_;
    // Independent heap: strings and arrays are separate vectors indexed by the handle stored in Value::i.
    std::vector<std::string> strs_;
    std::vector<std::vector<Value>> arrs_;
    int64_t cells_ = 0;
    static constexpr int64_t kMaxCells = 1 << 22;
    Value newStr(std::string s) {
        cells_ += 1 + static_cast<int64_t>(s.size() / 8);
        if (cells_ > kMaxCells) throw RuntimeFail{"heap limit exceeded", true};
        strs_.push_back(std::move(s));
        Value v; v.type = Type::Str; v.i = static_cast<int64_t>(strs_.size()) - 1; return v;
    }
    Value newArr(Type t, int64_t n) {
        if (n < 0) throw RuntimeFail{"negative array size", false};
        if (cells_ + n + 1 > kMaxCells) throw RuntimeFail{"heap limit exceeded", true};
        cells_ += n + 1;
        Value z; z.type = elemType(t);
        arrs_.emplace_back(static_cast<size_t>(n), z);
        Value v; v.type = t; v.i = static_cast<int64_t>(arrs_.size()) - 1; return v;
    }
    using Env = std::unordered_map<const Symbol*, Value>;
    Env* env_ = nullptr;
    bool returning_ = false;
    Value retVal_;

    FuncDecl* find(const std::string& n) { for (auto& f : prog_.functions) if (f->name == n) return f.get(); return nullptr; }
    void tick() { if (++steps_ > maxSteps_) throw RuntimeFail{"step limit", true}; }

    Value callFunction(FuncDecl& f, const std::vector<Value>& args) {
        if (depth_ >= maxDepth_) throw RuntimeFail{"stack overflow", true};
        ++depth_;
        Env env; Env* saved = env_; env_ = &env;
        for (size_t i = 0; i < f.params.size(); ++i) env[f.params[i]->sym] = args[i];
        returning_ = false; retVal_ = Value();
        execBlock(*f.body);
        Value rv = retVal_;
        returning_ = false;
        env_ = saved; --depth_;
        return rv;
    }

    void execBlock(Block& b) { for (auto& s : b.stmts) { exec(*s); if (returning_) return; } }

    void exec(Stmt& s) {
        tick();
        switch (s.kind) {
            case NodeKind::Block: execBlock(static_cast<Block&>(s)); break;
            case NodeKind::VarDecl: { auto& d = static_cast<VarDecl&>(s); (*env_)[d.sym] = eval(*d.init); break; }
            case NodeKind::Assign: { auto& a = static_cast<Assign&>(s); (*env_)[a.sym] = eval(*a.value); break; }
            case NodeKind::IndexAssign: {
                auto& a = static_cast<IndexAssign&>(s);
                Value idx = eval(*a.index), v = eval(*a.value);
                auto& arr = arrs_[(*env_)[a.sym].i];
                if (idx.i < 0 || idx.i >= static_cast<int64_t>(arr.size())) throw RuntimeFail{"index out of bounds", false};
                arr[idx.i] = v; break;
            }
            case NodeKind::If: {
                auto& i = static_cast<If&>(s);
                if (eval(*i.cond).b) execBlock(*i.thenB); else if (i.elseB) exec(*i.elseB);
                break;
            }
            case NodeKind::While: {
                auto& w = static_cast<While&>(s);
                while (eval(*w.cond).b) { tick(); execBlock(*w.body); if (returning_) return; }
                break;
            }
            case NodeKind::Return: {
                auto& r = static_cast<Return&>(s);
                Value v; if (r.value) v = eval(*r.value);
                retVal_ = v; returning_ = true; break;
            }
            case NodeKind::Print: {
                if (out_.size() >= 5000) throw RuntimeFail{"output limit", true};
                Value pv = eval(*static_cast<Print&>(s).value);
                out_.push_back(pv.type == Type::Str ? strs_[pv.i] : refFormat(pv)); break;
            }
            case NodeKind::ExprStmt: eval(*static_cast<ExprStmt&>(s).expr); break;
            default: break;
        }
    }

    Value eval(Expr& e) {
        tick();
        switch (e.kind) {
            case NodeKind::IntLit: return Value::Int(static_cast<IntLit&>(e).value);
            case NodeKind::FloatLit: return Value::Float(static_cast<FloatLit&>(e).value);
            case NodeKind::BoolLit: return Value::Bool(static_cast<BoolLit&>(e).value);
            case NodeKind::StrLit: return newStr(static_cast<StrLit&>(e).value);
            case NodeKind::NewArray: { auto& n = static_cast<NewArray&>(e); return newArr(e.type, eval(*n.size).i); }
            case NodeKind::ArrayLit: {
                auto& a = static_cast<ArrayLit&>(e);
                Value arr = newArr(e.type, static_cast<int64_t>(a.elems.size()));
                for (size_t k = 0; k < a.elems.size(); ++k) { Value v = eval(*a.elems[k]); arrs_[arr.i][k] = v; }
                return arr;
            }
            case NodeKind::Index: {
                auto& x = static_cast<Index&>(e);
                Value b = eval(*x.base), i = eval(*x.index);
                auto& arr = arrs_[b.i];
                if (i.i < 0 || i.i >= static_cast<int64_t>(arr.size())) throw RuntimeFail{"index out of bounds", false};
                return arr[i.i];
            }
            case NodeKind::VarRef: return (*env_)[static_cast<VarRef&>(e).sym];
            case NodeKind::Unary: {
                auto& u = static_cast<Unary&>(e);
                return refUnary(u.op == "-", eval(*u.operand));
            }
            case NodeKind::Binary: {
                auto& b = static_cast<Binary&>(e);
                if (b.op == "&&") { Value l = eval(*b.lhs); return l.b ? eval(*b.rhs) : l; }
                if (b.op == "||") { Value l = eval(*b.lhs); return l.b ? l : eval(*b.rhs); }
                Value l = eval(*b.lhs), r = eval(*b.rhs);
                if (l.type == Type::Str) {
                    if (b.op == "+") return newStr(strs_[l.i] + strs_[r.i]);
                    return Value::Bool((strs_[l.i] == strs_[r.i]) == (b.op == "=="));
                }
                return refBinary(b.op, l, r);
            }
            case NodeKind::Call: {
                auto& c = static_cast<Call&>(e);
                std::vector<Value> args;
                for (auto& a : c.args) args.push_back(eval(*a));
                if (c.builtin && c.name == "len") return Value::Int(args[0].type == Type::Str ? static_cast<int64_t>(strs_[args[0].i].size()) : static_cast<int64_t>(arrs_[args[0].i].size()));
                if (c.builtin) return refCast(c.name == "to_float", args[0]);
                return callFunction(*find(c.name), args);
            }
            default: return Value();
        }
    }
};

} // namespace

InterpResult interpret(Program& program, int64_t maxSteps, int maxDepth) { return Interp(program, maxSteps, maxDepth).run(); }

} // namespace nova
