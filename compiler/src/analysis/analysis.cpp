#include "nova/analysis.hpp"
#include <algorithm>
#include <functional>

namespace nova {

static bool isJump(IRInstr::Kind k) { return k == IRInstr::Jump || k == IRInstr::JumpIfFalse || k == IRInstr::JumpIfTrue; }

// ------------------------------------------------------------------ CFG
CFG buildCFG(const IRFunction& fn) {
    CFG g;
    const auto& code = fn.code;
    if (code.empty()) { g.blocks.push_back({}); g.rpo = {0}; return g; }
    // 1. leaders: first instruction, every label, every instruction after a jump/return
    std::vector<char> leader(code.size(), 0);
    leader[0] = 1;
    for (size_t i = 0; i < code.size(); ++i) {
        if (code[i].kind == IRInstr::Label) leader[i] = 1;
        if ((isJump(code[i].kind) || code[i].kind == IRInstr::Return) && i + 1 < code.size()) leader[i + 1] = 1;
    }
    // 2. blocks. If the first instruction is a jump target (function starts with a loop) the entry block would
    // have predecessors, which breaks the "entry has no incoming edge" assumption of dominators/SSA: add an empty entry.
    bool entryTargeted = false;
    if (code[0].kind == IRInstr::Label)
        for (auto& i : code) if (isJump(i.kind) && i.target == code[0].target) entryTargeted = true;
    if (entryTargeted) { BasicBlock e; e.id = 0; e.begin = e.end = 0; g.blocks.push_back(e); }
    for (size_t i = 0; i < code.size();) {
        size_t j = i + 1;
        while (j < code.size() && !leader[j]) ++j;
        BasicBlock b; b.id = static_cast<int>(g.blocks.size()); b.begin = i; b.end = j;
        for (size_t k = i; k < j; ++k) if (code[k].kind == IRInstr::Label) b.labels.push_back(code[k].target);
        g.blocks.push_back(b);
        i = j;
    }
    std::map<std::string, int> labelBlock;
    for (auto& b : g.blocks) for (auto& l : b.labels) labelBlock[l] = b.id;
    // 3. edges
    auto add = [&](int from, int to, const char* kind) { g.edges.push_back({from, to, kind}); };
    auto target = [&](const std::string& l) { auto it = labelBlock.find(l); return it == labelBlock.end() ? -1 : it->second; };   // malformed IR: verifyCFG reports it
    for (auto& b : g.blocks) {
        int next = b.id + 1 < static_cast<int>(g.blocks.size()) ? b.id + 1 : -1;
        if (b.begin == b.end) { if (next >= 0) add(b.id, next, "fall"); continue; }   // synthetic entry
        const IRInstr& last = code[b.end - 1];
        switch (last.kind) {
            case IRInstr::Return: break;
            case IRInstr::Jump: if (target(last.target) >= 0) add(b.id, target(last.target), "jump"); break;
            case IRInstr::JumpIfFalse:
                if (target(last.target) >= 0) add(b.id, target(last.target), "false");
                if (next >= 0) add(b.id, next, "true");
                break;
            case IRInstr::JumpIfTrue:
                if (target(last.target) >= 0) add(b.id, target(last.target), "true");
                if (next >= 0) add(b.id, next, "false");
                break;
            default: if (next >= 0) add(b.id, next, "fall");
        }
    }
    for (auto& e : g.edges) {
        auto& s = g.blocks[e.from].succ; if (std::find(s.begin(), s.end(), e.to) == s.end()) s.push_back(e.to);
        auto& p = g.blocks[e.to].pred; if (std::find(p.begin(), p.end(), e.from) == p.end()) p.push_back(e.from);
    }
    // 4. reachability + reverse post-order (DFS)
    std::vector<int> post; std::vector<char> seen(g.blocks.size(), 0);
    std::function<void(int)> dfs = [&](int b) { seen[b] = 1; for (int s : g.blocks[b].succ) if (!seen[s]) dfs(s); post.push_back(b); };
    dfs(0);
    g.rpo.assign(post.rbegin(), post.rend());
    for (auto& b : g.blocks) b.reachable = seen[b.id];
    // 5. dominators (Cooper-Harvey-Kennedy iterative algorithm)
    std::vector<int> order(g.blocks.size(), -1);
    for (size_t i = 0; i < g.rpo.size(); ++i) order[g.rpo[i]] = static_cast<int>(i);
    std::vector<int> idom(g.blocks.size(), -1);
    idom[0] = 0;
    auto intersect = [&](int a, int b) {
        while (a != b) { while (order[a] > order[b]) a = idom[a]; while (order[b] > order[a]) b = idom[b]; }
        return a;
    };
    for (bool changed = true; changed;) {
        changed = false;
        for (int b : g.rpo) {
            if (b == 0) continue;
            int nd = -1;
            for (int p : g.blocks[b].pred) {
                if (!g.blocks[p].reachable || idom[p] < 0) continue;
                nd = nd < 0 ? p : intersect(p, nd);
            }
            if (nd != idom[b]) { idom[b] = nd; changed = true; }
        }
    }
    for (auto& b : g.blocks) b.idom = (b.id == 0 || !b.reachable) ? -1 : idom[b.id];
    // 6. back edges: u -> v where v dominates u (natural loops)
    auto dominates = [&](int a, int b) { for (int x = b; x >= 0; x = (x == 0 ? -1 : idom[x])) { if (x == a) return true; if (idom[x] < 0) break; } return false; };
    for (auto& e : g.edges)
        if (g.blocks[e.from].reachable && g.blocks[e.to].reachable && dominates(e.to, e.from)) { e.back = true; g.blocks[e.to].loopHeader = true; }
    return g;
}

// ------------------------------------------------------------------ data-flow helpers
namespace {

using Set = std::set<std::string>;

Set unite(const Set& a, const Set& b) { Set r = a; r.insert(b.begin(), b.end()); return r; }
Set minus(const Set& a, const Set& b) { Set r; for (auto& x : a) if (!b.count(x)) r.insert(x); return r; }

}

DataflowResult computeLiveness(const IRFunction& fn, const CFG& cfg) {
    DataflowResult r; r.name = "liveness"; r.direction = "backward";
    size_t n = cfg.blocks.size();
    r.gen.assign(n, {}); r.kill.assign(n, {}); r.in.assign(n, {}); r.out.assign(n, {});
    for (auto& b : cfg.blocks)           // USE = read before any write in the block; DEF = written in the block
        for (size_t k = b.begin; k < b.end && !fn.code.empty(); ++k) {
            const IRInstr& i = fn.code[k];
            i.forEachUse([&](const Operand& o) { if (!r.kill[b.id].count(o.name)) r.gen[b.id].insert(o.name); });
            if (i.dst.isName()) r.kill[b.id].insert(i.dst.name);
        }
    for (r.iterations = 0; r.iterations < 200;) {
        bool changed = false;
        for (auto it = cfg.rpo.rbegin(); it != cfg.rpo.rend(); ++it) {   // reverse of RPO converges quickly for backward problems
            int b = *it;
            Set out;
            for (int s : cfg.blocks[b].succ) out = unite(out, r.in[s]);
            Set in = unite(r.gen[b], minus(out, r.kill[b]));
            if (out != r.out[b] || in != r.in[b]) { r.out[b] = out; r.in[b] = in; changed = true; }
        }
        ++r.iterations;
        r.history.push_back({r.iterations, r.in, r.out});
        if (!changed) { r.converged = true; break; }
    }
    return r;
}

DataflowResult computeReachingDefs(const IRFunction& fn, const CFG& cfg) {
    DataflowResult r; r.name = "reaching-definitions"; r.direction = "forward";
    size_t n = cfg.blocks.size();
    r.gen.assign(n, {}); r.kill.assign(n, {}); r.in.assign(n, {}); r.out.assign(n, {});
    std::map<std::string, Set> defsOf;                                    // variable -> all definition ids
    std::vector<std::vector<std::pair<std::string, std::string>>> blockDefs(n);   // (id, var) in order
    Set entry;
    for (auto& p : fn.params) { std::string id = "p:" + p.first; defsOf[p.first].insert(id); entry.insert(id); r.defText[id] = "param " + p.first; }
    for (auto& b : cfg.blocks)
        for (size_t k = b.begin; k < b.end && !fn.code.empty(); ++k) {
            const IRInstr& i = fn.code[k];
            if (!i.dst.isName()) continue;
            std::string id = "d" + std::to_string(k);
            defsOf[i.dst.name].insert(id); blockDefs[b.id].push_back({id, i.dst.name});
            r.defText[id] = i.str() + (i.line ? "   (line " + std::to_string(i.line) + ")" : "");
        }
    for (auto& b : cfg.blocks) {
        Set gen, kill;
        for (auto& [id, var] : blockDefs[b.id]) {
            for (auto& other : defsOf[var]) if (other != id) { kill.insert(other); gen.erase(other); }
            gen.insert(id);
        }
        r.gen[b.id] = gen; r.kill[b.id] = minus(kill, gen);
    }
    for (r.iterations = 0; r.iterations < 200;) {
        bool changed = false;
        for (int b : cfg.rpo) {
            Set in = b == 0 ? entry : Set();
            for (int p : cfg.blocks[b].pred) if (cfg.blocks[p].reachable) in = unite(in, r.out[p]);
            Set out = unite(r.gen[b], minus(in, r.kill[b]));
            if (in != r.in[b] || out != r.out[b]) { r.in[b] = in; r.out[b] = out; changed = true; }
        }
        ++r.iterations;
        r.history.push_back({r.iterations, r.in, r.out});
        if (!changed) { r.converged = true; break; }
    }
    return r;
}

// ------------------------------------------------------------------ SSA
SsaFunction buildSSA(const IRFunction& fn, const CFG& cfg, const DataflowResult& live) {
    SsaFunction ssa;
    size_t n = cfg.blocks.size();
    ssa.blocks.resize(n);
    // dominance frontiers (Cooper et al.)
    std::vector<Set> df(n);
    for (auto& b : cfg.blocks) {
        if (!b.reachable || b.pred.size() < 2) continue;
        for (int p : b.pred) {
            if (!cfg.blocks[p].reachable) continue;
            int runner = p;
            while (runner != b.idom && runner >= 0) { df[runner].insert(std::to_string(b.id)); runner = cfg.blocks[runner].idom; }
        }
    }
    // definition sites per variable (parameters are defined in the entry block)
    std::map<std::string, std::set<int>> defSites;
    std::map<std::string, Type> varType;
    for (auto& p : fn.params) { defSites[p.first].insert(0); varType[p.first] = p.second; }
    for (auto& b : cfg.blocks)
        if (b.reachable)
            for (size_t k = b.begin; k < b.end && !fn.code.empty(); ++k)
                if (fn.code[k].dst.isName()) defSites[fn.code[k].dst.name].insert(b.id);
    // phi placement: iterated dominance frontier, pruned by liveness (only where the variable is live-in)
    std::vector<std::map<std::string, SsaPhi>> phis(n);
    for (auto& [var, sites] : defSites) {
        std::vector<int> work(sites.begin(), sites.end());
        std::set<int> placed, queued(sites.begin(), sites.end());
        while (!work.empty()) {
            int b = work.back(); work.pop_back();
            for (auto& dfs : df[b]) {
                int y = std::stoi(dfs);
                if (placed.count(y)) continue;
                placed.insert(y);
                if (live.in[y].count(var)) { SsaPhi ph; ph.var = var; phis[y][var] = ph; }
                if (!queued.count(y)) { queued.insert(y); work.push_back(y); }
            }
        }
    }
    // renaming: preorder walk of the dominator tree with per-variable version stacks
    std::map<std::string, int> counter;
    std::map<std::string, std::vector<std::string>> stack;
    auto fresh = [&](const std::string& v) { std::string nm = v + "#" + std::to_string(counter[v]++); stack[v].push_back(nm); ssa.definitions.push_back(nm); return nm; };
    auto cur = [&](const std::string& v) { auto& s = stack[v]; if (s.empty()) { std::string nm = v + "#undef"; return nm; } return s.back(); };
    std::vector<std::vector<int>> domKids(n);
    for (auto& b : cfg.blocks) if (b.idom >= 0) domKids[b.idom].push_back(b.id);
    std::function<void(int)> rename = [&](int b) {
        std::map<std::string, size_t> pushed;
        auto note = [&](const std::string& v) { pushed[v]++; };
        for (auto& [var, ph] : phis[b]) { ph.dst = fresh(var); note(var); }
        if (b == 0) for (auto& p : fn.params) { fresh(p.first); note(p.first); }
        for (size_t k = cfg.blocks[b].begin; k < cfg.blocks[b].end && !fn.code.empty(); ++k) {
            IRInstr i = fn.code[k];
            auto ren = [&](Operand& o) { if (o.isName()) { o.name = cur(o.name); ssa.uses.push_back(o.name); } };
            ren(i.a); ren(i.b); for (auto& a : i.args) ren(a);
            if (i.dst.isName()) { std::string v = i.dst.name; i.dst.name = fresh(v); note(v); }
            ssa.blocks[b].code.push_back(i.str()); ssa.blocks[b].lines.push_back(i.line);
        }
        for (int s : cfg.blocks[b].succ) for (auto& [var, ph] : phis[s]) { std::string nm = cur(var); ph.args.push_back({b, nm}); ssa.uses.push_back(nm); }
        for (int c : domKids[b]) rename(c);
        for (auto& [v, cnt] : pushed) for (size_t t = 0; t < cnt; ++t) stack[v].pop_back();
    };
    rename(0);
    for (size_t b = 0; b < n; ++b) for (auto& [var, ph] : phis[b]) { ssa.blocks[b].phis.push_back(ph); ++ssa.phiCount; }
    return ssa;
}

// ------------------------------------------------------------------ JSON
static Json setJson(const std::set<std::string>& s) { Json a = Json::array(); for (auto& x : s) a.push(x); return a; }
static Json setsJson(const std::vector<std::set<std::string>>& v) { Json a = Json::array(); for (auto& s : v) a.push(setJson(s)); return a; }

static Json dataflowJson(const DataflowResult& r, bool liveness) {
    Json j = Json::object();
    j.set("name", r.name).set("direction", r.direction).set("converged", r.converged).set("iterations", r.iterations)
     .set(liveness ? "use" : "gen", setsJson(r.gen)).set(liveness ? "def" : "kill", setsJson(r.kill))
     .set("in", setsJson(r.in)).set("out", setsJson(r.out));
    Json h = Json::array();
    for (auto& it : r.history) { Json o = Json::object(); o.set("iteration", it.iter).set("in", setsJson(it.in)).set("out", setsJson(it.out)); h.push(o); }
    j.set("history", h);
    if (!liveness) { Json d = Json::object(); for (auto& [k, v] : r.defText) d.set(k, v); j.set("definitions", d); }
    return j;
}

Json analyzeFunctionJson(const IRFunction& fn, int regK) {
    CFG cfg = buildCFG(fn);
    DataflowResult live = computeLiveness(fn, cfg), rd = computeReachingDefs(fn, cfg);
    SsaFunction ssa = buildSSA(fn, cfg, live);
    Json j = Json::object();
    j.set("name", fn.name);
    Json blocks = Json::array();
    for (auto& b : cfg.blocks) {
        Json o = Json::object();
        Json ins = Json::array(), lines = Json::array(), lab = Json::array(), succ = Json::array(), pred = Json::array();
        int minLine = 0, maxLine = 0;
        for (size_t k = b.begin; k < b.end && !fn.code.empty(); ++k) {
            Json i = Json::object(); i.set("index", static_cast<int>(k)).set("text", fn.code[k].str()).set("line", fn.code[k].line);
            ins.push(i);
            int ln = fn.code[k].line;
            if (ln > 0) { if (!minLine || ln < minLine) minLine = ln; if (ln > maxLine) maxLine = ln; }
        }
        for (auto& l : b.labels) lab.push(l);
        for (int s : b.succ) succ.push(s);
        for (int p : b.pred) pred.push(p);
        o.set("id", b.id).set("labels", lab).set("instructions", ins).set("succ", succ).set("pred", pred).set("idom", b.idom)
         .set("reachable", b.reachable).set("loopHeader", b.loopHeader).set("firstLine", minLine).set("lastLine", maxLine);
        blocks.push(o);
    }
    Json edges = Json::array();
    for (auto& e : cfg.edges) { Json o = Json::object(); o.set("from", e.from).set("to", e.to).set("kind", e.kind).set("back", e.back); edges.push(o); }
    Json rpo = Json::array(); for (int b : cfg.rpo) rpo.push(b);
    j.set("blocks", blocks).set("edges", edges).set("rpo", rpo);
    j.set("liveness", dataflowJson(live, true)).set("reachingDefinitions", dataflowJson(rd, false));
    Json s = Json::object(); Json sb = Json::array();
    for (size_t b = 0; b < ssa.blocks.size(); ++b) {
        Json o = Json::object(); Json ph = Json::array(), code = Json::array();
        for (auto& p : ssa.blocks[b].phis) {
            Json a = Json::array();
            for (auto& [pb, nm] : p.args) { Json x = Json::object(); x.set("block", pb).set("name", nm); a.push(x); }
            std::string text = p.dst + " = φ(";
            for (size_t k = 0; k < p.args.size(); ++k) text += (k ? ", " : "") + p.args[k].second + " [B" + std::to_string(p.args[k].first) + "]";
            Json po = Json::object(); po.set("var", p.var).set("dst", p.dst).set("args", a).set("text", text + ")");
            ph.push(po);
        }
        for (auto& c : ssa.blocks[b].code) code.push(c);
        o.set("id", static_cast<int>(b)).set("phis", ph).set("code", code);
        sb.push(o);
    }
    s.set("blocks", sb).set("phiCount", ssa.phiCount).set("definitions", static_cast<int>(ssa.definitions.size()));
    j.set("ssa", s);
    // --- further analyses (declared in the header, defined below) ---
    DataflowResult avail = computeAvailableExpressions(fn, cfg);
    j.set("availableExpressions", dataflowJson(avail, false));
    ConstPropResult cp = computeConstantPropagation(fn, cfg);
    Json cpj = Json::object(); Json cin = Json::array(), cout_ = Json::array();
    auto envJson = [](const ConstEnv& e) { Json a = Json::array(); for (auto& [k, v] : e) { Json o = Json::object(); o.set("var", k).set("value", v.str()).set("kind", v.kind == ConstLattice::Const ? "const" : v.kind == ConstLattice::NAC ? "nac" : "undef"); a.push(o); } return a; };
    for (size_t b = 0; b < cp.in.size(); ++b) { cin.push(envJson(cp.in[b])); cout_.push(envJson(cp.out[b])); }
    cpj.set("in", cin).set("out", cout_).set("worklistPops", cp.worklistPops).set("converged", cp.converged);
    j.set("constantPropagation", cpj);
    Json dead = Json::array();
    for (int d : findDeadInstructions(fn, cfg, live)) { Json o = Json::object(); o.set("index", d).set("text", fn.code[d].str()).set("line", fn.code[d].line); dead.push(o); }
    j.set("deadInstructions", dead);
    std::vector<std::set<int>> dfs = dominanceFrontiers(cfg);
    Json dfj = Json::array(); for (auto& s2 : dfs) { Json a = Json::array(); for (int b : s2) a.push(b); dfj.push(a); }
    j.set("dominanceFrontiers", dfj);
    RegAllocResult ra = allocateRegisters(fn, cfg, live, regK);
    Json raj = Json::object(), rn = Json::array(), re = Json::array(), so = Json::array();
    for (auto& n : ra.nodes) { Json o = Json::object(); o.set("name", n.name).set("degree", n.degree).set("color", n.color).set("spilled", n.spilled).set("first", n.firstIndex).set("last", n.lastIndex).set("uses", n.uses); rn.push(o); }
    for (auto& e : ra.edges) { Json o = Json::object(); o.set("a", e.first).set("b", e.second); re.push(o); }
    for (auto& s2 : ra.simplifyOrder) so.push(s2);
    raj.set("k", ra.k).set("nodes", rn).set("edges", re).set("colorsUsed", ra.colorsUsed).set("spills", ra.spills).set("minimalK", ra.minimalK)
       .set("valid", verifyAllocation(ra)).set("simplifyOrder", so).set("variables", static_cast<int>(ra.nodes.size()));
    j.set("registerAllocation", raj);
    Json ver = Json::object(); Json vc = Json::array(), vs = Json::array();
    for (auto& m : verifyCFG(fn, cfg)) vc.push(m);
    for (auto& m : verifySSA(ssa, cfg)) vs.push(m);
    ver.set("cfg", vc).set("ssa", vs);
    j.set("verification", ver);
    int loops = 0; for (auto& b : cfg.blocks) if (b.loopHeader) ++loops;
    j.set("summary", Json::object().set("blocks", static_cast<int>(cfg.blocks.size())).set("edges", static_cast<int>(cfg.edges.size()))
          .set("loops", loops).set("phis", ssa.phiCount).set("livenessIterations", live.iterations).set("reachingDefsIterations", rd.iterations));
    return j;
}

Json analyzeProgramJson(const IRProgram& p, int regK) {
    Json a = Json::array();
    for (auto& f : p.functions) a.push(analyzeFunctionJson(f, regK));
    return a;
}

} // namespace nova

