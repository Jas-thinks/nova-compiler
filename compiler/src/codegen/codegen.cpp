#include "nova/bytecode.hpp"
#include <algorithm>
#include <map>
#include <sstream>

namespace nova {

const char* opName(OpCode op) {
    static const char* n[] = {"PUSH", "POP", "LOAD", "STORE", "ADD", "SUB", "MUL", "DIV", "MOD", "NEG", "NOT", "LT", "LE", "GT", "GE", "EQ", "NE",
                              "I2F", "F2I", "JMP", "JZ", "JNZ", "CALL", "RET", "RETV", "PRINT",
                              "PUSHS", "NEWARR", "ALOAD", "ASTORE", "ALEN", "SCAT", "SEQ", "HALT"};
    return n[static_cast<int>(op)];
}

const char* opDescription(OpCode op) {
    switch (op) {
        case OpCode::PUSH: return "Push a constant onto the operand stack.";
        case OpCode::POP: return "Discard the top of the operand stack.";
        case OpCode::LOAD: return "Push the value of a local slot onto the operand stack.";
        case OpCode::STORE: return "Pop the top of the stack into a local slot.";
        case OpCode::ADD: return "Pop b, pop a, push a + b.";
        case OpCode::SUB: return "Pop b, pop a, push a - b.";
        case OpCode::MUL: return "Pop b, pop a, push a * b.";
        case OpCode::DIV: return "Pop b, pop a, push a / b (integer division traps on zero).";
        case OpCode::MOD: return "Pop b, pop a, push a % b (traps on zero).";
        case OpCode::NEG: return "Pop a, push -a.";
        case OpCode::NOT: return "Pop a, push logical not a.";
        case OpCode::LT: return "Pop b, pop a, push a < b.";
        case OpCode::LE: return "Pop b, pop a, push a <= b.";
        case OpCode::GT: return "Pop b, pop a, push a > b.";
        case OpCode::GE: return "Pop b, pop a, push a >= b.";
        case OpCode::EQ: return "Pop b, pop a, push a == b.";
        case OpCode::NE: return "Pop b, pop a, push a != b.";
        case OpCode::I2F: return "Pop an int, push it converted to float.";
        case OpCode::F2I: return "Pop a float, push it truncated to int.";
        case OpCode::JMP: return "Jump unconditionally to an address.";
        case OpCode::JZ: return "Pop a bool; jump if it is false.";
        case OpCode::JNZ: return "Pop a bool; jump if it is true.";
        case OpCode::CALL: return "Pop N arguments into a new call frame and jump to the function.";
        case OpCode::RET: return "Pop the return value, destroy the frame, push the value in the caller.";
        case OpCode::RETV: return "Destroy the frame and return to the caller (no value).";
        case OpCode::PRINT: return "Pop a value and append it to the program output.";
        case OpCode::PUSHS: return "Push a reference to string literal #a from the constant pool.";
        case OpCode::NEWARR: return "Pop a size n, allocate a zero-initialised heap array of n elements, push its reference (traps if n < 0).";
        case OpCode::ALOAD: return "Pop index, pop array, push array[index] (traps when out of bounds).";
        case OpCode::ASTORE: return "Pop value, pop index, pop array; array[index] = value (traps when out of bounds).";
        case OpCode::ALEN: return "Pop a string or array reference, push its length.";
        case OpCode::SCAT: return "Pop b, pop a (strings), push a new string a + b.";
        case OpCode::SEQ: return "Pop b, pop a (strings), push whether their contents are equal.";
        case OpCode::HALT: return "Stop the virtual machine.";
    }
    return "";
}

OpCode opFromBin(BinOp b) {
    switch (b) {
        case BinOp::Add: return OpCode::ADD; case BinOp::Sub: return OpCode::SUB; case BinOp::Mul: return OpCode::MUL;
        case BinOp::Div: return OpCode::DIV; case BinOp::Mod: return OpCode::MOD; case BinOp::Lt: return OpCode::LT;
        case BinOp::Le: return OpCode::LE; case BinOp::Gt: return OpCode::GT; case BinOp::Ge: return OpCode::GE;
        case BinOp::Eq: return OpCode::EQ; case BinOp::Ne: return OpCode::NE;
    }
    return OpCode::ADD;
}

namespace {

struct Emitter {
    BcProgram& p;
    std::map<std::string, int> slot;
    std::map<std::string, int> labelAddr;
    std::vector<std::pair<size_t, std::string>> fixups;
    int line = 0, col = 0;

