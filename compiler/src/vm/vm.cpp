#include "nova/vm.hpp"

namespace nova {

namespace {
struct Frame {
    int func;
    int retIp;
    int callLine;
    std::vector<Value> locals;
    std::vector<Value> stack;
};
}

VMResult runVM(const BcProgram& prog, const VMOptions& opt) {
    VMResult res;
    std::vector<Frame> frames;
    frames.push_back({-1, -1, 0, {}, {}});   // entry-stub frame
    int ip = 0;
    std::string lastEvent = "program start";

    auto snap = [&](bool terminal) {
        if (!opt.trace) return;
        if (res.trace.size() >= opt.maxTrace) { res.traceTruncated = true; return; }
        Snapshot s; s.step = res.steps; s.ip = ip; s.outputCount = res.output.size(); s.event = lastEvent; s.terminal = terminal;
        for (size_t k = 1; k < frames.size(); ++k) s.frames.push_back({frames[k].func, frames[k].callLine, frames[k].locals, frames[k].stack});
        res.trace.push_back(std::move(s));
    };
    auto fail = [&](const std::string& msg) {
        res.ok = false; res.error = msg;
        res.errorIp = ip;
        if (ip >= 0 && ip < static_cast<int>(prog.code.size())) { res.errorLine = prog.code[ip].line; res.errorCol = prog.code[ip].col; }
    };

    while (true) {
        if (ip < 0 || ip >= static_cast<int>(prog.code.size())) { fail("instruction pointer out of range (" + std::to_string(ip) + ")"); break; }
        if (res.steps >= opt.maxSteps) { fail("execution step limit exceeded (" + std::to_string(opt.maxSteps) + " instructions)"); break; }
        snap(false);
        const BcInstr& in = prog.code[ip];
        Frame& fr = frames.back();
        auto& st = fr.stack;
        res.opCounts[opName(in.op)]++;
        ++res.steps;
        auto need = [&](size_t n) { if (st.size() < n) { fail("internal error: operand stack underflow"); return false; } return true; };
        bool stop = false;
        int next = ip + 1;
        switch (in.op) {
            case OpCode::PUSH: st.push_back(in.k); lastEvent = "push " + valueToString(in.k); break;
            case OpCode::POP: if (!need(1)) { stop = true; break; } st.pop_back(); lastEvent = "pop"; break;
            case OpCode::LOAD: st.push_back(fr.locals[in.a]); lastEvent = "load " + prog.comment(ip); break;
            case OpCode::STORE: if (!need(1)) { stop = true; break; } fr.locals[in.a] = st.back(); st.pop_back(); lastEvent = "store " + prog.comment(ip); break;
            case OpCode::ADD: case OpCode::SUB: case OpCode::MUL: case OpCode::DIV: case OpCode::MOD:
            case OpCode::LT: case OpCode::LE: case OpCode::GT: case OpCode::GE: case OpCode::EQ: case OpCode::NE: {
                if (!need(2)) { stop = true; break; }
                Value b = st.back(); st.pop_back(); Value a = st.back(); st.pop_back();
                BinOp bo = BinOp::Add;
                switch (in.op) {
                    case OpCode::ADD: bo = BinOp::Add; break; case OpCode::SUB: bo = BinOp::Sub; break; case OpCode::MUL: bo = BinOp::Mul; break;
                    case OpCode::DIV: bo = BinOp::Div; break; case OpCode::MOD: bo = BinOp::Mod; break; case OpCode::LT: bo = BinOp::Lt; break;
                    case OpCode::LE: bo = BinOp::Le; break; case OpCode::GT: bo = BinOp::Gt; break; case OpCode::GE: bo = BinOp::Ge; break;
                    case OpCode::EQ: bo = BinOp::Eq; break; default: bo = BinOp::Ne; break;
                }
                Value r; std::string err;
                if (!evalBin(bo, a, b, r, err)) { fail(err); stop = true; break; }
                st.push_back(r); lastEvent = valueToString(a) + " " + binOpText(bo) + " " + valueToString(b) + " = " + valueToString(r);
                break;
            }
            case OpCode::NEG: case OpCode::NOT: case OpCode::I2F: case OpCode::F2I: {
                if (!need(1)) { stop = true; break; }
                Value a = st.back(); st.pop_back();
                UnOp uo = in.op == OpCode::NEG ? UnOp::Neg : in.op == OpCode::NOT ? UnOp::Not : in.op == OpCode::I2F ? UnOp::ToFloat : UnOp::ToInt;
                Value r = evalUn(uo, a); st.push_back(r); lastEvent = std::string(unOpText(uo)) + " " + valueToString(a) + " = " + valueToString(r);
                break;
            }
            case OpCode::JMP: next = in.a; lastEvent = "jump to " + std::to_string(in.a); break;
            case OpCode::JZ: case OpCode::JNZ: {
                if (!need(1)) { stop = true; break; }
                bool c = st.back().b; st.pop_back();
                bool take = in.op == OpCode::JZ ? !c : c;
                if (take) next = in.a;
                lastEvent = std::string(take ? "branch taken" : "branch not taken") + " (cond " + (c ? "true" : "false") + ")";
                break;
            }
            case OpCode::CALL: {
                const BcFunction& f = prog.functions[in.a];
                if (!need(in.argc)) { stop = true; break; }
                if (static_cast<int>(frames.size()) > opt.maxCallDepth) { fail("stack overflow: call depth exceeded " + std::to_string(opt.maxCallDepth)); stop = true; break; }
                Frame nf; nf.func = in.a; nf.retIp = ip + 1; nf.callLine = in.line;
                nf.locals.resize(f.slotNames.size());
                for (size_t s = 0; s < f.slotTypes.size(); ++s) {
                    Value z; z.type = f.slotTypes[s]; nf.locals[s] = z;
                }
                for (int a = in.argc - 1; a >= 0; --a) { nf.locals[a] = st.back(); st.pop_back(); }
                frames.push_back(std::move(nf));
                res.maxCallDepth = std::max<int>(res.maxCallDepth, static_cast<int>(frames.size()) - 1);
                next = f.entry; lastEvent = "call " + f.name + " (new frame #" + std::to_string(frames.size() - 1) + ")";
                break;
            }
            case OpCode::RET: case OpCode::RETV: {
                Value rv;
                if (in.op == OpCode::RET) { if (!need(1)) { stop = true; break; } rv = st.back(); }
                int retIp = fr.retIp; std::string name = prog.functions[fr.func].name;
                frames.pop_back();
                if (in.op == OpCode::RET) frames.back().stack.push_back(rv);
                next = retIp; lastEvent = "return from " + name + (in.op == OpCode::RET ? " with " + valueToString(rv) : "") + " (frame destroyed)";
                break;
            }
            case OpCode::PRINT: {
                if (!need(1)) { stop = true; break; }
                std::string s = valueToString(st.back()); st.pop_back();
                if (res.output.size() >= opt.maxOutputLines) { fail("output limit exceeded (" + std::to_string(opt.maxOutputLines) + " lines)"); stop = true; break; }
                res.output.push_back(s); lastEvent = "print " + s;
                break;
            }
            case OpCode::HALT: lastEvent = "halt"; stop = true; break;
        }
        for (auto& f2 : frames) res.maxStackDepth = std::max<int>(res.maxStackDepth, static_cast<int>(f2.stack.size()));
        if (!res.ok) { lastEvent = "runtime error: " + res.error; snap(true); return res; }
        if (stop) { ip = in.op == OpCode::HALT ? ip : next; snap(true); return res; }
        ip = next;
    }
    lastEvent = "runtime error: " + res.error;
    snap(true);
    return res;
}

Json snapshotToJson(const Snapshot& s) {
    Json j = Json::object();
    j.set("step", s.step).set("ip", s.ip).set("out", static_cast<int>(s.outputCount)).set("event", s.event).set("terminal", s.terminal);
    Json frames = Json::array();
    for (auto& f : s.frames) {
        Json o = Json::object();
        Json loc = Json::array(), stk = Json::array();
        for (auto& v : f.locals) loc.push(valueToString(v));
        for (auto& v : f.stack) stk.push(valueToString(v));
        o.set("fn", f.function).set("callLine", f.callLine).set("locals", loc).set("stack", stk);
        frames.push(o);
    }
    j.set("frames", frames);
    return j;
}

Json VMResult::toJson(const BcProgram* prog, bool includeTrace) const {
    Json j = Json::object();
    Json out = Json::array();
    for (auto& s : output) out.push(s);
    j.set("ok", ok).set("output", out).set("steps", steps).set("maxStackDepth", maxStackDepth).set("maxCallDepth", maxCallDepth);
    if (!ok) {
        Json e = Json::object();
        e.set("message", error).set("ip", errorIp).set("line", errorLine).set("col", errorCol);
        j.set("error", e);
    }
    Json oc = Json::object();
    for (auto& [k, v] : opCounts) oc.set(k, v);
    j.set("opCounts", oc);
    if (includeTrace) {
        Json t = Json::array();
        for (auto& s : trace) t.push(snapshotToJson(s));
        j.set("trace", t).set("traceTruncated", traceTruncated);
    }
    (void)prog;
    return j;
}

} // namespace nova
