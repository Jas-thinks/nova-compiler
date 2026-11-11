// End-to-end compilation pipeline, JSON serialisation, differential checking and the novac CLI.
#pragma once
#include <functional>
#include <memory>
#include "nova/bytecode.hpp"
#include "nova/interp.hpp"
#include "nova/optimizer.hpp"
#include "nova/vm.hpp"

namespace nova {

struct CompileOptions {
    OptConfig opt;                 // passes used for the "optimized" program
    bool run = false;              // execute with the VM
    bool trace = false;            // record VM snapshots for the debugger
    std::string traceTarget = "optimized";   // optimized | baseline
    int64_t maxSteps = 2000000;
    size_t maxTrace = 10000;
};

struct StageInfo {
    std::string id, name, status;  // status: ok | error | skipped
    double ms = 0;
    Json summary = Json::object();
};

struct CompileResult {
    std::string source;
    LexResult lex;
    ParseResult parse;
    std::unique_ptr<Program> ast;
    std::unique_ptr<SemanticResult> sem;
    IRProgram ir;
    OptResult opt;
    BcProgram bcBaseline, bcOptimized;
    bool haveIR = false, haveBytecode = false;
    DiagnosticList diagnostics;    // all phases, in order
    std::vector<StageInfo> stages;
    bool success = false;
    std::string failedStage;
    // execution
    bool ran = false;
    VMResult runBaseline, runOptimized;
    InterpResult reference;
    bool referenceRan = false;
};

std::unique_ptr<CompileResult> compileSource(const std::string& source, const CompileOptions& opt);
Json resultToJson(const CompileResult& r, const CompileOptions& opt);

struct DiffOutcome {
    bool compiled = false;
    bool inconclusive = false;   // step/recursion limit reached
    bool match = true;
    std::string detail;
    size_t irBefore = 0, irAfter = 0;
    int64_t stepsBefore = 0, stepsAfter = 0;
};
// Compile and compare: reference interpreter vs VM(unoptimized) vs VM(optimized with cfg).
DiffOutcome differentialCheck(const std::string& source, const OptConfig& cfg);

// Random valid program generator (deterministic for a seed).
std::string generateRandomProgram(uint64_t seed);
// saveDir (optional): failing programs are written as seed-N.nova together with a delta-minimized seed-N.min.nova
Json runFuzz(int count, uint64_t seed, const OptConfig& cfg, const std::string& saveDir = "");
// Mutation fuzzing of *invalid* programs: the compiler must never crash or hang, must emit well-formed diagnostics for
// rejected programs, and mutants that happen to stay valid must still behave identically on all engines.
Json runInvalidFuzz(int count, uint64_t seed, const OptConfig& cfg);
std::string mutateProgram(const std::string& source, uint64_t seed);
// Delta-debugging (ddmin over lines): smallest subset of lines for which stillFails() holds.
std::string minimizeProgram(const std::string& source, const std::function<bool(const std::string&)>& stillFails);

int runCli(int argc, char** argv);

} // namespace nova