    size_t emit(OpCode op, int a = 0) {
        BcInstr i; i.op = op; i.a = a; i.line = line; i.col = col;
        p.code.push_back(i);
        return p.code.size() - 1;
    }
    void push(const Operand& o) {
        if (o.isConst() && o.type == Type::Str) {
            auto it = std::find(p.strings.begin(), p.strings.end(), o.name);
            int idx = static_cast<int>(it - p.strings.begin());
            if (it == p.strings.end()) p.strings.push_back(o.name);
            emit(OpCode::PUSHS, idx);
        }
        else if (o.isConst()) { size_t k = emit(OpCode::PUSH); p.code[k].k = o.val; }
        else emit(OpCode::LOAD, slot.at(o.name));
    }
};

} // namespace

BcProgram generateBytecode(const IRProgram& ir) {
    BcProgram p;
    Emitter em{p, {}, {}, {}, 0, 0};
    // Entry stub: CALL main; HALT. Function indices equal IR function order.
    std::map<std::string, int> fidx;
    for (size_t i = 0; i < ir.functions.size(); ++i) fidx[ir.functions[i].name] = static_cast<int>(i);
    p.mainIndex = fidx.count("main") ? fidx["main"] : -1;
    size_t callMain = em.emit(OpCode::CALL, p.mainIndex);
    p.code[callMain].argc = 0;
    em.emit(OpCode::HALT);
    for (auto& f : ir.functions) {
        BcFunction bf; bf.name = f.name; bf.entry = static_cast<int>(p.code.size());
        bf.paramCount = static_cast<int>(f.params.size()); bf.returnsValue = f.ret != Type::Void;
        em.slot.clear(); em.labelAddr.clear(); em.fixups.clear();
        auto addSlot = [&](const std::string& n, Type t, bool temp) {
            if (em.slot.count(n)) return;
            em.slot[n] = static_cast<int>(bf.slotNames.size());
            bf.slotNames.push_back(n); bf.slotTypes.push_back(t); bf.slotIsTemp.push_back(temp);
        };
        for (auto& pr : f.params) addSlot(pr.first, pr.second, false);
        // Allocate slots for every name still referenced (optimized-away variables get no slot).
        std::map<std::string, LocalInfo> info;
        for (auto& l : f.locals) info[l.name] = l;
        auto want = [&](const Operand& o) {
            if (!o.isName()) return;
            auto it = info.find(o.name);
            addSlot(o.name, o.type, it != info.end() ? it->second.isTemp : o.kind == Operand::Temp);
        };
        for (auto& i : f.code) { want(i.dst); want(i.a); want(i.b); for (auto& a : i.args) want(a); }
        for (auto& i : f.code) {
            em.line = i.line; em.col = i.col;
            switch (i.kind) {
                case IRInstr::Copy: em.push(i.a); em.emit(OpCode::STORE, em.slot.at(i.dst.name)); break;
                case IRInstr::Bin:
                    em.push(i.a); em.push(i.b);
                    if (i.a.type == Type::Str) {                     // string operators have dedicated opcodes
                        em.emit(i.bop == BinOp::Add ? OpCode::SCAT : OpCode::SEQ);
                        if (i.bop == BinOp::Ne) em.emit(OpCode::NOT);
                    } else em.emit(opFromBin(i.bop));
                    em.emit(OpCode::STORE, em.slot.at(i.dst.name)); break;
                case IRInstr::Un:
                    em.push(i.a);
                    em.emit(i.uop == UnOp::Neg ? OpCode::NEG : i.uop == UnOp::Not ? OpCode::NOT : i.uop == UnOp::ToFloat ? OpCode::I2F : OpCode::F2I);
                    em.emit(OpCode::STORE, em.slot.at(i.dst.name)); break;
                case IRInstr::Label: em.labelAddr[i.target] = static_cast<int>(p.code.size()); break;
                case IRInstr::Jump: em.fixups.push_back({em.emit(OpCode::JMP), i.target}); break;
                case IRInstr::JumpIfFalse: em.push(i.a); em.fixups.push_back({em.emit(OpCode::JZ), i.target}); break;
                case IRInstr::JumpIfTrue: em.push(i.a); em.fixups.push_back({em.emit(OpCode::JNZ), i.target}); break;
                case IRInstr::Call: {
                    if (isIntrinsic(i.callee)) {
                        for (auto& a : i.args) em.push(a);
                        if (i.callee == "__newarr") { size_t k = em.emit(OpCode::NEWARR); p.code[k].k.type = i.dst.type; }
                        else if (i.callee == "__aload") em.emit(OpCode::ALOAD);
                        else if (i.callee == "__astore") em.emit(OpCode::ASTORE);
                        else em.emit(OpCode::ALEN);
                        if (i.dst.isName()) em.emit(OpCode::STORE, em.slot.at(i.dst.name));
                        break;
                    }
                    for (auto& a : i.args) em.push(a);
                    size_t k = em.emit(OpCode::CALL, fidx.at(i.callee));
                    p.code[k].argc = static_cast<int>(i.args.size());
                    bool callee_returns = ir.functions[fidx.at(i.callee)].ret != Type::Void;
                    if (i.dst.isName()) em.emit(OpCode::STORE, em.slot.at(i.dst.name));
                    else if (callee_returns) em.emit(OpCode::POP);
                    break;
                }
                case IRInstr::Return:
                    if (i.a.isNone()) em.emit(OpCode::RETV); else { em.push(i.a); em.emit(OpCode::RET); }
                    break;
                case IRInstr::Print: em.push(i.a); em.emit(OpCode::PRINT); break;
            }
        }
        for (auto& [at, label] : em.fixups) p.code[at].a = em.labelAddr.at(label);
        // A label at the very end of a function would point past it; guarantee a landing instruction.
        bool needsLanding = false;
        for (auto& [l, a] : em.labelAddr) if (a == static_cast<int>(p.code.size())) needsLanding = true;
        if (needsLanding || f.code.empty() || (f.code.back().kind != IRInstr::Return && f.code.back().kind != IRInstr::Jump)) {
            em.line = f.code.empty() ? 0 : f.code.back().line; em.col = 0;
            em.emit(bf.returnsValue ? OpCode::HALT : OpCode::RETV);  // unreachable for well-typed programs
        }
        bf.end = static_cast<int>(p.code.size());
        p.functions.push_back(std::move(bf));
    }
    return p;
}

int BcProgram::functionAt(int addr) const {
    for (size_t i = 0; i < functions.size(); ++i) if (addr >= functions[i].entry && addr < functions[i].end) return static_cast<int>(i);
    return -1;
}

std::string BcProgram::comment(int addr) const {
    const BcInstr& i = code[addr];
    int fi = functionAt(addr);
    switch (i.op) {
        case OpCode::LOAD: case OpCode::STORE: return fi >= 0 ? functions[fi].slotNames[i.a] : "";
        case OpCode::JMP: case OpCode::JZ: case OpCode::JNZ: return "-> " + std::to_string(i.a);
        case OpCode::PUSHS: return i.a >= 0 && i.a < static_cast<int>(strings.size()) ? "\"" + strings[i.a] + "\"" : "";
        case OpCode::CALL: return i.a >= 0 ? functions[i.a].name + " (" + std::to_string(i.argc) + " args)" : "";
        default: return "";
    }
}

std::string BcProgram::disasm(int addr) const {
    const BcInstr& i = code[addr];
    std::string s = opName(i.op);
    switch (i.op) {
        case OpCode::PUSH: s += " " + valueToString(i.k); break;
        case OpCode::PUSHS: s += " " + std::to_string(i.a); break;
        case OpCode::NEWARR: s += std::string(" ") + typeName(i.k.type); break;
        case OpCode::LOAD: case OpCode::STORE: case OpCode::JMP: case OpCode::JZ: case OpCode::JNZ: s += " " + std::to_string(i.a); break;
        case OpCode::CALL: s += " " + std::to_string(i.a) + ", " + std::to_string(i.argc); break;
        default: break;
    }
    return s;
}

Json BcProgram::toJson() const {
    Json j = Json::object();
    Json fs = Json::array();
    for (size_t k = 0; k < functions.size(); ++k) {
        auto& f = functions[k];
        Json o = Json::object();
        Json names = Json::array(), types = Json::array(), temps = Json::array();
        for (size_t s = 0; s < f.slotNames.size(); ++s) { names.push(f.slotNames[s]); types.push(typeName(f.slotTypes[s])); temps.push(static_cast<bool>(f.slotIsTemp[s])); }
        o.set("index", static_cast<int>(k)).set("name", f.name).set("entry", f.entry).set("end", f.end).set("params", f.paramCount)
         .set("returnsValue", f.returnsValue).set("slotNames", names).set("slotTypes", types).set("slotIsTemp", temps);
        fs.push(o);
    }
    Json ins = Json::array();
    for (size_t a = 0; a < code.size(); ++a) {
        Json o = Json::object();
        o.set("addr", static_cast<int>(a)).set("op", opName(code[a].op)).set("text", disasm(static_cast<int>(a))).set("comment", comment(static_cast<int>(a)))
         .set("description", opDescription(code[a].op)).set("function", functionAt(static_cast<int>(a)) >= 0 ? functions[functionAt(static_cast<int>(a))].name : "<entry>");
        if (code[a].line > 0) o.set("line", code[a].line).set("col", code[a].col);
        ins.push(o);
    }
    j.set("functions", fs).set("instructions", ins).set("count", static_cast<int>(code.size())).set("mainIndex", mainIndex);
    return j;
}

std::string BcProgram::text() const {
    std::ostringstream os;
    for (size_t a = 0; a < code.size(); ++a) {
        char buf[16]; std::snprintf(buf, sizeof buf, "%04zu  ", a);
        os << buf << disasm(static_cast<int>(a));
        std::string c = comment(static_cast<int>(a));
        if (!c.empty()) os << "    ; " << c;
        os << "\n";
    }
    return os.str();
}

} // namespace nova
