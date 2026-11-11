// Random NOVA program generator + differential fuzz driver.
#include <fstream>
#include <random>
#include <sstream>
#include "nova/pipeline.hpp"

namespace nova {

namespace {

struct FSig { std::string name; std::vector<Type> params; Type ret; };
struct FVar { std::string name; Type type; bool assignable; };

class Gen {
public:
    explicit Gen(uint64_t seed) : rng_(seed) {}

    std::string program() {
        int nf = pick(0, 3);
        for (int i = 0; i < nf; ++i) helper(i);
        os_ << "fn main() {\n";
        scopes_.clear(); scopes_.push_back({});
        stmts(pick(6, 16), 1, Type::Void, 0);
        os_ << "}\n";
        return os_.str();
    }

private:
    std::mt19937_64 rng_;
    std::ostringstream os_;
    std::vector<std::vector<FVar>> scopes_;
    std::vector<FSig> funcs_;
    int counter_ = 0;

    int pick(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng_); }
    bool chance(int pct) { return pick(1, 100) <= pct; }
    std::string fresh(const char* p) { return std::string(p) + std::to_string(++counter_); }
    void indent(int d) { for (int i = 0; i < d; ++i) os_ << "    "; }
    static const char* tn(Type t) { return t == Type::Int ? "int" : t == Type::Float ? "float" : "bool"; }
    Type randType() { int k = pick(0, 9); return k < 6 ? Type::Int : k < 8 ? Type::Float : Type::Bool; }

    void helper(int idx) {
        FSig sig; sig.name = "f" + std::to_string(idx);
        int np = pick(0, 3);
        scopes_.clear(); scopes_.push_back({});
        std::string ps;
        for (int i = 0; i < np; ++i) {
            Type t = chance(75) ? Type::Int : Type::Float;
            std::string n = fresh("p");
            sig.params.push_back(t);
            scopes_[0].push_back({n, t, true});
            ps += (i ? ", " : "") + n + ": " + tn(t);
        }
        int rk = pick(0, 9);
        sig.ret = rk < 5 ? Type::Int : rk < 7 ? Type::Float : rk < 8 ? Type::Bool : Type::Void;
        os_ << "fn " << sig.name << "(" << ps << ")";
        if (sig.ret != Type::Void) os_ << " -> " << tn(sig.ret);
        os_ << " {\n";
        if (chance(15)) {   // a function whose first statement is a (never-executed) loop: its entry block is a jump target
            indent(1); os_ << "while (false && " << (np > 0 && sig.params[0] == Type::Int ? scopes_[0][0].name + " < 3" : "true") << ") {\n";
            indent(2); os_ << "print(" << intLit() << ");\n"; indent(1); os_ << "}\n";
        }
        stmts(pick(1, 6), 1, sig.ret, 0);
        if (sig.ret != Type::Void) { indent(1); os_ << "return " << expr(sig.ret, 2) << ";\n"; }
        os_ << "}\n\n";
        funcs_.push_back(sig);
    }

    std::vector<const FVar*> visible(Type t, bool onlyAssignable) {
        std::vector<const FVar*> v;
        for (auto& s : scopes_) for (auto& x : s) if (x.type == t && (!onlyAssignable || x.assignable)) v.push_back(&x);
        return v;
    }

    void stmts(int n, int depth, Type ret, int loopDepth) {
        for (int i = 0; i < n; ++i) stmt(depth, ret, loopDepth);
    }

    void block(int depth, Type ret, int loopDepth, int n) {
        os_ << "{\n";
        scopes_.push_back({});
        stmts(n, depth + 1, ret, loopDepth);
        scopes_.pop_back();
        indent(depth); os_ << "}";
    }

