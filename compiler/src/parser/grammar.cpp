#include "nova/grammar.hpp"
#include <sstream>
#include <stdexcept>

namespace nova {

const char* novaGrammarText() {
    return R"G(
Program -> FuncList
FuncList -> FuncList Func | Func
Func -> fn ID ( ParamsOpt ) RetOpt Block
ParamsOpt -> Params | eps
Params -> Params , Param | Param
Param -> ID : Type
RetOpt -> -> Type | eps
Type -> ElemType | ElemType [ ] | string
ElemType -> int | float | bool
Block -> { StmtList }
StmtList -> StmtList Stmt | eps
Stmt -> VarDecl | Assign | IfStmt | WhileStmt | ReturnStmt | PrintStmt | CallStmt | Block | error ; | error
VarDecl -> let ID : Type = Expr ;
Assign -> ID = Expr ; | ID [ Expr ] = Expr ;
IfStmt -> if ( Expr ) Block ElsePart
ElsePart -> else Block | else IfStmt | eps
WhileStmt -> while ( Expr ) Block
ReturnStmt -> return ExprOpt ;
ExprOpt -> Expr | eps
PrintStmt -> print ( Expr ) ;
CallStmt -> Call ;
Expr -> OrExpr
OrExpr -> OrExpr || AndExpr | AndExpr
AndExpr -> AndExpr && EqExpr | EqExpr
EqExpr -> EqExpr == RelExpr | EqExpr != RelExpr | RelExpr
RelExpr -> RelExpr < AddExpr | RelExpr <= AddExpr | RelExpr > AddExpr | RelExpr >= AddExpr | AddExpr
AddExpr -> AddExpr + MulExpr | AddExpr - MulExpr | MulExpr
MulExpr -> MulExpr * UnaryExpr | MulExpr / UnaryExpr | MulExpr % UnaryExpr | UnaryExpr
UnaryExpr -> - UnaryExpr | ! UnaryExpr | Postfix
Postfix -> Postfix [ Expr ] | Primary
Primary -> INT_LIT | FLOAT_LIT | STR_LIT | true | false | ID | Call | ( Expr ) | [ Args ] | new ElemType [ Expr ]
Call -> ID ( ArgsOpt )
ArgsOpt -> Args | eps
Args -> Args , Expr | Expr
)G";
}

int Grammar::intern(const std::string& n, bool isTerm) {
    auto it = ids.find(n);
    if (it != ids.end()) return it->second;
    int id = static_cast<int>(names.size());
    names.push_back(n); terminal.push_back(isTerm); ids[n] = id;
    return id;
}

Grammar Grammar::nova() { return fromText(novaGrammarText()); }

Grammar Grammar::fromText(const std::string& text) {
    Grammar g;
    // Pass 1: collect nonterminal names (everything on a left-hand side).
    struct Rule { std::string lhs; std::vector<std::vector<std::string>> alts; };
    std::vector<Rule> rules;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        size_t a = line.find(" -> ");
        if (a == std::string::npos) continue;
        Rule r; r.lhs = line.substr(0, a);
        std::istringstream rhs(line.substr(a + 4));
        std::string tok; std::vector<std::string> cur;
        while (rhs >> tok) {
            if (tok == "|") { r.alts.push_back(cur); cur.clear(); } else cur.push_back(tok);
        }
        r.alts.push_back(cur);
        rules.push_back(std::move(r));
    }
    std::set<std::string> nts;
    for (auto& r : rules) nts.insert(r.lhs);
    g.eof = g.intern("$", true);
    g.augStart = g.intern(rules[0].lhs + "'", false);
    // Intern nonterminals in definition order, so output is stable and readable.
    for (auto& r : rules) g.intern(r.lhs, false);
    g.prods.push_back({g.augStart, {g.intern(rules[0].lhs, false)}});
    for (auto& r : rules) {
        for (auto& alt : r.alts) {
            Production p; p.lhs = g.intern(r.lhs, false);
            for (auto& s : alt) {
                if (s == "eps") continue;
                p.rhs.push_back(g.intern(s, nts.count(s) == 0));
            }
            g.prods.push_back(std::move(p));
        }
    }
    if (g.names.size() > 250 || g.prods.size() > 4000) throw std::runtime_error("grammar too large for item packing");
    g.computeSets();
    return g;
}

