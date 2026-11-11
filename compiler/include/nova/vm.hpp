// NOVA bytecode virtual machine with optional per-instruction state snapshots.
#pragma once
#include <map>
#include <set>
#include "nova/bytecode.hpp"

namespace nova {

struct VMOptions {
    int64_t maxSteps = 2000000;
    int maxCallDepth = 512;
    size_t maxOutputLines = 5000;
    bool trace = false;          // record a snapshot before every instruction
    size_t maxTrace = 10000;
};

struct FrameSnapshot {
    int function = -1;
    int callLine = 0;             // source line of the CALL that created this frame
    std::vector<Value> locals;
    std::vector<Value> stack;
};

struct Snapshot {
    int64_t step = 0;
    int ip = 0;
    std::vector<FrameSnapshot> frames;   // call stack, outermost first (entry stub excluded)
    size_t outputCount = 0;
    std::string event;                   // what the previously executed instruction did
    bool terminal = false;
};

struct VMResult {
    bool ok = true;                      // false on runtime error
    std::string error;
    int errorIp = -1, errorLine = 0, errorCol = 0;
    int64_t steps = 0;
    std::vector<std::string> output;
    std::map<std::string, int64_t> opCounts;
    int maxStackDepth = 0, maxCallDepth = 0;
    std::vector<Snapshot> trace;
    bool traceTruncated = false;
    Json toJson(const BcProgram* prog = nullptr, bool includeTrace = false) const;
};

VMResult runVM(const BcProgram& prog, const VMOptions& opt);
Json snapshotToJson(const Snapshot& s);

} // namespace nova