    void stmt(int depth, Type ret, int loopDepth) {
        int k = pick(1, 100);
        indent(depth);
        if (k <= 34) {                          // let
            Type t = randType(); std::string n = fresh("v");
            os_ << "let " << n << ": " << tn(t) << " = " << expr(t, 3) << ";\n";
            scopes_.back().push_back({n, t, true});
        } else if (k <= 52) {                   // assign
            Type t = randType();
            auto vs = visible(t, true);
            if (vs.empty()) { os_ << "print(" << expr(t, 2) << ");\n"; return; }
            os_ << vs[pick(0, (int)vs.size() - 1)]->name << " = " << expr(t, 3) << ";\n";
        } else if (k <= 68) {                   // print
            os_ << "print(" << expr(randType(), 3) << ");\n";
        } else if (k <= 80 && depth < 4) {      // if / else
            os_ << "if (" << expr(Type::Bool, 2) << ") ";
            block(depth, ret, loopDepth, pick(1, 3));
            if (chance(60)) { os_ << " else "; block(depth, ret, loopDepth, pick(1, 3)); }
            os_ << "\n";
        } else if (k <= 90 && depth < 4 && loopDepth < 2) {   // bounded while
            std::string i = fresh("i");
            os_ << "let " << i << ": int = 0;\n";
            scopes_.back().push_back({i, Type::Int, false});
            indent(depth); os_ << "while (" << i << " < " << pick(1, 4) << ") ";
            os_ << "{\n";
            scopes_.push_back({});
            stmts(pick(1, 4), depth + 1, ret, loopDepth + 1);
            indent(depth + 1); os_ << i << " = " << i << " + 1;\n";
            scopes_.pop_back();
            indent(depth); os_ << "}\n";
        } else if (k <= 96 && !funcs_.empty()) {   // call statement
            const FSig& f = funcs_[pick(0, (int)funcs_.size() - 1)];
            os_ << callText(f, 2) << ";\n";
        } else if (ret != Type::Void && depth > 1 && chance(50)) {   // early return inside nested block
            os_ << "if (" << expr(Type::Bool, 2) << ") { return " << expr(ret, 2) << "; }\n";
        } else {
            os_ << "print(" << expr(Type::Int, 2) << ");\n";
        }
    }

    std::string callText(const FSig& f, int d) {
        std::string s = f.name + "(";
        for (size_t i = 0; i < f.params.size(); ++i) s += (i ? ", " : "") + expr(f.params[i], d);
        return s + ")";
    }

    std::string intLit() {
        static const int lits[] = {0, 1, 2, 3, 5, 7, 10, 20, 100, -1, -3, 42};
        return std::to_string(lits[pick(0, 11)]);
    }
    std::string floatLit() {
        static const char* lits[] = {"0.0", "1.0", "2.0", "0.5", "1.5", "3.25", "10.0", "-2.5"};
        return lits[pick(0, 7)];
    }

    std::string expr(Type t, int d) {
        if (t == Type::Int) {
            int k = pick(1, 100);
            auto vs = visible(Type::Int, false);
            if (d <= 0 || k <= 15) return chance(50) || vs.empty() ? "(" + intLit() + ")" : vs[pick(0, (int)vs.size() - 1)]->name;
            if (k <= 30 && !vs.empty()) return vs[pick(0, (int)vs.size() - 1)]->name;
            if (k <= 78) {
                static const char* ops[] = {"+", "-", "*", "/", "%"};
                const char* op = ops[pick(0, 4)];
                std::string rhs;
                if (op[0] == '/' || op[0] == '%') rhs = chance(85) ? "(" + std::to_string(pick(1, 9)) + ")" : expr(Type::Int, d - 1);
                else rhs = expr(Type::Int, d - 1);
                return "(" + expr(Type::Int, d - 1) + " " + op + " " + rhs + ")";
            }
            if (k <= 84) return "(-" + expr(Type::Int, d - 1) + ")";
            if (k <= 90) return "to_int(" + expr(Type::Float, d - 1) + ")";
            if (auto f = findFunc(Type::Int)) return callText(*f, d - 1);
            return "(" + intLit() + ")";
        }
        if (t == Type::Float) {
            int k = pick(1, 100);
            auto vs = visible(Type::Float, false);
            if (d <= 0 || k <= 20) return chance(60) || vs.empty() ? "(" + floatLit() + ")" : vs[pick(0, (int)vs.size() - 1)]->name;
            if (k <= 80) { static const char* ops[] = {"+", "-", "*", "/"}; return "(" + expr(Type::Float, d - 1) + " " + ops[pick(0, 3)] + " " + expr(Type::Float, d - 1) + ")"; }
            if (k <= 88) return "to_float(" + expr(Type::Int, d - 1) + ")";
            if (auto f = findFunc(Type::Float)) return callText(*f, d - 1);
            return "(" + floatLit() + ")";
        }
        int k = pick(1, 100);
        auto vs = visible(Type::Bool, false);
        if (d <= 0 || k <= 10) return chance(60) || vs.empty() ? (chance(50) ? "true" : "false") : vs[pick(0, (int)vs.size() - 1)]->name;
        if (k <= 55) {
            static const char* ops[] = {"<", "<=", ">", ">=", "==", "!="};
            Type nt = chance(75) ? Type::Int : Type::Float;
            return "(" + expr(nt, d - 1) + " " + ops[pick(0, 5)] + " " + expr(nt, d - 1) + ")";
        }
        if (k <= 80) return "(" + expr(Type::Bool, d - 1) + (chance(50) ? " && " : " || ") + expr(Type::Bool, d - 1) + ")";
        if (k <= 90) return "(!" + expr(Type::Bool, d - 1) + ")";
        if (k <= 95) return "(" + expr(Type::Bool, d - 1) + (chance(50) ? " == " : " != ") + expr(Type::Bool, d - 1) + ")";
        if (auto f = findFunc(Type::Bool)) return callText(*f, d - 1);
        return "true";
    }