void Grammar::computeSets() {
    size_t n = names.size();
    nullable.assign(n, false);
    first.assign(n, {});
    follow.assign(n, {});
    for (size_t s = 0; s < n; ++s) if (terminal[s]) first[s].insert(static_cast<int>(s));
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& p : prods) {
            bool allNull = true;
            for (int s : p.rhs) {
                size_t before = first[p.lhs].size();
                first[p.lhs].insert(first[s].begin(), first[s].end());
                if (first[p.lhs].size() != before) changed = true;
                if (!nullable[s]) { allNull = false; break; }
            }
            if (allNull && !nullable[p.lhs]) { nullable[p.lhs] = true; changed = true; }
        }
    }
    follow[augStart].insert(eof);
    changed = true;
    while (changed) {
        changed = false;
        for (auto& p : prods) {
            for (size_t i = 0; i < p.rhs.size(); ++i) {
                int b = p.rhs[i];
                if (terminal[b]) continue;
                size_t before = follow[b].size();
                bool restNull = true;
                for (size_t j = i + 1; j < p.rhs.size(); ++j) {
                    follow[b].insert(first[p.rhs[j]].begin(), first[p.rhs[j]].end());
                    if (!nullable[p.rhs[j]]) { restNull = false; break; }
                }
                if (restNull) follow[b].insert(follow[p.lhs].begin(), follow[p.lhs].end());
                if (follow[b].size() != before) changed = true;
            }
        }
    }
}

std::set<int> Grammar::firstOfSeq(const std::vector<int>& seq, size_t from, int la) const {
    std::set<int> out;
    for (size_t i = from; i < seq.size(); ++i) {
        out.insert(first[seq[i]].begin(), first[seq[i]].end());
        if (!nullable[seq[i]]) return out;
    }
    out.insert(la);
    return out;
}

std::string Grammar::prodText(int p, int dot) const {
    std::string s = names[prods[p].lhs] + " ->";
    const auto& rhs = prods[p].rhs;
    for (size_t i = 0; i <= rhs.size(); ++i) {
        if (static_cast<int>(i) == dot) s += " •";
        if (i < rhs.size()) s += " " + names[rhs[i]];
    }
    if (rhs.empty() && dot < 0) s += " ε";
    return s;
}

Json Grammar::toJson() const {
    Json j = Json::object();
    Json ps = Json::array();
    for (size_t i = 0; i < prods.size(); ++i) {
        Json p = Json::object();
        p.set("id", static_cast<int>(i)).set("lhs", names[prods[i].lhs]).set("text", prodText(static_cast<int>(i)));
        Json rhs = Json::array();
        for (int s : prods[i].rhs) rhs.push(names[s]);
        p.set("rhs", rhs);
        ps.push(p);
    }
    j.set("productions", ps);
    Json terms = Json::array(), nts = Json::array();
    for (size_t s = 0; s < names.size(); ++s) (terminal[s] ? terms : nts).push(names[s]);
    j.set("terminals", terms).set("nonterminals", nts);
    Json f1 = Json::object(), f2 = Json::object();
    for (size_t s = 0; s < names.size(); ++s) {
        if (terminal[s]) continue;
        Json a = Json::array(), b = Json::array();
        for (int t : first[s]) a.push(names[t]);
        if (nullable[s]) a.push("ε");
        for (int t : follow[s]) b.push(names[t]);
        f1.set(names[s], a); f2.set(names[s], b);
    }
    j.set("first", f1).set("follow", f2);
    return j;
}

} // namespace nova