// ====================================================================== additional analyses
namespace nova {

namespace {
using Set = std::set<std::string>;
Set unite2(const Set& a, const Set& b) { Set r = a; r.insert(b.begin(), b.end()); return r; }
Set inter(const Set& a, const Set& b) { Set r; for (auto& x : a) if (b.count(x)) r.insert(x); return r; }
Set minus2(const Set& a, const Set& b) { Set r; for (auto& x : a) if (!b.count(x)) r.insert(x); return r; }

bool pureExpr(const IRInstr& i) { return i.kind == IRInstr::Bin || i.kind == IRInstr::Un; }
std::string exprKey(const IRInstr& i) {
    if (i.kind == IRInstr::Bin) { std::string a = i.a.str(), b = i.b.str(); if (isCommutative(i.bop) && b < a) std::swap(a, b); return a + " " + binOpText(i.bop) + " " + b; }
    std::string u = unOpText(i.uop);
    return (i.uop == UnOp::ToFloat || i.uop == UnOp::ToInt) ? u + "(" + i.a.str() + ")" : u + i.a.str();
}
std::vector<std::string> exprOperands(const IRInstr& i) { std::vector<std::string> v; if (i.a.isName()) v.push_back(i.a.name); if (i.b.isName()) v.push_back(i.b.name); return v; }
bool anyOperandIs(const IRInstr& i, const std::string& n) { for (auto& o : exprOperands(i)) if (o == n) return true; return false; }
}

// ---- liveness via worklist --------------------------------------------------------------
DataflowResult computeLivenessWorklist(const IRFunction& fn, const CFG& cfg) {
    DataflowResult r = computeLiveness(fn, cfg);        // reuse USE/DEF
    size_t n = cfg.blocks.size();
    r.name = "liveness (worklist)"; r.in.assign(n, {}); r.out.assign(n, {}); r.history.clear(); r.iterations = 0; r.converged = false;
    std::vector<int> work; std::vector<char> queued(n, 0);
    for (int b : cfg.rpo) { work.push_back(b); queued[b] = 1; }          // pop from the back = reverse RPO first
    while (!work.empty()) {
        int b = work.back(); work.pop_back(); queued[b] = 0; ++r.iterations;
        Set out;
        for (int s : cfg.blocks[b].succ) out = unite2(out, r.in[s]);
        Set in = unite2(r.gen[b], minus2(out, r.kill[b]));
        r.out[b] = out;
        if (in != r.in[b]) {
            r.in[b] = in;
            for (int p : cfg.blocks[b].pred) if (cfg.blocks[p].reachable && !queued[p]) { work.push_back(p); queued[p] = 1; }
        }
    }
    r.converged = true;
    return r;
}

// ---- available expressions ----------------------------------------------------------------
DataflowResult computeAvailableExpressions(const IRFunction& fn, const CFG& cfg) {
    DataflowResult r; r.name = "available-expressions"; r.direction = "forward";
    size_t n = cfg.blocks.size();
    r.gen.assign(n, {}); r.kill.assign(n, {}); r.in.assign(n, {}); r.out.assign(n, {});
    Set universe;
    std::map<std::string, std::vector<std::string>> operandsOf;
    for (auto& i : fn.code) if (pureExpr(i)) { std::string k = exprKey(i); universe.insert(k); operandsOf[k] = exprOperands(i); r.defText[k] = k; }
    for (auto& b : cfg.blocks) {
        Set gen, kill;
        for (size_t k = b.begin; k < b.end; ++k) {
            const IRInstr& i = fn.code[k];
            std::string key; bool isE = pureExpr(i); if (isE) key = exprKey(i);
            if (isE) gen.insert(key);
            if (i.dst.isName()) {                      // a definition kills every expression using the variable
                for (auto& e : universe) { auto& ops = operandsOf[e]; if (std::find(ops.begin(), ops.end(), i.dst.name) != ops.end()) { kill.insert(e); gen.erase(e); } }
                if (isE && anyOperandIs(i, i.dst.name)) gen.erase(key);     // x = x + 1 does not make x+1 available
            }
        }
        r.gen[b.id] = gen; r.kill[b.id] = kill;
    }
    for (auto& b : cfg.blocks) r.out[b.id] = b.id == 0 ? r.gen[0] : universe;     // optimistic initialisation for a must-analysis
    std::vector<int> work(cfg.rpo.rbegin(), cfg.rpo.rend()); std::vector<char> queued(n, 0);
    for (int b : work) queued[b] = 1;
    std::reverse(work.begin(), work.end());                                   // pop from back = RPO order
    while (!work.empty()) {
        int b = work.back(); work.pop_back(); queued[b] = 0; ++r.iterations;
        Set in; bool first = true;
        if (b != 0) for (int p : cfg.blocks[b].pred) if (cfg.blocks[p].reachable) { in = first ? r.out[p] : inter(in, r.out[p]); first = false; }
        Set out = unite2(r.gen[b], minus2(in, r.kill[b]));
        bool changed = out != r.out[b] || in != r.in[b];
        r.in[b] = in; r.out[b] = out;
        if (changed) for (int s : cfg.blocks[b].succ) if (!queued[s]) { work.push_back(s); queued[s] = 1; }
    }
    r.converged = true;
    r.history.push_back({r.iterations, r.in, r.out});
    return r;
}

// ---- constant propagation (flat lattice) -------------------------------------------------------
std::string ConstLattice::str() const { return kind == Undef ? "⊥" : kind == NAC ? "⊤" : valueToString(v); }

namespace {
ConstLattice meetL(const ConstLattice& a, const ConstLattice& b) {
    if (a.kind == ConstLattice::Undef) return b;
    if (b.kind == ConstLattice::Undef) return a;
    if (a.kind == ConstLattice::NAC || b.kind == ConstLattice::NAC) { ConstLattice x; x.kind = ConstLattice::NAC; return x; }
    if (a.v == b.v) return a;
    ConstLattice x; x.kind = ConstLattice::NAC; return x;
}
ConstEnv meetEnv(const ConstEnv& a, const ConstEnv& b) {
    ConstEnv r = a;
    for (auto& [k, v] : b) { auto it = r.find(k); r[k] = it == r.end() ? v : meetL(it->second, v); }
    return r;
}
ConstLattice evalOperand(const Operand& o, const ConstEnv& env) {
    ConstLattice l;
    if (o.isConst()) { l.kind = ConstLattice::Const; l.v = o.val; }
    else if (o.isName()) { auto it = env.find(o.name); if (it != env.end()) l = it->second; }
    return l;
}
void transferConst(const IRInstr& i, ConstEnv& env) {
    if (!i.dst.isName()) return;
    ConstLattice res; res.kind = ConstLattice::NAC;
    if (i.kind == IRInstr::Copy) res = evalOperand(i.a, env);
    else if (i.kind == IRInstr::Bin) {
        ConstLattice a = evalOperand(i.a, env), b = evalOperand(i.b, env);
        if (a.kind == ConstLattice::Const && b.kind == ConstLattice::Const) {
            Value out; std::string err;
            if (evalBin(i.bop, a.v, b.v, out, err)) { res.kind = ConstLattice::Const; res.v = out; }
        } else if (a.kind == ConstLattice::Undef || b.kind == ConstLattice::Undef) {
            if (a.kind != ConstLattice::NAC && b.kind != ConstLattice::NAC) res.kind = ConstLattice::Undef;
        }
    } else if (i.kind == IRInstr::Un) {
        ConstLattice a = evalOperand(i.a, env);
        if (a.kind == ConstLattice::Const) { res.kind = ConstLattice::Const; res.v = evalUn(i.uop, a.v); }
        else if (a.kind == ConstLattice::Undef) res.kind = ConstLattice::Undef;
    }
    env[i.dst.name] = res;
}
}

ConstPropResult computeConstantPropagation(const IRFunction& fn, const CFG& cfg) {
    ConstPropResult r;
    size_t n = cfg.blocks.size();
    r.in.assign(n, {}); r.out.assign(n, {});
    ConstEnv entry;
    for (auto& p : fn.params) { ConstLattice l; l.kind = ConstLattice::NAC; entry[p.first] = l; }
    std::vector<char> seen(n, 0), queued(n, 0);
    std::vector<int> work; work.push_back(0); queued[0] = 1;
    while (!work.empty()) {
        int b = work.back(); work.pop_back(); queued[b] = 0; ++r.worklistPops;
        ConstEnv in = b == 0 ? entry : ConstEnv();
        for (int p : cfg.blocks[b].pred) if (cfg.blocks[p].reachable && seen[p]) in = meetEnv(in, r.out[p]);
        ConstEnv env = in;
        for (size_t k = cfg.blocks[b].begin; k < cfg.blocks[b].end; ++k) transferConst(fn.code[k], env);
        bool changed = !seen[b] || env.size() != r.out[b].size();
        if (!changed) for (auto& [k, v] : env) if (!(r.out[b][k] == v)) { changed = true; break; }
        seen[b] = 1; r.in[b] = in; r.out[b] = env;
        if (changed) for (int s : cfg.blocks[b].succ) if (!queued[s]) { work.push_back(s); queued[s] = 1; }
        if (r.worklistPops > 100000) return r;
    }
    r.converged = true; r.iterations = r.worklistPops;
    return r;
}

// ---- dead code according to liveness -------------------------------------------------------------
std::vector<int> findDeadInstructions(const IRFunction& fn, const CFG& cfg, const DataflowResult& live) {
    std::vector<int> dead;
    for (auto& b : cfg.blocks) {
        if (!b.reachable) continue;
        Set l = live.out[b.id];
        for (size_t k = b.end; k-- > b.begin;) {
            const IRInstr& i = fn.code[k];
            bool pure = i.kind == IRInstr::Copy || i.kind == IRInstr::Un || (i.kind == IRInstr::Bin && !(i.a.type == Type::Int && (i.bop == BinOp::Div || i.bop == BinOp::Mod) && !(i.b.isConst() && i.b.val.i != 0)));
            if (pure && i.dst.isName() && !l.count(i.dst.name)) { dead.push_back(static_cast<int>(k)); continue; }
            if (i.dst.isName()) l.erase(i.dst.name);
            i.forEachUse([&](const Operand& o) { l.insert(o.name); });
        }
    }
    std::sort(dead.begin(), dead.end());
    return dead;
}

// ---- register allocation (Chaitin-Briggs style graph colouring, optimistic) -------------------------
RegAllocResult allocateRegisters(const IRFunction& fn, const CFG& cfg, const DataflowResult& live, int k) {
    RegAllocResult r; r.k = k;
    std::map<std::string, std::set<std::string>> adj;
    std::map<std::string, RegAllocNode> nodes;
    auto touch = [&](const std::string& n, int idx, bool use) {
        auto& nd = nodes[n]; nd.name = n; adj[n];
        if (nd.firstIndex < 0 || idx < nd.firstIndex) nd.firstIndex = idx;
        if (idx > nd.lastIndex) nd.lastIndex = idx;
        if (use) nd.uses++;
    };
    for (auto& p : fn.params) touch(p.first, 0, false);
    for (size_t i = 0; i < fn.code.size(); ++i) {
        if (fn.code[i].dst.isName()) touch(fn.code[i].dst.name, static_cast<int>(i), false);
        fn.code[i].forEachUse([&](const Operand& o) { touch(o.name, static_cast<int>(i), true); });
    }
    auto edge = [&](const std::string& a, const std::string& b) { if (a != b) { adj[a].insert(b); adj[b].insert(a); } };
    // parameters are simultaneously live at entry
    for (auto& p : fn.params) for (auto& q : fn.params) edge(p.first, q.first);
    for (auto& b : cfg.blocks) {
        if (!b.reachable) continue;
        Set l = live.out[b.id];
        for (size_t idx = b.end; idx-- > b.begin;) {
            const IRInstr& i = fn.code[idx];
            if (i.dst.isName()) {
                for (auto& v : l) if (!(i.kind == IRInstr::Copy && i.a.isName() && i.a.name == v)) edge(i.dst.name, v);   // copy: dst/src may share
                l.erase(i.dst.name);
            }
            i.forEachUse([&](const Operand& o) { l.insert(o.name); });
        }
        if (b.id == 0) for (auto& v : l) for (auto& w : l) edge(v, w);                // everything live into entry (params / undefined) coexists
    }
    for (auto& [n, nb] : adj) { nodes[n].degree = static_cast<int>(nb.size()); for (auto& m : nb) if (n < m) r.edges.push_back({n, m}); }

    auto colorWith = [&](int K, bool record) {
        std::map<std::string, std::set<std::string>> g = adj;
        std::vector<std::string> stack; std::set<std::string> removed;
        std::vector<std::string> order;
        while (removed.size() < g.size()) {
            std::string pick; bool found = false;
            for (auto& [n, nb] : g) {                                    // simplify: remove a node of degree < K
                if (removed.count(n)) continue;
                int d = 0; for (auto& m : nb) if (!removed.count(m)) ++d;
                if (d < K) { pick = n; found = true; break; }
            }
            if (!found) {                                                // potential spill: lowest (uses / degree) cost
                double best = 1e18;
                for (auto& [n, nb] : g) {
                    if (removed.count(n)) continue;
                    int d = 0; for (auto& m : nb) if (!removed.count(m)) ++d;
                    double cost = (nodes[n].uses + 1.0) / (d + 1.0);
                    if (cost < best) { best = cost; pick = n; }
                }
            }
            removed.insert(pick); stack.push_back(pick); order.push_back(pick);
        }
        std::map<std::string, int> color; std::set<std::string> spilled;
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {       // select (optimistic)
            std::vector<char> used(K, 0);
            for (auto& m : adj[*it]) { auto c = color.find(m); if (c != color.end() && c->second >= 0) used[c->second] = 1; }
            int c = -1; for (int x = 0; x < K; ++x) if (!used[x]) { c = x; break; }
            if (c < 0) spilled.insert(*it); else color[*it] = c;
        }
        if (record) {
            r.simplifyOrder = order;
            for (auto& [n, nd] : nodes) { if (color.count(n)) nd.color = color[n]; nd.spilled = spilled.count(n) > 0; }
            r.spills = static_cast<int>(spilled.size());
            std::set<int> cs; for (auto& [n, c] : color) cs.insert(c);
            r.colorsUsed = static_cast<int>(cs.size());
        }
        return spilled.size();
    };
    colorWith(k, true);
    for (int kk = 1; kk <= static_cast<int>(nodes.size()) + 1; ++kk) if (colorWith(kk, false) == 0) { r.minimalK = kk; break; }
    for (auto& [n, nd] : nodes) r.nodes.push_back(nd);
    return r;
}

bool verifyAllocation(const RegAllocResult& r) {
    std::map<std::string, const RegAllocNode*> by;
    for (auto& n : r.nodes) by[n.name] = &n;
    for (auto& [a, b] : r.edges) {
        const auto *x = by[a], *y = by[b];
        if (!x->spilled && !y->spilled && x->color >= 0 && x->color == y->color) return false;
    }
    for (auto& n : r.nodes) if (!n.spilled && (n.color < 0 || n.color >= r.k)) return false;
    return true;
}

// ---- verifiers -------------------------------------------------------------------------------------------------
std::vector<std::string> verifyCFG(const IRFunction& fn, const CFG& cfg) {
    std::vector<std::string> p;
    if (cfg.blocks.empty()) { p.push_back("no blocks"); return p; }
    if (!cfg.blocks[0].pred.empty()) p.push_back("entry block has predecessors");
    size_t next = 0;
    for (auto& b : cfg.blocks) {
        if (b.begin != next) p.push_back("block B" + std::to_string(b.id) + " does not start where the previous block ended");
        next = b.end;
    }
    if (next != fn.code.size()) p.push_back("blocks do not cover all instructions");
    std::set<std::string> labels; for (auto& i : fn.code) if (i.kind == IRInstr::Label) labels.insert(i.target);
    for (auto& i : fn.code) if ((i.kind == IRInstr::Jump || i.kind == IRInstr::JumpIfFalse || i.kind == IRInstr::JumpIfTrue) && !labels.count(i.target)) p.push_back("jump to undefined label " + i.target);
    for (auto& b : cfg.blocks) {
        for (int s : b.succ) { auto& pr = cfg.blocks[s].pred; if (std::find(pr.begin(), pr.end(), b.id) == pr.end()) p.push_back("succ/pred asymmetry B" + std::to_string(b.id) + "->B" + std::to_string(s)); }
        if (b.begin == b.end) continue;
        auto k = fn.code[b.end - 1].kind;
        bool term = k == IRInstr::Jump || k == IRInstr::JumpIfFalse || k == IRInstr::JumpIfTrue || k == IRInstr::Return;
        for (size_t i = b.begin; i + 1 < b.end; ++i) {
            auto ik = fn.code[i].kind;
            if (ik == IRInstr::Jump || ik == IRInstr::JumpIfFalse || ik == IRInstr::JumpIfTrue || ik == IRInstr::Return) p.push_back("terminator in the middle of B" + std::to_string(b.id));
        }
        if (!term && b.id + 1 == static_cast<int>(cfg.blocks.size())) p.push_back("last block B" + std::to_string(b.id) + " falls off the end of the function");
        if (k == IRInstr::Return && !b.succ.empty()) p.push_back("return block has successors");
    }
    return p;
}

std::vector<std::string> verifySSA(const SsaFunction& ssa, const CFG& cfg) {
    std::vector<std::string> p;
    std::set<std::string> defs;
    for (auto& d : ssa.definitions) if (!defs.insert(d).second) p.push_back("name defined twice: " + d);
    for (auto& u : ssa.uses) if (!defs.count(u)) p.push_back("use without definition: " + u);
    for (size_t b = 0; b < ssa.blocks.size() && b < cfg.blocks.size(); ++b) {
        size_t preds = 0; for (int q : cfg.blocks[b].pred) if (cfg.blocks[q].reachable) ++preds;
        for (auto& ph : ssa.blocks[b].phis) if (cfg.blocks[b].reachable && ph.args.size() != preds) p.push_back("phi " + ph.dst + " has " + std::to_string(ph.args.size()) + " operands for " + std::to_string(preds) + " predecessors");
    }
    return p;
}

std::vector<std::set<int>> dominanceFrontiers(const CFG& cfg) {
    std::vector<std::set<int>> df(cfg.blocks.size());
    for (auto& b : cfg.blocks) {
        if (!b.reachable || b.pred.size() < 2) continue;
        for (int p : b.pred) {
            if (!cfg.blocks[p].reachable) continue;
            int runner = p;
            while (runner != b.idom && runner >= 0) { df[runner].insert(b.id); runner = cfg.blocks[runner].idom; }
        }
    }
    return df;
}

} // namespace nova