    const FSig* findFunc(Type ret) {
        std::vector<const FSig*> c;
        for (auto& f : funcs_) if (f.ret == ret) c.push_back(&f);
        if (c.empty()) return nullptr;
        return c[pick(0, (int)c.size() - 1)];
    }
};

} // namespace

std::string generateRandomProgram(uint64_t seed) { return Gen(seed * 2654435761ULL + 12345).program(); }

std::string minimizeProgram(const std::string& source, const std::function<bool(const std::string&)>& stillFails) {
    std::vector<std::string> lines;
    { std::istringstream in(source); std::string l; while (std::getline(in, l)) lines.push_back(l); }
    auto join = [](const std::vector<std::string>& v) { std::string r; for (auto& l : v) r += l + "\n"; return r; };
    if (!stillFails(join(lines))) return source;
    size_t chunk = lines.size() / 2;
    while (chunk >= 1) {
        bool reduced = false;
        for (size_t start = 0; start < lines.size();) {
            std::vector<std::string> cand;
            for (size_t i = 0; i < lines.size(); ++i) if (i < start || i >= start + chunk) cand.push_back(lines[i]);
            if (!cand.empty() && stillFails(join(cand))) { lines = cand; reduced = true; }
            else start += chunk;
        }
        if (!reduced) { if (chunk == 1) break; chunk /= 2; }
    }
    return join(lines);
}

static void writeFile(const std::string& path, const std::string& content) { std::ofstream(path) << content; }

Json runFuzz(int count, uint64_t seed, const OptConfig& cfg, const std::string& saveDir) {
    int compiled = 0, inconclusive = 0, passed = 0, failed = 0, genFailures = 0, runtimeErrors = 0;
    int64_t irBefore = 0, irAfter = 0, stepsBefore = 0, stepsAfter = 0;
    Json failures = Json::array();
    for (int i = 0; i < count; ++i) {
        uint64_t s = seed + static_cast<uint64_t>(i);
        std::string src = generateRandomProgram(s);
        DiffOutcome d = differentialCheck(src, cfg);
        if (!d.compiled) {
            ++genFailures;
            if (failures.size() < 5) { Json f = Json::object(); f.set("seed", static_cast<int64_t>(s)).set("kind", "generator produced invalid program").set("detail", d.detail).set("source", src); failures.push(f); }
            continue;
        }
        ++compiled;
        if (d.inconclusive) { ++inconclusive; continue; }
        irBefore += d.irBefore; irAfter += d.irAfter; stepsBefore += d.stepsBefore; stepsAfter += d.stepsAfter;
        if (d.match) ++passed;
        else {
            ++failed;
            if (!saveDir.empty()) {
                std::string min = minimizeProgram(src, [&](const std::string& c) { DiffOutcome o = differentialCheck(c, cfg); return o.compiled && !o.inconclusive && !o.match; });
                writeFile(saveDir + "/seed-" + std::to_string(s) + ".nova", src);
                writeFile(saveDir + "/seed-" + std::to_string(s) + ".min.nova", min);
            }
            if (failures.size() < 5) { Json f = Json::object(); f.set("seed", static_cast<int64_t>(s)).set("kind", "semantic mismatch").set("detail", d.detail).set("source", src); failures.push(f); }
        }
    }
    (void)runtimeErrors;
    Json j = Json::object();
    j.set("requested", count).set("seed", static_cast<int64_t>(seed)).set("compiled", compiled).set("passed", passed).set("failed", failed)
     .set("inconclusive", inconclusive).set("generatorFailures", genFailures)
     .set("irInstructionsBefore", irBefore).set("irInstructionsAfter", irAfter)
     .set("vmStepsBefore", stepsBefore).set("vmStepsAfter", stepsAfter).set("failures", failures)
     .set("config", cfg.toJson());
    return j;
}

} // namespace nova

