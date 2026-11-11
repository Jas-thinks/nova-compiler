#include <cstdlib>
#include <iostream>
#include <iterator>
#include "nova/pipeline.hpp"
#include "nova/x86.hpp"
#include "nova/sql.hpp"

namespace nova {

namespace {
std::string optValue(const std::vector<std::string>& args, const std::string& key, const std::string& def) {
    for (auto& a : args) if (a.rfind("--" + key + "=", 0) == 0) return a.substr(key.size() + 3);
    return def;
}
bool hasFlag(const std::vector<std::string>& args, const std::string& key) {
    for (auto& a : args) if (a == "--" + key) return true;
    return false;
}
int usage() {
    std::cerr << "usage: novac <command> [options]\n"
                 "  compile [--passes=all|none|a,b,..] [--run] [--trace] [--target=optimized|baseline] [--max-steps=N] [--max-trace=N]   (source on stdin, JSON on stdout)\n"
                 "  asm [--passes=..]       x86-64 assembly (native backend) for the program on stdin; see scripts/nova-native.sh\n"
                 "  sql                     NOVA-SQL: run a ';'-separated SQL script on stdin, JSON report on stdout\n"
                 "  grammar                 grammar, FIRST/FOLLOW and LR(1) tables as JSON\n"
                 "  state N                 kernel and closure of LR(1) state N\n"
                 "  fuzz [--count=N] [--seed=S] [--passes=...]   differential fuzz test\n"
                 "  fuzz --mode=invalid ...  mutation fuzzing of invalid programs;  --save-dir=DIR saves + minimizes failures\n"
                 "  check [--passes=..]     differential check of the program on stdin\n"
                 "  mutate [--seed=S]       print a mutated (usually invalid) program\n"
                 "  gen [--seed=S]          print a random valid NOVA program\n"
                 "  passes                  list optimization passes\n"
                 "  isa                     list VM bytecode instructions\n";
    return 2;
}
}

int runCli(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) return usage();
    const std::string cmd = args[0];
    try {
        if (cmd == "compile") {
            CompileOptions co;
            std::string err;
            if (!OptConfig::parse(optValue(args, "passes", "all"), co.opt, err)) { std::cerr << err << "\n"; return 2; }
            co.run = hasFlag(args, "run"); co.trace = hasFlag(args, "trace");
            co.traceTarget = optValue(args, "target", "optimized");
            co.maxSteps = std::atoll(optValue(args, "max-steps", "2000000").c_str());
            co.maxTrace = static_cast<size_t>(std::atoll(optValue(args, "max-trace", "10000").c_str()));
            if (co.trace) co.run = true;
            std::string src((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
            auto r = compileSource(src, co);
            std::cout << resultToJson(*r, co).dump() << "\n";
            return 0;
        }
        if (cmd == "asm") {   // native backend: x86-64 assembly for the (optimized) program on stdin
            CompileOptions co; std::string err;
            if (!OptConfig::parse(optValue(args, "passes", "all"), co.opt, err)) { std::cerr << err << "\n"; return 2; }
            std::string src((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
            auto r = compileSource(src, co);
            if (!r->success) { for (auto& d : r->diagnostics.items()) std::cerr << d.message << "\n"; return 1; }
            std::cout << generateX86(optValue(args, "passes", "all") == "none" ? r->ir : r->opt.program, !hasFlag(args, "no-regalloc"));
            return 0;
        }
        if (cmd == "sql") {   // NOVA-SQL: script on stdin, JSON report on stdout
            std::string src((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
            std::cout << runSqlScript(src).dump() << "\n";
            return 0;
        }
        if (cmd == "grammar") { std::cout << novaTable().tablesJson().dump() << "\n"; return 0; }
        if (cmd == "state") {
            if (args.size() < 2) return usage();
            int n = std::atoi(args[1].c_str());
            if (n < 0 || static_cast<size_t>(n) >= novaTable().stateCount()) { std::cerr << "no such state\n"; return 2; }
            std::cout << novaTable().stateJson(n).dump() << "\n"; return 0;
        }
        if (cmd == "fuzz") {
            OptConfig cfg; std::string err;
            if (!OptConfig::parse(optValue(args, "passes", "all"), cfg, err)) { std::cerr << err << "\n"; return 2; }
            int count = std::atoi(optValue(args, "count", "200").c_str());
            uint64_t seed = static_cast<uint64_t>(std::atoll(optValue(args, "seed", "1").c_str()));
            if (optValue(args, "mode", "valid") == "invalid") std::cout << runInvalidFuzz(count, seed, cfg).dump() << "\n";
            else std::cout << runFuzz(count, seed, cfg, optValue(args, "save-dir", "")).dump() << "\n";
            return 0;
        }
        if (cmd == "mutate") { std::cout << mutateProgram(generateRandomProgram(static_cast<uint64_t>(std::atoll(optValue(args, "seed", "1").c_str()))), static_cast<uint64_t>(std::atoll(optValue(args, "seed", "1").c_str()))); return 0; }
        if (cmd == "check") {   // differential check of a program on stdin: exit 0 = engines agree, 1 = mismatch, 2 = does not compile
            OptConfig cfg; std::string err;
            if (!OptConfig::parse(optValue(args, "passes", "all"), cfg, err)) { std::cerr << err << "\n"; return 2; }
            std::string src((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
            DiffOutcome d = differentialCheck(src, cfg);
            std::cout << (!d.compiled ? "does not compile: " + d.detail : d.inconclusive ? "inconclusive" : d.match ? "engines agree" : "MISMATCH: " + d.detail) << "\n";
            return !d.compiled ? 2 : (d.inconclusive || d.match ? 0 : 1);
        }
        if (cmd == "gen") { std::cout << generateRandomProgram(static_cast<uint64_t>(std::atoll(optValue(args, "seed", "1").c_str()))); return 0; }
        if (cmd == "isa") {
            Json a = Json::array();
            for (int k = 0; k <= static_cast<int>(OpCode::HALT); ++k) {
                Json o = Json::object();
                o.set("op", opName(static_cast<OpCode>(k))).set("description", opDescription(static_cast<OpCode>(k)));
                a.push(o);
            }
            std::cout << a.dump() << "\n"; return 0;
        }
        if (cmd == "passes") {
            Json a = Json::array();
            for (auto& [id, title] : optimizationPasses()) { Json p = Json::object(); p.set("id", id).set("title", title); a.push(p); }
            std::cout << a.dump() << "\n"; return 0;
        }
    } catch (const std::exception& e) {
        std::cerr << "internal compiler error: " << e.what() << "\n";
        return 70;
    }
    return usage();
}

} // namespace nova
