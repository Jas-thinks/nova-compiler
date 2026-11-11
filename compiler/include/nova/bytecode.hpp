// NOVA VM bytecode (a stack-machine instruction set; NOT native machine code).
#pragma once
#include <string>
#include <vector>
#include "nova/ir.hpp"

namespace nova {

enum class OpCode { PUSH, POP, LOAD, STORE, ADD, SUB, MUL, DIV, MOD, NEG, NOT, LT, LE, GT, GE, EQ, NE,
                    I2F, F2I, JMP, JZ, JNZ, CALL, RET, RETV, PRINT, HALT };

const char* opName(OpCode op);
const char* opDescription(OpCode op);
OpCode opFromBin(BinOp b);

struct BcInstr {
    OpCode op = OpCode::HALT;
    Value k;          // PUSH constant
    int a = 0;        // LOAD/STORE slot, jump target address, CALL function index
    int argc = 0;     // CALL
    int line = 0, col = 0;
};

struct BcFunction {
    std::string name;
    int entry = 0, end = 0;     // [entry, end)
    int paramCount = 0;
    bool returnsValue = false;
    std::vector<std::string> slotNames;
    std::vector<Type> slotTypes;
    std::vector<char> slotIsTemp;
};

struct BcProgram {
    std::vector<BcInstr> code;
    std::vector<BcFunction> functions;
    int mainIndex = -1;
    std::string disasm(int addr) const;       // one instruction, human-readable
    std::string comment(int addr) const;      // operand annotation (slot name, callee, target)
    int functionAt(int addr) const;           // index of the function containing addr, -1 for the entry stub
    Json toJson() const;
    std::string text() const;
};

BcProgram generateBytecode(const IRProgram& ir);

// Static bytecode verifier (like a JVM verifier, much smaller): returns problems, empty when the program is well-formed.
// Checks jump targets, slot / function indices, call arity, function termination and, by abstract interpretation of
// the operand-stack depth over the control-flow graph, consistent non-negative depths and balanced returns.
std::vector<std::string> verifyBytecode(const BcProgram& prog);

} // namespace nova
