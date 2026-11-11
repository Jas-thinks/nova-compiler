#include <set>
#include "nova/bytecode.hpp"

namespace nova {

std::vector<std::string> verifyBytecode(const BcProgram& p) {
    std::vector<std::string> problems;
    auto bad = [&](int addr, const std::string& m) { problems.push_back("@" + std::to_string(addr) + ": " + m); };
    int n = static_cast<int>(p.code.size());
    if (n < 2 || p.code[0].op != OpCode::CALL || p.code[1].op != OpCode::HALT) { bad(0, "entry stub must be CALL main; HALT"); return problems; }
    if (p.mainIndex < 0) bad(0, "no main function");
    for (size_t fi = 0; fi < p.functions.size(); ++fi) {
        const BcFunction& f = p.functions[fi];
        if (f.entry < 0 || f.end > n || f.entry >= f.end) { bad(f.entry, "function " + f.name + " has an invalid code range"); continue; }
        const BcInstr& last = p.code[f.end - 1];
        if (last.op != OpCode::RET && last.op != OpCode::RETV && last.op != OpCode::JMP && last.op != OpCode::HALT) bad(f.end - 1, "function " + f.name + " can fall off its end");
        // structural checks
        for (int a = f.entry; a < f.end; ++a) {
            const BcInstr& in = p.code[a];
            switch (in.op) {
                case OpCode::LOAD: case OpCode::STORE:
                    if (in.a < 0 || in.a >= static_cast<int>(f.slotNames.size())) bad(a, "slot out of range");
                    break;
                case OpCode::JMP: case OpCode::JZ: case OpCode::JNZ:
                    if (in.a < f.entry || in.a >= f.end) bad(a, "jump target " + std::to_string(in.a) + " outside function " + f.name);
                    break;
                case OpCode::CALL:
                    if (in.a < 0 || in.a >= static_cast<int>(p.functions.size())) bad(a, "call to unknown function");
                    else if (in.argc != p.functions[in.a].paramCount) bad(a, "call arity mismatch for " + p.functions[in.a].name);
                    break;
                case OpCode::HALT: bad(a, "HALT inside a function body"); break;
                default: break;
            }
        }
        // abstract interpretation of stack depth (worklist over instruction addresses)
        std::vector<int> depth(f.end - f.entry, -1);
        std::vector<int> work = {f.entry};
        depth[0] = 0;
        bool abort = false;
        while (!work.empty() && !abort) {
            int a = work.back(); work.pop_back();
            int d = depth[a - f.entry];
            const BcInstr& in = p.code[a];
            int nd = d;
            bool falls = true;
            std::vector<int> targets;
            auto need = [&](int k) { if (d < k) { bad(a, std::string("stack underflow executing ") + opName(in.op)); abort = true; return false; } return true; };
            switch (in.op) {
                case OpCode::PUSH: case OpCode::LOAD: nd = d + 1; break;
                case OpCode::POP: case OpCode::STORE: case OpCode::PRINT: if (!need(1)) break; nd = d - 1; break;
                case OpCode::ADD: case OpCode::SUB: case OpCode::MUL: case OpCode::DIV: case OpCode::MOD:
                case OpCode::LT: case OpCode::LE: case OpCode::GT: case OpCode::GE: case OpCode::EQ: case OpCode::NE: if (!need(2)) break; nd = d - 1; break;
                case OpCode::NEG: case OpCode::NOT: case OpCode::I2F: case OpCode::F2I: if (!need(1)) break; break;
                case OpCode::JMP: falls = false; targets.push_back(in.a); break;
                case OpCode::JZ: case OpCode::JNZ: if (!need(1)) break; nd = d - 1; targets.push_back(in.a); break;
                case OpCode::CALL:
                    if (in.a < 0 || in.a >= static_cast<int>(p.functions.size())) { abort = true; break; }
                    if (!need(in.argc)) break;
                    nd = d - in.argc + (p.functions[in.a].returnsValue ? 1 : 0);
                    break;
                case OpCode::RET:
                    if (!need(1)) break;
                    if (d != 1) bad(a, "RET with " + std::to_string(d) + " values on the operand stack (expected exactly 1)");
                    if (!f.returnsValue) bad(a, "RET in a function without a return value");
                    falls = false; break;
                case OpCode::RETV:
                    if (d != 0) bad(a, "RETV with a non-empty operand stack");
                    if (f.returnsValue) bad(a, "RETV in a function that returns a value");
                    falls = false; break;
                case OpCode::HALT: falls = false; break;
            }
            if (abort) break;
            if (falls) targets.push_back(a + 1);
            for (int t : targets) {
                if (t < f.entry || t >= f.end) { if (falls && t == a + 1) bad(a, "falls through past the end of the function"); continue; }
                int& dt = depth[t - f.entry];
                if (dt < 0) { dt = nd; work.push_back(t); }
                else if (dt != nd) { bad(t, "inconsistent operand-stack depth at merge (" + std::to_string(dt) + " vs " + std::to_string(nd) + ")"); abort = true; }
            }
        }
    }
    return problems;
}

} // namespace nova
