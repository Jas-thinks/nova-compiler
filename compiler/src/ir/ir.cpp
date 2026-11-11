#include "nova/ir.hpp"
#include <sstream>

namespace nova {

Json Operand::toJson() const {
    Json j = Json::object();
    const char* k = kind == None ? "none" : kind == Const ? "const" : kind == Var ? "var" : "temp";
    j.set("kind", k).set("text", str()).set("type", typeName(type));
    return j;
}

std::string IRInstr::str() const {
    switch (kind) {
        case Copy: return dst.str() + " = " + a.str();
        case Bin: return dst.str() + " = " + a.str() + " " + binOpText(bop) + " " + b.str();
        case Un:
            if (uop == UnOp::ToFloat || uop == UnOp::ToInt) return dst.str() + " = " + unOpText(uop) + "(" + a.str() + ")";
            return dst.str() + " = " + unOpText(uop) + a.str();
        case Label: return target + ":";
        case Jump: return "goto " + target;
        case JumpIfFalse: return "if_false " + a.str() + " goto " + target;
        case JumpIfTrue: return "if_true " + a.str() + " goto " + target;
        case Call: {
            std::string s = dst.isNone() ? "" : dst.str() + " = ";
            s += "call " + callee + "(";
            for (size_t i = 0; i < args.size(); ++i) s += (i ? ", " : "") + args[i].str();
            return s + ")";
        }
        case Return: return a.isNone() ? "return" : "return " + a.str();
        case Print: return "print " + a.str();
    }
    return "?";
}

std::string IRInstr::explain() const {
    switch (kind) {
        case Copy: return "Copy the value of " + a.str() + " into " + dst.str() + ".";
        case Bin: return "Compute " + a.str() + " " + binOpText(bop) + " " + b.str() + " and store the result in " + dst.str() + (dst.kind == Operand::Temp ? " (a compiler temporary)." : ".");
        case Un: return uop == UnOp::ToFloat ? "Convert int " + a.str() + " to float, store in " + dst.str() + "."
                      : uop == UnOp::ToInt ? "Truncate float " + a.str() + " to int, store in " + dst.str() + "."
                      : uop == UnOp::Neg ? "Negate " + a.str() + ", store in " + dst.str() + "." : "Logical NOT of " + a.str() + ", store in " + dst.str() + ".";
        case Label: return "Jump target " + target + " (starts a basic block).";
        case Jump: return "Unconditional jump to " + target + ".";
        case JumpIfFalse: return "Jump to " + target + " when " + a.str() + " is false; otherwise fall through.";
        case JumpIfTrue: return "Jump to " + target + " when " + a.str() + " is true; otherwise fall through.";
        case Call: return "Call function " + callee + " with " + std::to_string(args.size()) + " argument(s)" + (dst.isNone() ? "." : "; the result goes to " + dst.str() + ".");
        case Return: return a.isNone() ? "Return from the function without a value." : "Return " + a.str() + " to the caller.";
        case Print: return "Print " + a.str() + " to program output.";
    }
    return "";
}

Json IRInstr::toJson(int index) const {
    Json j = Json::object();
    j.set("index", index).set("text", str()).set("explanation", explain());
    static const char* kn[] = {"copy", "binary", "unary", "label", "jump", "jump_if_false", "jump_if_true", "call", "return", "print"};
    j.set("kind", kn[kind]);
    if (kind == Bin) j.set("op", binOpText(bop));
    if (kind == Un) j.set("op", unOpText(uop));
    if (!dst.isNone()) j.set("dst", dst.toJson());
    if (!a.isNone()) j.set("a", a.toJson());
    if (!b.isNone()) j.set("b", b.toJson());
    if (!target.empty()) j.set("target", target);
    if (!callee.empty()) j.set("callee", callee);
    if (line > 0) j.set("line", line).set("col", col);
    return j;
}

Json IRFunction::toJson() const {
    Json j = Json::object();
    j.set("name", name).set("returnType", typeName(ret));
    Json ps = Json::array();
    for (auto& p : params) { Json o = Json::object(); o.set("name", p.first).set("type", typeName(p.second)); ps.push(o); }
    j.set("params", ps);
    Json code_ = Json::array();
    for (size_t i = 0; i < code.size(); ++i) code_.push(code[i].toJson(static_cast<int>(i)));
    j.set("instructions", code_).set("count", static_cast<int>(code.size()));
    return j;
}

Json IRProgram::toJson() const {
    Json j = Json::object();
    Json fs = Json::array();
    for (auto& f : functions) fs.push(f.toJson());
    j.set("functions", fs).set("instructionCount", static_cast<int>(instructionCount()));
    return j;
}

std::string IRProgram::text() const {
    std::ostringstream os;
    for (auto& f : functions) {
        os << "function " << f.name << "(";
        for (size_t i = 0; i < f.params.size(); ++i) os << (i ? ", " : "") << f.params[i].first << ": " << typeName(f.params[i].second);
        os << ") -> " << typeName(f.ret) << "\n";
        for (auto& i : f.code) os << (i.kind == IRInstr::Label ? "" : "    ") << i.str() << "\n";
    }
    return os.str();
}

} // namespace nova
