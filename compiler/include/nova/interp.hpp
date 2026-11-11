// Reference tree-walking interpreter over the annotated AST. Independent of IR, optimizer and VM;
// used as the oracle for differential testing.
#pragma once
#include "nova/semantic.hpp"
#include "nova/value.hpp"

namespace nova {

struct InterpResult {
    bool ok = true;
    bool limitHit = false;     // step limit or recursion limit: result is inconclusive
    std::string error;
    std::vector<std::string> output;
};

InterpResult interpret(Program& program, int64_t maxSteps = 2000000, int maxDepth = 256);

} // namespace nova