namespace nova {

std::string mutateProgram(const std::string& source, uint64_t seed) {
    std::mt19937_64 rng(seed * 0x9E3779B97F4A7C15ull + 7);
    auto pick = [&](size_t n) { return static_cast<size_t>(rng() % (n ? n : 1)); };
    std::string s = source;
    std::vector<std::string> lines;
    { std::istringstream in(s); std::string l; while (std::getline(in, l)) lines.push_back(l); }
    int rounds = 1 + static_cast<int>(pick(2));
    for (int r = 0; r < rounds && !s.empty(); ++r) {
        switch (pick(8)) {
            case 0: s.erase(pick(s.size()), 1); break;                                           // delete a character
            case 1: s.insert(pick(s.size() + 1), 1, "(){};,+-*/=<>!&|:\"@#."[pick(19)]); break;   // insert junk
            case 2: { std::vector<std::string> l; std::istringstream in(s); std::string x; while (std::getline(in, x)) l.push_back(x);
                      if (l.size() > 1) l.erase(l.begin() + pick(l.size())); s.clear(); for (auto& x2 : l) s += x2 + "\n"; break; }   // delete a line
            case 3: { std::vector<std::string> l; std::istringstream in(s); std::string x; while (std::getline(in, x)) l.push_back(x);
                      if (!l.empty()) { size_t i = pick(l.size()); l.insert(l.begin() + i, l[i]); } s.clear(); for (auto& x2 : l) s += x2 + "\n"; break; }   // duplicate a line
            case 4: { static const char* kw[] = {"int", "float", "bool", "let", "fn", "while", "if", "else", "return", "print", "true", "false"};
                      size_t p = pick(s.size()); s.insert(p, std::string(" ") + kw[pick(12)] + " "); break; }                                                // insert a keyword
            case 5: { size_t p = s.find_first_of("0123456789", pick(s.size())); if (p != std::string::npos) s[p] = static_cast<char>('0' + pick(10)); break; } // change a digit
            case 6: { size_t a = pick(s.size()), b = pick(s.size()); std::swap(s[a], s[b]); break; }                                                         // swap characters
            case 7: { size_t p = pick(s.size()); size_t len = 1 + pick(12); s.erase(p, len); break; }                                                          // delete a span
        }
    }
    return s;
}

Json runInvalidFuzz(int count, uint64_t seed, const OptConfig& cfg) {
    int rejectedLex = 0, rejectedParse = 0, rejectedSem = 0, accepted = 0, acceptedMismatch = 0, badDiag = 0, crashes = 0, inconclusive = 0;
    Json problems = Json::array();
    auto note = [&](uint64_t s, const std::string& what, const std::string& src) {
        if (problems.size() < 5) { Json o = Json::object(); o.set("seed", static_cast<int64_t>(s)).set("problem", what).set("source", src); problems.push(o); }
    };
    for (int i = 0; i < count; ++i) {
        uint64_t s = seed + static_cast<uint64_t>(i);
        std::string src = mutateProgram(generateRandomProgram(s), s);
        try {
            CompileOptions co; co.opt = cfg; co.run = false;
            auto r = compileSource(src, co);
            if (r->success) {
                ++accepted;
                DiffOutcome d = differentialCheck(src, cfg);
                if (d.inconclusive) ++inconclusive;
                else if (!d.match) { ++acceptedMismatch; note(s, "accepted mutant behaves differently: " + d.detail, src); }
                continue;
            }
            if (r->failedStage == "lexer") ++rejectedLex; else if (r->failedStage == "parser") ++rejectedParse; else if (r->failedStage == "semantic") ++rejectedSem;
            bool anyError = false, ok = true;
            for (auto& d : r->diagnostics.items()) {
                if (d.severity != Severity::Error) continue;
                anyError = true;
                if (d.message.empty() || d.category.empty() || d.code.empty() || d.loc.line < 1 || d.loc.col < 1) ok = false;
            }
            if (!anyError || !ok) { ++badDiag; note(s, anyError ? "malformed diagnostic" : "failure without an error diagnostic", src); }
            if (r->failedStage == "codegen") { ++badDiag; note(s, "internal compiler error after successful semantic analysis", src); }
        } catch (const std::exception& e) { ++crashes; note(s, std::string("exception: ") + e.what(), src); }
    }
    Json j = Json::object();
    j.set("requested", count).set("seed", static_cast<int64_t>(seed)).set("rejectedLexical", rejectedLex).set("rejectedSyntax", rejectedParse)
     .set("rejectedSemantic", rejectedSem).set("acceptedMutants", accepted).set("acceptedMismatches", acceptedMismatch)
     .set("inconclusive", inconclusive).set("malformedDiagnostics", badDiag).set("crashes", crashes).set("problems", problems);
    return j;
}

} // namespace nova
