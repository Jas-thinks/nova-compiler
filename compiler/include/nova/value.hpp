// Runtime values and the single definition of NOVA arithmetic semantics, shared by the
// constant folder, the bytecode VM and the reference interpreter.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include "nova/ast.hpp"

namespace nova {

struct Value {
    Type type = Type::Int;   // Int | Float | Bool
    int64_t i = 0;
    double f = 0;
    bool b = false;
    static Value Int(int64_t v) { Value x; x.type = Type::Int; x.i = v; return x; }
    static Value Float(double v) { Value x; x.type = Type::Float; x.f = v; return x; }
    static Value Bool(bool v) { Value x; x.type = Type::Bool; x.b = v; return x; }
    bool operator==(const Value& o) const {
        if (type != o.type) return false;
        switch (type) { case Type::Int: return i == o.i; case Type::Float: return f == o.f || (std::isnan(f) && std::isnan(o.f)); case Type::Bool: return b == o.b; default: return true; }
    }
    bool operator!=(const Value& o) const { return !(*this == o); }
    bool isZero() const { return type == Type::Int ? i == 0 : type == Type::Float ? f == 0 : !b; }
    bool isOne() const { return type == Type::Int ? i == 1 : type == Type::Float ? f == 1 : b; }
};

inline std::string formatFloat(double d) {
    if (std::isnan(d)) return "nan";
    if (std::isinf(d)) return d < 0 ? "-inf" : "inf";
    char buf[48]; std::snprintf(buf, sizeof buf, "%.15g", d);
    std::string s = buf;
    if (s.find_first_of(".en") == std::string::npos) s += ".0";
    return s;
}
inline std::string valueToString(const Value& v) {
    switch (v.type) { case Type::Int: return std::to_string(v.i); case Type::Float: return formatFloat(v.f);
                      case Type::Bool: return v.b ? "true" : "false"; default: return "?"; }
}

enum class BinOp { Add, Sub, Mul, Div, Mod, Lt, Le, Gt, Ge, Eq, Ne };
enum class UnOp { Neg, Not, ToFloat, ToInt };

inline const char* binOpText(BinOp o) {
    static const char* t[] = {"+", "-", "*", "/", "%", "<", "<=", ">", ">=", "==", "!="};
    return t[static_cast<int>(o)];
}
inline bool binOpFromText(const std::string& s, BinOp& o) {
    for (int k = 0; k <= static_cast<int>(BinOp::Ne); ++k) if (s == binOpText(static_cast<BinOp>(k))) { o = static_cast<BinOp>(k); return true; }
    return false;
}
inline const char* unOpText(UnOp o) {
    static const char* t[] = {"-", "!", "to_float", "to_int"};
    return t[static_cast<int>(o)];
}
inline bool isCommutative(BinOp o) { return o == BinOp::Add || o == BinOp::Mul || o == BinOp::Eq || o == BinOp::Ne; }

// Returns false and sets err on a runtime error (integer division by zero).
inline bool evalBin(BinOp op, const Value& a, const Value& b, Value& out, std::string& err) {
    if (a.type == Type::Int) {
        uint64_t ua = static_cast<uint64_t>(a.i), ub = static_cast<uint64_t>(b.i);
        switch (op) {
            case BinOp::Add: out = Value::Int(static_cast<int64_t>(ua + ub)); return true;
            case BinOp::Sub: out = Value::Int(static_cast<int64_t>(ua - ub)); return true;
            case BinOp::Mul: out = Value::Int(static_cast<int64_t>(ua * ub)); return true;
            case BinOp::Div:
                if (b.i == 0) { err = "division by zero"; return false; }
                out = Value::Int(b.i == -1 ? static_cast<int64_t>(0 - ua) : a.i / b.i); return true;
            case BinOp::Mod:
                if (b.i == 0) { err = "modulo by zero"; return false; }
                out = Value::Int(b.i == -1 ? 0 : a.i % b.i); return true;
            case BinOp::Lt: out = Value::Bool(a.i < b.i); return true;
            case BinOp::Le: out = Value::Bool(a.i <= b.i); return true;
            case BinOp::Gt: out = Value::Bool(a.i > b.i); return true;
            case BinOp::Ge: out = Value::Bool(a.i >= b.i); return true;
            case BinOp::Eq: out = Value::Bool(a.i == b.i); return true;
            case BinOp::Ne: out = Value::Bool(a.i != b.i); return true;
        }
    } else if (a.type == Type::Float) {
        switch (op) {
            case BinOp::Add: out = Value::Float(a.f + b.f); return true;
            case BinOp::Sub: out = Value::Float(a.f - b.f); return true;
            case BinOp::Mul: out = Value::Float(a.f * b.f); return true;
            case BinOp::Div: out = Value::Float(a.f / b.f); return true;
            case BinOp::Lt: out = Value::Bool(a.f < b.f); return true;
            case BinOp::Le: out = Value::Bool(a.f <= b.f); return true;
            case BinOp::Gt: out = Value::Bool(a.f > b.f); return true;
            case BinOp::Ge: out = Value::Bool(a.f >= b.f); return true;
            case BinOp::Eq: out = Value::Bool(a.f == b.f); return true;
            case BinOp::Ne: out = Value::Bool(a.f != b.f); return true;
            default: break;
        }
    } else {
        if (op == BinOp::Eq) { out = Value::Bool(a.b == b.b); return true; }
        if (op == BinOp::Ne) { out = Value::Bool(a.b != b.b); return true; }
    }
    err = "invalid operand types"; return false;
}

inline Value evalUn(UnOp op, const Value& a) {
    switch (op) {
        case UnOp::Neg: return a.type == Type::Int ? Value::Int(static_cast<int64_t>(0 - static_cast<uint64_t>(a.i))) : Value::Float(-a.f);
        case UnOp::Not: return Value::Bool(!a.b);
        case UnOp::ToFloat: return Value::Float(static_cast<double>(a.i));
        case UnOp::ToInt: {
            double d = a.f;
            if (std::isnan(d)) return Value::Int(0);
            if (d >= 9223372036854775807.0) return Value::Int(std::numeric_limits<int64_t>::max());
            if (d <= -9223372036854775808.0) return Value::Int(std::numeric_limits<int64_t>::min());
            return Value::Int(static_cast<int64_t>(d));
        }
    }
    return a;
}

} // namespace nova
