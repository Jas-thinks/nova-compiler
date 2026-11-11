// Control-flow graph, iterative data-flow analyses (liveness, reaching definitions),
// dominators and SSA construction (pruned SSA, Cytron et al.) over the NOVA IR.
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>
#include "nova/ir.hpp"

namespace nova {

struct CfgEdge { int from, to; std::string kind; bool back = false; };  // kind: fall | jump | true | false

struct BasicBlock {
    int id = 0;
    size_t begin = 0, end = 0;              // instruction range [begin, end) in the function's code
    std::vector<std::string> labels;        // labels that start / lie inside the block
    std::vector<int> succ, pred;
    int idom = -1;                          // immediate dominator (-1 for entry / unreachable)
    bool reachable = true;
    bool loopHeader = false;
};

struct CFG {
    std::vector<BasicBlock> blocks;
    std::vector<CfgEdge> edges;
    std::vector<int> rpo;                   // reverse post-order of reachable blocks
};

CFG buildCFG(const IRFunction& fn);

struct DataflowIteration { int iter; std::vector<std::set<std::string>> in, out; };
struct DataflowResult {
    std::string name;                       // "liveness" | "reaching-definitions"
    std::string direction;                  // backward | forward
    std::vector<std::set<std::string>> gen, kill;   // USE/DEF for liveness, GEN/KILL for reaching defs
    std::vector<std::set<std::string>> in, out;     // fixed point
    std::vector<DataflowIteration> history;
    bool converged = false;
    int iterations = 0;
    std::map<std::string, std::string> defText;     // reaching defs: definition id -> instruction text
};

DataflowResult computeLiveness(const IRFunction& fn, const CFG& cfg);
DataflowResult computeReachingDefs(const IRFunction& fn, const CFG& cfg);

// ---- SSA ----
struct SsaPhi { std::string var, dst; std::vector<std::pair<int, std::string>> args; };  // (pred block, version name)
struct SsaBlock { std::vector<SsaPhi> phis; std::vector<std::string> code; std::vector<int> lines; };
struct SsaFunction {
    std::vector<SsaBlock> blocks;
    int phiCount = 0;
    std::vector<std::string> definitions;   // every SSA name defined (params, phis, instructions) - each exactly once
    std::vector<std::string> uses;          // every SSA name used
};
SsaFunction buildSSA(const IRFunction& fn, const CFG& cfg, const DataflowResult& liveness);

// ---- worklist solvers and further analyses ----
// Liveness solved with a worklist (independent of computeLiveness' round-robin sweep; tests assert equal fixed points).
DataflowResult computeLivenessWorklist(const IRFunction& fn, const CFG& cfg);
// Available expressions: forward, MUST (intersection) analysis over pure binary/unary expressions.
DataflowResult computeAvailableExpressions(const IRFunction& fn, const CFG& cfg);

// Constant propagation over the flat lattice  UNDEF  <  { ..., -1, 0, 1, ... }  <  NAC   (Kildall, worklist).
struct ConstLattice { enum Kind { Undef, Const, NAC } kind = Undef; Value v; bool operator==(const ConstLattice& o) const { return kind == o.kind && (kind != Const || v == o.v); } std::string str() const; };
using ConstEnv = std::map<std::string, ConstLattice>;
struct ConstPropResult { std::vector<ConstEnv> in, out; int iterations = 0; int worklistPops = 0; bool converged = false; };
ConstPropResult computeConstantPropagation(const IRFunction& fn, const CFG& cfg);

// Instructions whose result is dead according to liveness (what DCE would delete). Indices into fn.code.
std::vector<int> findDeadInstructions(const IRFunction& fn, const CFG& cfg, const DataflowResult& liveness);

// Register allocation on virtual registers (the NOVA VM has frame slots, not machine registers).
struct RegAllocNode { std::string name; int degree = 0; int color = -1; bool spilled = false; int firstIndex = -1, lastIndex = -1; int uses = 0; };
struct RegAllocResult {
    int k = 0;
    std::vector<RegAllocNode> nodes;
    std::vector<std::pair<std::string, std::string>> edges;
    int colorsUsed = 0;
    int spills = 0;
    int minimalK = 0;              // smallest K for which this allocator needs no spill
    std::vector<std::string> simplifyOrder;
};
RegAllocResult allocateRegisters(const IRFunction& fn, const CFG& cfg, const DataflowResult& liveness, int k);
bool verifyAllocation(const RegAllocResult& r);   // no two interfering, non-spilled nodes share a colour

// CFG well-formedness: returns human-readable problems (empty when valid).
std::vector<std::string> verifyCFG(const IRFunction& fn, const CFG& cfg);
// SSA well-formedness: single definition per name, every use defined, φ arity == #reachable predecessors.
std::vector<std::string> verifySSA(const SsaFunction& ssa, const CFG& cfg);
// Dominance frontier sets (block ids) and dominator-tree children.
std::vector<std::set<int>> dominanceFrontiers(const CFG& cfg);

Json analyzeFunctionJson(const IRFunction& fn, int regK = 4);
Json analyzeProgramJson(const IRProgram& p, int regK = 4);

} // namespace nova
