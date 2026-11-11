// Three-address-code intermediate representation (structured instructions, not strings).
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "nova/semantic.hpp"
#include "nova/value.hpp"

namespace nova {

struct Operand {
    enum Kind { None, Const, Var, Temp } kind = None;
    Type type = Type::Error;
    Value val;           // for Const
    std::string name;    // for Var / Temp
    static Operand none() { return {}; }
    static Operand cnst(const Value& v) { Operand o; o.kind = Const; o.type = v.type; o.val = v; return o; }
    // String constant: the text lives in `name`; val.i is a content hash so Value equality stays meaningful.
    static Operand strConst(const std::string& s) { Operand o; o.kind = Const; o.type = Type::Str; o.val.type = Type::Str; o.val.i = static_cast<int64_t>(std::hash<std::string>{}(s)); o.name = s; return o; }
    static Operand var(const std::string& n, Type t) { Operand o; o.kind = Var; o.name = n; o.type = t; return o; }
    static Operand temp(const std::string& n, Type t) { Operand o; o.kind = Temp; o.name = n; o.type = t; return o; }
    bool isNone() const { return kind == None; }
    bool isConst() const { return kind == Const; }
    bool isName() const { return kind == Var || kind == Temp; }
    bool operator==(const Operand& o) const {
        if (kind != o.kind) return false;
        if (kind == Const) return val == o.val && (type != Type::Str || name == o.name);
        return kind == None || name == o.name;
    }
    bool operator!=(const Operand& o) const { return !(*this == o); }
    std::string str() const;
    Json toJson() const;
};

struct IRInstr {
    enum Kind { Copy, Bin, Un, Label, Jump, JumpIfFalse, JumpIfTrue, Call, Return, Print } kind = Copy;
    BinOp bop = BinOp::Add;
    UnOp uop = UnOp::Neg;
    Operand dst, a, b;
    std::string target;               // label name (Label / Jump*)
    std::string callee;               // Call
    std::vector<Operand> args;        // Call
    int line = 0, col = 0;            // originating source position (0 if synthesized)

    std::string str() const;
    std::string explain() const;
    bool definesName() const { return dst.isName(); }
    Json toJson(int index) const;
    // Reads: operands that are inputs.
    template <class F> void forEachUse(F&& f) const {
        if (a.isName()) f(a);
        if (b.isName()) f(b);
        for (auto& x : args) if (x.isName()) f(x);
    }
};

// Heap/string operations are expressed as Calls to intrinsics so every pass that understands calls (use/def sets,
// substitution, SSA renaming, "never removable") handles them for free:
//   dst = __newarr(n)   dst = __aload(arr, i)   __astore(arr, i, v)   dst = __len(x)
inline bool isIntrinsic(const std::string& callee) { return callee.size() > 2 && callee[0] == '_' && callee[1] == '_'; }

struct LocalInfo { std::string name; Type type; bool isTemp; bool isParam; };

struct IRFunction {
    std::string name;
    std::vector<std::pair<std::string, Type>> params;
    Type ret = Type::Void;
    std::vector<LocalInfo> locals;     // params + variables + temporaries
    std::vector<IRInstr> code;
    Json toJson() const;
};

struct IRProgram {
    std::vector<IRFunction> functions;
    size_t instructionCount() const { size_t n = 0; for (auto& f : functions) n += f.code.size(); return n; }
    size_t countOf(IRInstr::Kind k) const { size_t n = 0; for (auto& f : functions) for (auto& i : f.code) if (i.kind == k) ++n; return n; }
    Json toJson() const;
    std::string text() const;
};

IRProgram generateIR(Program& program);

} // namespace nova
