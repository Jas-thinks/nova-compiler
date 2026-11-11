#include "nova/optimizer.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace nova {

const std::vector<std::pair<std::string, std::string>>& optimizationPasses() {
    static const std::vector<std::pair<std::string, std::string>> p = {
        {"inlining", "Function inlining"},
        {"constant_propagation", "Constant & copy propagation"},
        {"constant_folding", "Constant folding"},
        {"algebraic", "Algebraic simplification"},
        {"cse", "Common subexpression elimination"},
        {"unreachable", "Unreachable code elimination"},
        {"dce", "Dead code elimination (liveness)"},
    };
    return p;
}

bool OptConfig::parse(const std::string& list, OptConfig& out, std::string& error) {
    if (list == "all") { out = OptConfig(); return true; }
    out = OptConfig::none();
    if (list == "none" || list.empty()) return true;
    std::stringstream ss(list); std::string item;
    while (std::getline(ss, item, ',')) {
        if (item == "inlining") out.inlining = true;
        else if (item == "constant_folding") out.constantFolding = true;
        else if (item == "constant_propagation") out.constantPropagation = true;
        else if (item == "algebraic") out.algebraic = true;
        else if (item == "cse") out.cse = true;
        else if (item == "unreachable") out.unreachable = true;
        else if (item == "dce") out.dce = true;
        else { error = "unknown optimization pass '" + item + "'"; return false; }
    }
    return true;
}

Json OptConfig::toJson() const {
    Json j = Json::object();
    j.set("inlining", inlining).set("constant_folding", constantFolding).set("constant_propagation", constantPropagation).set("algebraic", algebraic)
     .set("cse", cse).set("unreachable", unreachable).set("dce", dce);
    return j;
}

Json OptRecord::toJson() const {
    Json j = Json::object();
    j.set("pass", pass).set("function", function).set("before", before).set("after", after).set("explanation", explanation).set("round", round);
    if (line > 0) j.set("line", line).set("col", col);
    return j;
}

Json OptResult::toJson() const {
    Json j = Json::object();
    Json h = Json::array();
    for (auto& r : history) h.push(r.toJson());
    Json pp = Json::object();
    for (auto& [k, v] : perPass) pp.set(k, v);
    j.set("history", h).set("perPass", pp).set("rounds", rounds)
     .set("instructionsBefore", static_cast<int>(instructionsBefore)).set("instructionsAfter", static_cast<int>(instructionsAfter))
     .set("eliminated", static_cast<int>(instructionsBefore) - static_cast<int>(instructionsAfter));
    return j;
}

