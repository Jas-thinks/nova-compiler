// IR optimizer: constant folding/propagation, algebraic simplification, CSE,
// unreachable-code elimination and liveness-based dead-code elimination.
#pragma once
#include <map>
#include <string>
#include <vector>
#include "nova/ir.hpp"

namespace nova {

struct OptConfig {
    bool constantFolding = true;
    bool constantPropagation = true;   // also performs copy propagation
    bool algebraic = true;
    bool cse = true;
    bool unreachable = true;
    bool dce = true;
    int maxRounds = 12;
    static OptConfig none() { OptConfig c; c.constantFolding = c.constantPropagation = c.algebraic = c.cse = c.unreachable = c.dce = false; return c; }
    bool any() const { return constantFolding || constantPropagation || algebraic || cse || unreachable || dce; }
    // Parse a comma-separated list of pass ids, or "all" / "none".
    static bool parse(const std::string& list, OptConfig& out, std::string& error);
    Json toJson() const;
};

struct OptRecord {
    std::string pass, function, before, after, explanation;
    int line = 0, col = 0, round = 0;
    Json toJson() const;
};

struct OptResult {
    IRProgram program;
    std::vector<OptRecord> history;
    std::map<std::string, int> perPass;
    int rounds = 0;
    size_t instructionsBefore = 0, instructionsAfter = 0;
    Json toJson() const;
};

const std::vector<std::pair<std::string, std::string>>& optimizationPasses();  // id, title
OptResult optimize(const IRProgram& in, const OptConfig& cfg);

} // namespace nova