namespace {

struct Ctx {
    IRFunction* fn;
    std::vector<OptRecord>* hist;
    int round;
    void rec(const char* pass, const std::string& before, const std::string& after, const std::string& why, const IRInstr& at) {
        OptRecord r; r.pass = pass; r.function = fn->name; r.before = before; r.after = after; r.explanation = why;
        r.line = at.line; r.col = at.col; r.round = round;
        hist->push_back(std::move(r));
    }
};

bool defines(const IRInstr& i) { return i.dst.isName(); }

// ---------- constant & copy propagation (local to basic blocks) ----------
bool constProp(Ctx& c) {
    bool changed = false;
    // Function-wide: a variable assigned exactly once (and not a parameter) always holds that value
    // wherever it is read, because the language requires declaration before use.
    std::map<std::string, int> defCount;
    for (auto& p : c.fn->params) defCount[p.first]++;
    for (auto& i : c.fn->code) if (defines(i)) defCount[i.dst.name]++;
    std::map<std::string, Operand> globalConst;
    for (auto& i : c.fn->code)
        if (i.kind == IRInstr::Copy && i.dst.isName() && i.a.isConst() && defCount[i.dst.name] == 1) globalConst[i.dst.name] = i.a;
    std::map<std::string, Operand> env;
    for (auto& i : c.fn->code) {
        if (i.kind == IRInstr::Label) { env.clear(); continue; }
        std::string before = i.str();
        std::vector<std::string> notes;
        auto sub = [&](Operand& o) {
            if (!o.isName()) return;
            auto it = env.find(o.name);
            if (it == env.end()) {
                auto g = globalConst.find(o.name);
                if (g == globalConst.end()) return;
                notes.push_back(o.name + " is assigned only once, to the constant " + g->second.str());
                o = g->second;
                return;
            }
            notes.push_back(o.name + (it->second.isConst() ? " is the constant " : " is a copy of ") + it->second.str());
            o = it->second;
        };
        sub(i.a); sub(i.b);
        for (auto& x : i.args) sub(x);
        if (!notes.empty()) {
            changed = true;
            std::string why = "Dataflow facts: ";
            for (size_t k = 0; k < notes.size(); ++k) why += (k ? "; " : "") + notes[k];
            c.rec("constant_propagation", before, i.str(), why + ", so uses are replaced by the known value.", i);
        }
        if (defines(i)) {
            env.erase(i.dst.name);
            for (auto it = env.begin(); it != env.end();) { if (it->second.isName() && it->second.name == i.dst.name) it = env.erase(it); else ++it; }
            if (i.kind == IRInstr::Copy) {
                if (i.a.isConst()) env[i.dst.name] = i.a;
                else if (i.a.isName() && i.a.name != i.dst.name) env[i.dst.name] = i.a;
            }
        }
    }
    return changed;
}

// ---------- constant folding ----------
bool constFold(Ctx& c) {
    bool changed = false;
    std::vector<IRInstr> out;
    for (auto& i : c.fn->code) {
        std::string before = i.str();
        if (i.kind == IRInstr::Bin && i.a.isConst() && i.b.isConst()) {
            Value r; std::string err;
            if (evalBin(i.bop, i.a.val, i.b.val, r, err)) {
                IRInstr n = i; n.kind = IRInstr::Copy; n.a = Operand::cnst(r); n.b = Operand::none();
                c.rec("constant_folding", before, n.str(), "Both operands are compile-time constants, so " + before.substr(before.find('=') + 2) + " is evaluated by the compiler.", i);
                out.push_back(n); changed = true; continue;
            }
            // division by zero is left for run time so the error is preserved
        } else if (i.kind == IRInstr::Un && i.a.isConst()) {
            IRInstr n = i; n.kind = IRInstr::Copy; n.a = Operand::cnst(evalUn(i.uop, i.a.val));
            c.rec("constant_folding", before, n.str(), "The operand is a constant, so the unary operation is evaluated at compile time.", i);
            out.push_back(n); changed = true; continue;
        } else if ((i.kind == IRInstr::JumpIfFalse || i.kind == IRInstr::JumpIfTrue) && i.a.isConst()) {
            bool taken = (i.kind == IRInstr::JumpIfFalse) ? !i.a.val.b : i.a.val.b;
            if (taken) {
                IRInstr n = i; n.kind = IRInstr::Jump; n.a = Operand::none();
                c.rec("constant_folding", before, n.str(), "The condition is constant " + i.a.str() + ", so the branch is always taken: it becomes an unconditional jump.", i);
                out.push_back(n);
            } else {
                c.rec("constant_folding", before, "(removed)", "The condition is constant " + i.a.str() + ", so the branch is never taken and the test is deleted.", i);
            }
            changed = true; continue;
        }
        out.push_back(i);
    }
    c.fn->code = std::move(out);
    return changed;
}

// ---------- algebraic simplification ----------
bool plusZero(const Operand& o) {
    if (!o.isConst()) return false;
    if (o.val.type == Type::Int) return o.val.i == 0;
    return o.val.type == Type::Float && o.val.f == 0 && !std::signbit(o.val.f);
}
bool isOneC(const Operand& o) { return o.isConst() && o.val.type != Type::Bool && o.val.isOne(); }
bool isZeroInt(const Operand& o) { return o.isConst() && o.val.type == Type::Int && o.val.i == 0; }
bool isTwo(const Operand& o) { return o.isConst() && ((o.val.type == Type::Int && o.val.i == 2) || (o.val.type == Type::Float && o.val.f == 2.0)); }

bool algebraic(Ctx& c) {
    bool changed = false;
    for (auto& i : c.fn->code) {
        if (i.kind != IRInstr::Bin) continue;
        std::string before = i.str();
        bool isInt = i.a.type == Type::Int;
        auto toCopy = [&](const Operand& src, const std::string& why) {
            IRInstr n = i; n.kind = IRInstr::Copy; n.a = src; n.b = Operand::none();
            c.rec("algebraic", before, n.str(), why, i); i = n; changed = true;
        };
        auto toConst = [&](const Value& v, const std::string& why) { toCopy(Operand::cnst(v), why); };
        bool same = i.a.isName() && i.a == i.b;
        switch (i.bop) {
            case BinOp::Add:
                if (isInt && isZeroInt(i.b)) toCopy(i.a, "x + 0 = x");
                else if (isInt && isZeroInt(i.a)) toCopy(i.b, "0 + x = x");
                break;
            case BinOp::Sub:
                if (plusZero(i.b)) toCopy(i.a, "x - 0 = x");
                else if (isInt && same) toConst(Value::Int(0), "x - x = 0");
                break;
            case BinOp::Mul:
                if (isOneC(i.b)) toCopy(i.a, "x * 1 = x");
                else if (isOneC(i.a)) toCopy(i.b, "1 * x = x");
                else if (isInt && (isZeroInt(i.a) || isZeroInt(i.b))) toConst(Value::Int(0), "x * 0 = 0");
                else if (isTwo(i.b) && i.a.isName()) { IRInstr n = i; n.bop = BinOp::Add; n.b = i.a; c.rec("algebraic", before, n.str(), "Strength reduction: x * 2 = x + x (an addition is cheaper than a multiplication).", i); i = n; changed = true; }
                else if (isTwo(i.a) && i.b.isName()) { IRInstr n = i; n.bop = BinOp::Add; n.a = i.b; c.rec("algebraic", before, n.str(), "Strength reduction: 2 * x = x + x (an addition is cheaper than a multiplication).", i); i = n; changed = true; }
                break;
            case BinOp::Div:
                if (isOneC(i.b)) toCopy(i.a, "x / 1 = x");
                break;
            case BinOp::Mod:
                if (isInt && i.b.isConst() && i.b.val.i == 1) toConst(Value::Int(0), "x % 1 = 0");
                break;
            case BinOp::Eq: case BinOp::Le: case BinOp::Ge:
                if (isInt && same) toConst(Value::Bool(true), "x compared with itself using ==, <= or >= is always true");
                break;
            case BinOp::Ne: case BinOp::Lt: case BinOp::Gt:
                if (isInt && same) toConst(Value::Bool(false), "x compared with itself using !=, < or > is always false");
                break;
        }
    }
    return changed;
}

// ---------- local common subexpression elimination ----------
bool cse(Ctx& c) {
    struct Avail { std::string key; Operand holder; std::vector<std::string> deps; };
    std::vector<Avail> table;
    bool changed = false;
    for (auto& i : c.fn->code) {
        if (i.kind == IRInstr::Label) { table.clear(); continue; }
        bool pure = i.kind == IRInstr::Bin || i.kind == IRInstr::Un;
        std::string key;
        if (pure) {
            Operand x = i.a, y = i.b;
            if (i.kind == IRInstr::Bin && isCommutative(i.bop) && y.str() < x.str()) std::swap(x, y);
            key = (i.kind == IRInstr::Bin ? std::string("B") + binOpText(i.bop) : std::string("U") + unOpText(i.uop)) + "|" + typeName(x.type) + ":" + x.str() + "|" + typeName(y.type) + ":" + y.str();
        }
        bool replaced = false;
        if (pure) {
            for (auto& e : table) if (e.key == key) {
                std::string before = i.str();
                IRInstr n = i; n.kind = IRInstr::Copy; n.a = e.holder; n.b = Operand::none();
                c.rec("cse", before, n.str(), "The same expression was already computed into " + e.holder.str() + " and neither operand changed since, so the value is reused.", i);
                i = n; changed = true; replaced = true; break;
            }
        }
        if (defines(i)) {
            const std::string& d = i.dst.name;
            table.erase(std::remove_if(table.begin(), table.end(), [&](const Avail& e) {
                return e.holder.name == d || std::find(e.deps.begin(), e.deps.end(), d) != e.deps.end(); }), table.end());
            if (pure && !replaced) {
                Avail e; e.key = key; e.holder = i.dst;
                if (i.a.isName()) e.deps.push_back(i.a.name);
                if (i.b.isName()) e.deps.push_back(i.b.name);
                if (std::find(e.deps.begin(), e.deps.end(), d) == e.deps.end()) table.push_back(e);
            }
        }
    }
    return changed;
}

// ---------- CFG helpers ----------
struct Block { size_t begin, end; std::vector<int> succ; };  // [begin, end)

std::vector<Block> buildBlocks(const std::vector<IRInstr>& code) {
    std::vector<Block> bl;
    size_t start = 0;
    for (size_t k = 0; k < code.size(); ++k) {
        bool endsHere = false;
        auto kind = code[k].kind;
        if (kind == IRInstr::Jump || kind == IRInstr::JumpIfFalse || kind == IRInstr::JumpIfTrue || kind == IRInstr::Return) endsHere = true;
        if (k + 1 < code.size() && code[k + 1].kind == IRInstr::Label) endsHere = true;
        if (k == code.size() - 1) endsHere = true;
        if (endsHere) { bl.push_back({start, k + 1, {}}); start = k + 1; }
    }
    std::map<std::string, int> labelBlock;
    for (size_t b = 0; b < bl.size(); ++b) for (size_t k = bl[b].begin; k < bl[b].end; ++k) if (code[k].kind == IRInstr::Label) labelBlock[code[k].target] = static_cast<int>(b);
    for (size_t b = 0; b < bl.size(); ++b) {
        const IRInstr& last = code[bl[b].end - 1];
        int next = b + 1 < bl.size() ? static_cast<int>(b) + 1 : -1;
        switch (last.kind) {
            case IRInstr::Return: break;
            case IRInstr::Jump: if (labelBlock.count(last.target)) bl[b].succ.push_back(labelBlock[last.target]); break;
            case IRInstr::JumpIfFalse: case IRInstr::JumpIfTrue:
                if (labelBlock.count(last.target)) bl[b].succ.push_back(labelBlock[last.target]);
                if (next >= 0) bl[b].succ.push_back(next);
                break;
            default: if (next >= 0) bl[b].succ.push_back(next);
        }
    }
    return bl;
}

// ---------- unreachable code elimination ----------
bool unreachable(Ctx& c) {
    auto& code = c.fn->code;
    if (code.empty()) return false;
    bool changed = false;
    auto bl = buildBlocks(code);
    std::vector<char> reach(bl.size(), 0);
    std::vector<int> work = {0}; reach[0] = 1;
    while (!work.empty()) { int b = work.back(); work.pop_back(); for (int s : bl[b].succ) if (!reach[s]) { reach[s] = 1; work.push_back(s); } }
    std::vector<IRInstr> out;
    for (size_t b = 0; b < bl.size(); ++b) {
        for (size_t k = bl[b].begin; k < bl[b].end; ++k) {
            if (reach[b]) { out.push_back(code[k]); continue; }
            c.rec("unreachable", code[k].str(), "(removed)", "No path from the function entry reaches this basic block (control-flow reachability analysis).", code[k]);
            changed = true;
        }
    }
    code = std::move(out);
    // jump to the immediately following label(s)
    for (size_t k = 0; k < code.size();) {
        auto kind = code[k].kind;
        if (kind == IRInstr::Jump || kind == IRInstr::JumpIfFalse || kind == IRInstr::JumpIfTrue) {
            bool next = false;
            for (size_t m = k + 1; m < code.size() && code[m].kind == IRInstr::Label; ++m) if (code[m].target == code[k].target) next = true;
            if (next) {
                c.rec("unreachable", code[k].str(), "(removed)", "The jump targets the very next instruction, so it has no effect.", code[k]);
                code.erase(code.begin() + k); changed = true; continue;
            }
        }
        ++k;
    }
    // unreferenced labels
    std::set<std::string> used;
    for (auto& i : code) if (i.kind == IRInstr::Jump || i.kind == IRInstr::JumpIfFalse || i.kind == IRInstr::JumpIfTrue) used.insert(i.target);
    for (size_t k = 0; k < code.size();) {
        if (code[k].kind == IRInstr::Label && !used.count(code[k].target)) {
            c.rec("unreachable", code[k].str(), "(removed)", "No jump refers to this label any more.", code[k]);
            code.erase(code.begin() + k); changed = true; continue;
        }
        ++k;
    }
    return changed;
}

// ---------- dead code elimination via live-variable analysis ----------
bool removable(const IRInstr& i) {
    if (!i.dst.isName()) return false;
    switch (i.kind) {
        case IRInstr::Copy: case IRInstr::Un: return true;
        case IRInstr::Bin:
            if (i.a.type == Type::Int && (i.bop == BinOp::Div || i.bop == BinOp::Mod)) return i.b.isConst() && i.b.val.i != 0;  // may trap otherwise
            return true;
        default: return false;
    }
}

bool dce(Ctx& c) {
    auto& code = c.fn->code;
    if (code.empty()) return false;
    auto bl = buildBlocks(code);
    size_t n = bl.size();
    std::vector<std::set<std::string>> liveIn(n), liveOut(n);
    bool again = true;
    while (again) {
        again = false;
        for (size_t bi = n; bi-- > 0;) {
            std::set<std::string> out;
            for (int s : bl[bi].succ) out.insert(liveIn[s].begin(), liveIn[s].end());
            std::set<std::string> live = out;
            for (size_t k = bl[bi].end; k-- > bl[bi].begin;) {
                const IRInstr& i = code[k];
                if (defines(i)) live.erase(i.dst.name);
                i.forEachUse([&](const Operand& o) { live.insert(o.name); });
            }
            if (out != liveOut[bi] || live != liveIn[bi]) { liveOut[bi] = out; liveIn[bi] = live; again = true; }
        }
    }
    std::vector<char> kill(code.size(), 0);
    bool changed = false;
    for (size_t bi = 0; bi < n; ++bi) {
        std::set<std::string> live = liveOut[bi];
        for (size_t k = bl[bi].end; k-- > bl[bi].begin;) {
            const IRInstr& i = code[k];
            if (removable(i) && !live.count(i.dst.name)) {
                c.rec("dce", i.str(), "(removed)", "'" + i.dst.name + "' is not live after this instruction (backward liveness analysis): its value is never read before being overwritten or the function ends.", i);
                kill[k] = 1; changed = true; continue;
            }
            if (defines(i)) live.erase(i.dst.name);
            i.forEachUse([&](const Operand& o) { live.insert(o.name); });
        }
    }
    if (changed) {
        std::vector<IRInstr> out;
        for (size_t k = 0; k < code.size(); ++k) if (!kill[k]) out.push_back(code[k]);
        code = std::move(out);
    }
    return changed;
}

} // namespace

// ---------- interprocedural: function inlining ----------
// A call is replaced by a renamed copy of the callee's body: parameters become copies of the arguments, `return v`
// becomes `dst = v; goto end`. Only callees that are (transitively) non-recursive, small, and not `main` are inlined,
// and every caller may grow by a bounded amount, so the pass always terminates and never blows up code size.
namespace {
constexpr size_t kInlineMaxCalleeSize = 30;
constexpr size_t kInlineMaxCallerSize = 600;

bool callsOther(const IRFunction& f) { for (auto& i : f.code) if (i.kind == IRInstr::Call && !isIntrinsic(i.callee)) return true; return false; }

std::set<std::string> recursiveFunctions(const IRProgram& p) {
    std::map<std::string, std::set<std::string>> edges;
    for (auto& f : p.functions) for (auto& i : f.code) if (i.kind == IRInstr::Call && !isIntrinsic(i.callee)) edges[f.name].insert(i.callee);
    std::set<std::string> rec;
    for (auto& f : p.functions) {                       // f is recursive iff f is reachable from itself
        std::set<std::string> seen; std::vector<std::string> work(edges[f.name].begin(), edges[f.name].end());
        while (!work.empty()) {
            std::string g = work.back(); work.pop_back();
            if (g == f.name) { rec.insert(f.name); break; }
            if (!seen.insert(g).second) continue;
            for (auto& h : edges[g]) work.push_back(h);
        }
    }
    return rec;
}

bool inlineProgram(IRProgram& prog, std::vector<OptRecord>& hist, int& site) {   // `site` is unique across rounds, so generated names never collide
    bool any = false;
    auto rec = recursiveFunctions(prog);
    std::map<std::string, const IRFunction*> byName;
    for (auto& f : prog.functions) byName[f.name] = &f;
    std::map<std::string, IRFunction> snapshot;          // callee bodies as of the start of the round (stable while callers change)
    for (auto& f : prog.functions) snapshot[f.name] = f;
    for (auto& caller : prog.functions) {
        std::vector<IRInstr> out;
        for (auto& ins : caller.code) {
            bool done = false;
            if (ins.kind == IRInstr::Call && !isIntrinsic(ins.callee) && ins.callee != "main" && ins.callee != caller.name && !rec.count(ins.callee) &&
                snapshot.count(ins.callee) && snapshot[ins.callee].code.size() <= kInlineMaxCalleeSize && out.size() + snapshot[ins.callee].code.size() < kInlineMaxCallerSize) {
                const IRFunction& g = snapshot[ins.callee];
                const std::string tag = "_i" + std::to_string(++site);   // alphanumeric: also a valid assembly label fragment
                std::map<std::string, std::string> ren;
                auto rn = [&](const std::string& n) { auto it = ren.find(n); if (it != ren.end()) return it->second; return ren[n] = g.name + tag + "." + n; };   // e.g. sq_i3.x
                for (auto& l : g.locals) { caller.locals.push_back({rn(l.name), l.type, l.isTemp, false}); }
                for (auto& p : g.params) rn(p.first);
                auto op = [&](Operand o) { if (o.isName()) o.name = rn(o.name); return o; };
                const std::string endLabel = "inl_end" + tag;
                std::string before = ins.str();
                for (size_t k = 0; k < g.params.size(); ++k) {          // bind parameters
                    IRInstr c; c.kind = IRInstr::Copy; c.dst = Operand::var(rn(g.params[k].first), g.params[k].second); c.a = ins.args[k]; c.line = ins.line; c.col = ins.col; out.push_back(c);
                }
                for (auto& gi : g.code) {
                    IRInstr n = gi; n.dst = op(gi.dst); n.a = op(gi.a); n.b = op(gi.b);
                    for (auto& a : n.args) a = op(a);
                    if (n.kind == IRInstr::Label || n.kind == IRInstr::Jump || n.kind == IRInstr::JumpIfFalse || n.kind == IRInstr::JumpIfTrue) n.target += tag;
                    if (n.kind == IRInstr::Return) {
                        if (ins.dst.isName() && !n.a.isNone()) { IRInstr c; c.kind = IRInstr::Copy; c.dst = ins.dst; c.a = n.a; c.line = gi.line; c.col = gi.col; out.push_back(c); }
                        IRInstr j; j.kind = IRInstr::Jump; j.target = endLabel; j.line = gi.line; j.col = gi.col; out.push_back(j);
                        continue;
                    }
                    out.push_back(n);
                }
                IRInstr e; e.kind = IRInstr::Label; e.target = endLabel; e.line = ins.line; e.col = ins.col; out.push_back(e);
                OptRecord r; r.pass = "inlining"; r.function = caller.name; r.before = before;
                r.after = "(" + std::to_string(g.code.size()) + " instructions of " + g.name + " inlined)";
                r.explanation = "Call to '" + g.name + "' replaced by its body: parameters become copies of the arguments, 'return v' becomes 'dst = v; goto end'. This removes call/return overhead and exposes the body to constant propagation, CSE and DCE in the caller.";
                r.line = ins.line; r.col = ins.col; r.round = 0;
                hist.push_back(std::move(r));
                done = any = true;
            }
            if (!done) out.push_back(ins);
        }
        caller.code = std::move(out);
    }
    (void)byName; (void)callsOther;
    return any;
}
} // namespace

OptResult optimize(const IRProgram& in, const OptConfig& cfg) {
    OptResult res;
    res.program = in;
    res.instructionsBefore = in.instructionCount();
    int inlineSite = 0;
    if (cfg.inlining) for (int round = 0; round < 3 && inlineProgram(res.program, res.history, inlineSite); ++round) {}
    for (auto& f : res.program.functions) {
        for (int round = 1; round <= cfg.maxRounds && cfg.any(); ++round) {
            Ctx c{&f, &res.history, round};
            bool changed = false;
            if (cfg.constantPropagation) changed |= constProp(c);
            if (cfg.constantFolding) changed |= constFold(c);
            if (cfg.algebraic) changed |= algebraic(c);
            if (cfg.cse) changed |= cse(c);
            if (cfg.unreachable) changed |= unreachable(c);
            if (cfg.dce) changed |= dce(c);
            res.rounds = std::max(res.rounds, round);
            if (!changed) break;
        }
    }
    for (auto& r : res.history) res.perPass[r.pass]++;
    res.instructionsAfter = res.program.instructionCount();
    return res;
}

} // namespace nova
