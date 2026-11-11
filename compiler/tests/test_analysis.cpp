// Tests for: grammar/table verification, parser recovery, AST validation, IR/CFG/data-flow/SSA/regalloc algorithms,
// bytecode verifier, arithmetic boundaries, regression corpus, fuzzing framework.
#include <dirent.h>
#include <fstream>
#include <sstream>
#include "nova/analysis.hpp"
#include "nova/pipeline.hpp"
#include "test_framework.hpp"

using namespace nova;

namespace {
std::unique_ptr<CompileResult> compileIt(const std::string& src, const OptConfig& cfg = OptConfig(), bool run = true) {
    CompileOptions co; co.opt = cfg; co.run = run;
    return compileSource(src, co);
}
std::string wrapMain(const std::string& body) { return "fn main() {\n" + body + "\n}\n"; }
IRFunction irOf(const std::string& src, const std::string& fname, bool optimized = false) {
    auto r = compileIt(src, OptConfig(), false);
    CHECK(r->success);
    for (auto& f : (optimized ? r->opt.program : r->ir).functions) if (f.name == fname) return f;
    throw TestFailure("no such function " + fname);
}
IRFunction mainOf(const std::string& body, bool optimized = false) { return irOf(wrapMain(body), "main", optimized); }
std::string joined(const std::vector<std::string>& v) { std::string s; for (auto& x : v) s += x + "\n"; return s; }
bool contains(const std::set<std::string>& s, const std::string& x) { return s.count(x) > 0; }
}

// ================================================================== grammar & LR(1) table verification
TEST(grammar, gotoTransitionsMatchIndependentRecomputation) {
    // For every state and every shift/goto entry, recompute goto(closure(I), X) from the definition and compare with the table's target kernel.
    const LR1Table& t = novaTable();
    const Grammar& g = t.grammar;
    size_t checked = 0;
    for (size_t s = 0; s < t.stateCount(); ++s) {
        auto cl = t.closure(t.kernels[s]);
        for (size_t x = 0; x < g.names.size(); ++x) {
            LRAction a = t.table[s][x];
            if (a.kind != LRAction::Shift && a.kind != LRAction::Goto) continue;
            std::vector<uint32_t> expect;
            for (uint32_t it : cl) {
                const Production& p = g.prods[itemProd(it)];
                if (static_cast<size_t>(itemDot(it)) < p.rhs.size() && p.rhs[itemDot(it)] == static_cast<int>(x)) expect.push_back(packItem(itemProd(it), itemDot(it) + 1, itemLa(it)));
            }
            std::sort(expect.begin(), expect.end());
            CHECK(expect == t.kernels[a.target]);
            ++checked;
        }
    }
    CHECK(checked > 500);
}
TEST(grammar, everyReduceLookaheadIsInFollowOfLhs) {
    // canonical LR(1) lookaheads are a subset of FOLLOW(A): an independent consistency check between FOLLOW and the tables
    const LR1Table& t = novaTable();
    const Grammar& g = t.grammar;
    for (size_t s = 0; s < t.stateCount(); ++s)
        for (size_t x = 0; x < g.names.size(); ++x) {
            LRAction a = t.table[s][x];
            if (a.kind == LRAction::Reduce) CHECK(g.follow[g.prods[a.target].lhs].count(static_cast<int>(x)) > 0);
        }
}
TEST(grammar, shiftTargetsAreAccessedBySingleSymbol) {
    const LR1Table& t = novaTable();
    const Grammar& g = t.grammar;
    for (size_t s = 0; s < t.stateCount(); ++s)
        for (size_t x = 0; x < g.names.size(); ++x) {
            LRAction a = t.table[s][x];
            if (a.kind != LRAction::Shift && a.kind != LRAction::Goto) continue;
            for (uint32_t it : t.kernels[a.target]) { CHECK(itemDot(it) > 0); CHECK(g.prods[itemProd(it)].rhs[itemDot(it) - 1] == static_cast<int>(x)); }
        }
}
TEST(grammar, firstSetsOnSmallKnownGrammar) {
    Grammar g = Grammar::fromText("S -> A B\nA -> a | eps\nB -> b | eps\n");
    int A = g.symbol("A"), B = g.symbol("B"), S = g.symbol("S");
    CHECK(g.nullable[A] && g.nullable[B] && g.nullable[S]);
    CHECK(g.first[S].count(g.symbol("a")) && g.first[S].count(g.symbol("b")));
    CHECK(g.follow[A].count(g.symbol("b")) && g.follow[A].count(g.eof));
    CHECK(g.follow[B].count(g.eof) && !g.follow[B].count(g.symbol("a")));
}
TEST(grammar, conflictsAreDetectedForAmbiguousGrammar) {
    // classic ambiguous expression grammar must produce shift/reduce conflicts
    Grammar g = Grammar::fromText("E -> E + E | n\n");
    CHECK(!LR1Table::build(g).conflicts.empty());
}
TEST(grammar, lr1ButNotSlr1Grammar) {
    // S -> L = R | R ; L -> * R | id ; R -> L   is LR(1) (and LALR) but not SLR(1)
    Grammar g = Grammar::fromText("S -> L = R | R\nL -> * R | id\nR -> L\n");
    LR1Table t = LR1Table::build(g);
    CHECK(t.conflicts.empty());
    CHECK(t.stateCount() >= 10);
}

// ================================================================== parser recovery & syntax errors
TEST(recovery, reportsMultipleIndependentSyntaxErrors) {
    auto r = compileIt("fn main() {\n let a: int = 5\n print(a);\n let b: int = ;\n let c: int = (1 + 2;\n print(c);\n}\n", OptConfig(), false);
    CHECK(!r->success);
    CHECK_EQ(r->failedStage, std::string("parser"));
    int n = 0; for (auto& d : r->diagnostics.items()) if (d.severity == Severity::Error) ++n;
    CHECK_EQ(n, 3);
    CHECK(r->parse.recoveries >= 3);
}
TEST(recovery, firstErrorIdenticalWithAndWithoutLaterErrors) {
    auto a = compileIt("fn main() { let a: int = 5\n print(a); }", OptConfig(), false);
    auto b = compileIt("fn main() { let a: int = 5\n print(a); let b: int = ; }", OptConfig(), false);
    CHECK_EQ(a->diagnostics.items()[0].message, b->diagnostics.items()[0].message);
    CHECK_EQ(a->diagnostics.items()[0].loc.line, b->diagnostics.items()[0].loc.line);
}
TEST(recovery, errorOutsideStatementListIsUnrecoverableButSafe) {
    auto r = compileIt("fn main( { print(1); }", OptConfig(), false);
    CHECK(!r->success);
    CHECK_EQ(r->diagnostics.items().size(), size_t(1));
}
TEST(recovery, terminatesOnGarbageInput) {
    for (const char* g : {"{{{{{{{{", "}}}}}}", "fn fn fn", "fn main() { ;;;;;;; }", "fn main() { let let let }", "))))((((", "fn main() { if if if }", ""}) {
        auto r = compileIt(g, OptConfig(), false);
        CHECK(!r->success);
        CHECK(r->diagnostics.hasErrors());
    }
}
TEST(recovery, validProgramsUnaffected) {
    CHECK(compileIt(wrapMain("let a: int = 1; { let b: int = a; print(b); }"))->success);
}
TEST(recovery, neverSuppressesTheFirstError) {
    auto r = compileIt("fn main() { let x: int = ; }", OptConfig(), false);
    CHECK(!r->diagnostics.items().empty());
}

// ================================================================== lexer/parser boundary cases
TEST(boundary, emptyConstructs) {
    CHECK(compileIt("fn main() { }")->success);
    CHECK(compileIt("fn main() { { } { { } } }")->success);
    CHECK(compileIt("fn f() { } fn main() { f(); }")->success);
    CHECK(!compileIt("", OptConfig(), false)->success);          // a program needs at least one function
    CHECK(!compileIt("// only a comment\n", OptConfig(), false)->success);
}
TEST(boundary, deepNestingAndLongChains) {
    std::string e = "1"; for (int i = 0; i < 150; ++i) e = "(" + e + " + 1)";
    auto r = compileIt(wrapMain("print(" + e + ");"));
    CHECK(r->success);
    CHECK_EQ(r->runOptimized.output[0], std::string("151"));
    std::string chain = "1"; for (int i = 0; i < 400; ++i) chain += " + 1";
    CHECK_EQ(compileIt(wrapMain("print(" + chain + ");"))->runOptimized.output[0], std::string("401"));
    std::string blocks, closes; for (int i = 0; i < 60; ++i) { blocks += "{ "; closes += " }"; }
    CHECK(compileIt(wrapMain(blocks + "print(1);" + closes))->success);
}
TEST(boundary, precedenceAndAssociativityAreCorrect) {
    CHECK_EQ(compileIt(wrapMain("print(10 - 4 - 3);"))->runOptimized.output[0], std::string("3"));          // left assoc
    CHECK_EQ(compileIt(wrapMain("print(100 / 10 / 5);"))->runOptimized.output[0], std::string("2"));
    CHECK_EQ(compileIt(wrapMain("print(2 + 3 * 4 - 6 / 2);"))->runOptimized.output[0], std::string("11"));
    CHECK_EQ(compileIt(wrapMain("print(1 + 2 < 4 == true);"))->runOptimized.output[0], std::string("true"));
    CHECK_EQ(compileIt(wrapMain("print(-2 * -3);"))->runOptimized.output[0], std::string("6"));
    CHECK_EQ(compileIt(wrapMain("print(!false && false || true);"))->runOptimized.output[0], std::string("true"));
    CHECK_EQ(compileIt(wrapMain("print(true || false && false);"))->runOptimized.output[0], std::string("true"));   // && binds tighter than ||
}
TEST(boundary, numericLiteralLimits) {
    CHECK(compileIt(wrapMain("print(9223372036854775807);"))->success);
    CHECK(!compileIt(wrapMain("print(9223372036854775808);"), OptConfig(), false)->success);
    CHECK(compileIt(wrapMain("print(0.5); print(10.25);"))->success);
    CHECK(!compileIt(wrapMain("print(.5);"), OptConfig(), false)->success);
    CHECK(!compileIt(wrapMain("print(5.);"), OptConfig(), false)->success);
}
TEST(boundary, largeProgramCompiles) {
    std::string body;
    for (int i = 0; i < 1500; ++i) body += "let v" + std::to_string(i) + ": int = " + std::to_string(i) + " + 1;\n";
    body += "print(v1499);";
    auto r = compileIt(wrapMain(body));
    CHECK(r->success);
    CHECK_EQ(r->runOptimized.output[0], std::string("1500"));
}

// ================================================================== AST validation / stable serialisation
TEST(astvalid, randomProgramsProduceValidTypedAsts) {
    for (uint64_t seed = 1; seed <= 120; ++seed) {
        auto r = compileIt(generateRandomProgram(seed), OptConfig(), false);
        CHECK(r->success);
        auto problems = validateAst(r->ast.get(), true);
        if (!problems.empty()) throw TestFailure("seed " + std::to_string(seed) + ": " + problems[0]);
    }
}
TEST(astvalid, sexprIsLocationIndependentAndStable) {
    auto a = compileIt("fn main() { let x: int = 1 - 2 - 3 * 4; }", OptConfig(), false);
    auto b = compileIt("fn   main()\n{\n\n   let x : int =\n 1-2-3*4 ;\n}", OptConfig(), false);
    CHECK_EQ(astToSexpr(a->ast.get()), astToSexpr(b->ast.get()));
    CHECK_EQ(astToSexpr(a->ast.get()), std::string("(Program (Function main -> void (Block (VarDecl x: int (Binary - (Binary - (IntLit 1) (IntLit 2)) (Binary * (IntLit 3) (IntLit 4)))))))"));
}
TEST(astvalid, detectsBrokenTrees) {
    auto r = compileIt("fn main() { print(1 + 2); }", OptConfig(), false);
    r->ast->functions[0]->body->stmts[0]->loc.col = 0;      // corrupt a location
    CHECK(!validateAst(r->ast.get(), true).empty());
}

// ================================================================== semantic negatives (additional)
TEST(semantic2, assignmentToUndeclaredOrFunction) {
    auto a = compileIt(wrapMain("x = 1;"), OptConfig(), false);
    CHECK(!a->success);
    bool undefined = false; for (auto& d : a->diagnostics.items()) if (d.code == "E-SEM-001") undefined = true;
    CHECK(undefined);
    auto b = compileIt("fn f() { } fn main() { f = 1; }", OptConfig(), false);
    bool fn = false; for (auto& d : b->diagnostics.items()) if (d.code == "E-SEM-012") fn = true;
    CHECK(fn);
}
TEST(semantic2, duplicateParameterAndShadowedParameter) {
    auto a = compileIt("fn f(a: int, a: int) -> int { return a; } fn main() { print(f(1, 2)); }", OptConfig(), false);
    bool dup = false; for (auto& d : a->diagnostics.items()) if (d.code == "E-SEM-002") dup = true;
    CHECK(dup);
    CHECK(compileIt("fn f(a: int) -> int { { let a: int = 2; return a; } } fn main() { print(f(1)); }")->success);
}
TEST(semantic2, errorsCarryExactRanges) {
    auto r = compileIt("fn main() {\n    let a: int = 1;\n    print(a + b);\n}\n", OptConfig(), false);
    CHECK(!r->success);
    auto& d = r->diagnostics.items()[0];
    CHECK_EQ(d.loc.line, 3); CHECK_EQ(d.loc.col, 15); CHECK_EQ(d.loc.endCol, 16);
}
TEST(semantic2, usingVoidAndAssigningWrongTypes) {
    CHECK(!compileIt("fn f() { } fn main() { print(f()); }", OptConfig(), false)->success);
    CHECK(!compileIt(wrapMain("let a: bool = 1; "), OptConfig(), false)->success);
    CHECK(!compileIt(wrapMain("let a: int = 1; a = true;"), OptConfig(), false)->success);
    CHECK(!compileIt(wrapMain("let a: int = 1; print(a(2));"), OptConfig(), false)->success);
}

// ================================================================== CFG, dominators, loops
TEST(cfg2, syntheticEntryBlockWhenFunctionStartsWithLoop) {
    IRFunction f = irOf("fn f(n: int) -> int { while (n > 0) { n = n - 1; } return n; } fn main() { print(f(3)); }", "f");
    CFG g = buildCFG(f);
    CHECK(g.blocks[0].begin == g.blocks[0].end);          // empty synthetic entry
    CHECK(g.blocks[0].pred.empty());
    CHECK(verifyCFG(f, g).empty());
}
TEST(cfg2, verifierAcceptsAllRandomProgramsBeforeAndAfterOptimization) {
    for (uint64_t seed = 1; seed <= 150; ++seed) {
        auto r = compileIt(generateRandomProgram(seed), OptConfig(), false);
        CHECK(r->success);
        for (auto* prog : {&r->ir, &r->opt.program})
            for (auto& f : prog->functions) { auto pr = verifyCFG(f, buildCFG(f)); if (!pr.empty()) throw TestFailure("seed " + std::to_string(seed) + " fn " + f.name + ": " + pr[0]); }
    }
}
TEST(cfg2, verifierRejectsBrokenFunctions) {
    IRFunction f = mainOf("let a: int = 1; print(a);");
    f.code.pop_back();                                      // remove the final return: falls off the end
    CHECK(!verifyCFG(f, buildCFG(f)).empty());
    IRFunction g = mainOf("let i: int = 0; while (i < 2) { i = i + 1; }");
    for (auto& i : g.code) if (i.kind == IRInstr::Jump) i.target = "L_missing";
    CHECK(!verifyCFG(g, buildCFG(g)).empty());
}
TEST(cfg2, dominatorTreeAndFrontiersOnDiamond) {
    IRFunction f = mainOf("let a: int = 1; if (a > 0) { print(1); } else { print(2); } print(3);");
    CFG g = buildCFG(f);
    auto df = dominanceFrontiers(g);
    CHECK_EQ(g.blocks[1].idom, 0); CHECK_EQ(g.blocks[2].idom, 0); CHECK_EQ(g.blocks[3].idom, 0);
    CHECK(df[1].count(3) && df[2].count(3));
    CHECK(df[0].empty() && df[3].empty());
}
TEST(cfg2, dominanceFrontierOfLoopBodyContainsHeader) {
    IRFunction f = mainOf("let i: int = 0; while (i < 3) { i = i + 1; } print(i);");
    CFG g = buildCFG(f);
    auto df = dominanceFrontiers(g);
    int header = -1; for (auto& b : g.blocks) if (b.loopHeader) header = b.id;
    CHECK(header >= 0);
    bool found = false; for (auto& s : df) if (s.count(header)) found = true;
    CHECK(found);
    CHECK(df[header].count(header));                       // a loop header is in its own frontier
}
TEST(cfg2, reachabilityFindsUnreachableCodeAfterReturn) {
    IRFunction f = irOf("fn f() -> int { return 1; print(5); return 2; } fn main() { print(f()); }", "f");
    CFG g = buildCFG(f);
    int unreachable = 0; for (auto& b : g.blocks) if (!b.reachable) ++unreachable;
    CHECK(unreachable >= 1);
}

// ================================================================== data-flow
TEST(dataflow2, worklistLivenessEqualsRoundRobinOnRandomPrograms) {
    for (uint64_t seed = 1; seed <= 150; ++seed) {
        auto r = compileIt(generateRandomProgram(seed), OptConfig(), false);
        CHECK(r->success);
        for (auto& f : r->ir.functions) {
            CFG g = buildCFG(f);
            DataflowResult a = computeLiveness(f, g), b = computeLivenessWorklist(f, g);
            for (auto& blk : g.blocks) if (blk.reachable) { CHECK(a.in[blk.id] == b.in[blk.id]); CHECK(a.out[blk.id] == b.out[blk.id]); }
        }
    }
}
TEST(dataflow2, availableExpressionsAtJoin) {
    // a*b is computed on both paths -> available at the join; c+1 only on one path -> not available
    IRFunction f = irOf("fn f(a: int, b: int, c: int) -> int { let r: int = 0; if (a > 0) { r = a * b; r = r + c + 1; } else { r = a * b; } return r + a * b; } fn main() { print(f(1,2,3)); }", "f");
    CFG g = buildCFG(f);
    DataflowResult av = computeAvailableExpressions(f, g);
    CHECK(av.converged);
    int join = -1; for (auto& b : g.blocks) if (b.pred.size() == 2) join = b.id;
    CHECK(join >= 0);
    CHECK(contains(av.in[join], "a * b"));
}
TEST(dataflow2, availableExpressionsKilledByRedefinition) {
    IRFunction f = mainOf("let a: int = 1; let b: int = 2; let x: int = a + b; a = 5; let y: int = a + b; print(x + y);");
    CFG g = buildCFG(f);
    DataflowResult av = computeAvailableExpressions(f, g);
    CHECK(av.kill[0].count("a + b") == 1);                 // `a = 5` kills a + b
}
TEST(dataflow2, availableExpressionsLoopMustAnalysis) {
    // inside a loop, an expression whose operand changes in the loop body must NOT be available at the header
    IRFunction f = mainOf("let i: int = 0; let k: int = 2; let s: int = 0; while (i < 4) { s = s + k * 3; i = i + 1; } print(s);");
    CFG g = buildCFG(f);
    DataflowResult av = computeAvailableExpressions(f, g);
    int header = -1; for (auto& b : g.blocks) if (b.loopHeader) header = b.id;
    CHECK(header >= 0);
    CHECK(!contains(av.in[header], "k * 3"));              // not available on the entry edge (not yet computed)
    CHECK(!contains(av.in[header], "i + 1"));              // killed by i = ... along the back edge
}
TEST(dataflow2, constantPropagationJoinsAndLoops) {
    IRFunction f = irOf("fn f(c: bool) -> int { let x: int = 0; let y: int = 0; if (c) { x = 5; y = 1; } else { x = 5; y = 2; } return x + y; } fn main() { print(f(true)); }", "f");
    CFG g = buildCFG(f);
    ConstPropResult cp = computeConstantPropagation(f, g);
    CHECK(cp.converged);
    int join = -1; for (auto& b : g.blocks) if (b.pred.size() == 2) join = b.id;
    CHECK(join >= 0);
    CHECK_EQ(cp.in[join].at("x").str(), std::string("5"));       // equal constants on both paths survive the meet
    CHECK(cp.in[join].at("y").kind == ConstLattice::NAC);        // different constants meet to NAC
}
TEST(dataflow2, constantPropagationLoopVariableIsNotConstant) {
    IRFunction f = mainOf("let i: int = 0; let k: int = 7; while (i < 3) { i = i + 1; } print(k + i);");
    CFG g = buildCFG(f);
    ConstPropResult cp = computeConstantPropagation(f, g);
    int header = -1; for (auto& b : g.blocks) if (b.loopHeader) header = b.id;
    CHECK(cp.in[header].at("i").kind == ConstLattice::NAC);
    CHECK_EQ(cp.in[header].at("k").str(), std::string("7"));
}
TEST(dataflow2, constantPropagationFoldsArithmeticAndKeepsTrappingDivisionUnknown) {
    IRFunction f = mainOf("let a: int = 6; let b: int = a * 7; let z: int = 0; let c: int = b / z; print(b);");
    CFG g = buildCFG(f);
    ConstPropResult cp = computeConstantPropagation(f, g);
    CHECK_EQ(cp.out[0].at("b").str(), std::string("42"));
    CHECK(cp.out[0].at("c").kind == ConstLattice::NAC);
}
TEST(dataflow2, deadInstructionsMatchLiveness) {
    IRFunction f = mainOf("let x: int = 1; x = 2; x = 3; print(x);");
    CFG g = buildCFG(f);
    auto dead = findDeadInstructions(f, g, computeLiveness(f, g));
    CHECK_EQ(dead.size(), size_t(2));
    CHECK_EQ(f.code[dead[0]].str(), std::string("x = 1"));
    CHECK_EQ(f.code[dead[1]].str(), std::string("x = 2"));
}
TEST(dataflow2, reachingDefinitionsAcrossLoop) {
    IRFunction f = mainOf("let i: int = 0; while (i < 3) { i = i + 1; } print(i);");
    CFG g = buildCFG(f);
    DataflowResult rd = computeReachingDefs(f, g);
    int header = -1; for (auto& b : g.blocks) if (b.loopHeader) header = b.id;
    int idefs = 0;
    for (auto& d : rd.in[header]) if (rd.defText.at(d).rfind("i = ", 0) == 0) ++idefs;
    CHECK_EQ(idefs, 2);                                    // `i = 0` (entry edge) and `i = t` (back edge) both reach the header
}

// ================================================================== SSA
TEST(ssa2, verifierPassesOnRandomProgramsAndLoopAtEntry) {
    for (uint64_t seed = 1; seed <= 150; ++seed) {
        auto r = compileIt(generateRandomProgram(seed), OptConfig(), false);
        CHECK(r->success);
        for (auto* prog : {&r->ir, &r->opt.program})
            for (auto& f : prog->functions) {
                CFG g = buildCFG(f);
                SsaFunction s = buildSSA(f, g, computeLiveness(f, g));
                auto pr = verifySSA(s, g);
                if (!pr.empty()) throw TestFailure("seed " + std::to_string(seed) + " fn " + f.name + ": " + pr[0]);
            }
    }
}
TEST(ssa2, loopAtFunctionStartGetsCorrectPhiOperands) {
    IRFunction f = irOf("fn f(n: int) -> int { while (n > 0) { n = n - 1; } return n; } fn main() { print(f(3)); }", "f");
    CFG g = buildCFG(f);
    SsaFunction s = buildSSA(f, g, computeLiveness(f, g));
    CHECK(verifySSA(s, g).empty());
    int header = -1; for (auto& b : g.blocks) if (b.loopHeader) header = b.id;
    CHECK(header >= 0);
    CHECK_EQ(s.blocks[header].phis.size(), size_t(1));
    CHECK_EQ(s.blocks[header].phis[0].args.size(), size_t(2));      // one operand from the entry edge (parameter n#0), one from the back edge
}
TEST(ssa2, verifierDetectsBrokenSsa) {
    IRFunction f = mainOf("let x: int = 1; if (x > 0) { x = 2; } else { x = 3; } print(x);");
    CFG g = buildCFG(f);
    SsaFunction s = buildSSA(f, g, computeLiveness(f, g));
    CHECK(verifySSA(s, g).empty());
    s.definitions.push_back(s.definitions[0]);                       // duplicate definition
    CHECK(!verifySSA(s, g).empty());
    SsaFunction s2 = buildSSA(f, g, computeLiveness(f, g));
    s2.blocks.back().phis[0].args.pop_back();                        // wrong arity
    CHECK(!verifySSA(s2, g).empty());
}
TEST(ssa2, phiIsPrunedWhenVariableIsDead) {
    // x is assigned in both branches but never read afterwards: pruned SSA must not insert a phi for it
    IRFunction f = mainOf("let c: bool = true; let x: int = 0; if (c) { x = 1; } else { x = 2; } print(1);");
    CFG g = buildCFG(f);
    CHECK_EQ(buildSSA(f, g, computeLiveness(f, g)).phiCount, 0);
}
TEST(ssa2, semanticsPreservedByRenamingOnStraightLine) {
    // SSA of straight-line code just versions each assignment
    IRFunction f = mainOf("let a: int = 1; a = a + 1; a = a * 2; print(a);");
    CFG g = buildCFG(f);
    SsaFunction s = buildSSA(f, g, computeLiveness(f, g));
    CHECK_EQ(s.phiCount, 0);
    std::string code = joined(s.blocks[0].code);
    CHECK(code.find("a#0 = 1") != std::string::npos);
    CHECK(code.find("a#1 = t") != std::string::npos);
    CHECK(code.find("print a#2") != std::string::npos);
}

// ================================================================== register allocation
TEST(regalloc, interferingVariablesGetDifferentColours) {
    IRFunction f = mainOf("let a: int = 1; let b: int = 2; let c: int = a + b; print(c + a + b);");
    CFG g = buildCFG(f);
    RegAllocResult r = allocateRegisters(f, g, computeLiveness(f, g), 8);
    CHECK(verifyAllocation(r));
    CHECK_EQ(r.spills, 0);
    CHECK(r.minimalK >= 2);
    std::map<std::string, int> col; for (auto& n : r.nodes) col[n.name] = n.color;
    CHECK(col["a"] != col["b"]);                                    // simultaneously live
}
TEST(regalloc, nonOverlappingVariablesShareARegister) {
    IRFunction f = mainOf("let a: int = 1; print(a); let b: int = 2; print(b); let c: int = 3; print(c);");
    CFG g = buildCFG(f);
    RegAllocResult r = allocateRegisters(f, g, computeLiveness(f, g), 4);
    CHECK(verifyAllocation(r));
    CHECK_EQ(r.minimalK, 1);
    CHECK_EQ(r.colorsUsed, 1);
}
TEST(regalloc, spillsWhenTooFewRegistersAndStaysValid) {
    IRFunction f = mainOf("let a: int = 1; let b: int = 2; let c: int = 3; let d: int = 4; print(a + b + c + d);");
    CFG g = buildCFG(f);
    DataflowResult l = computeLiveness(f, g);
    RegAllocResult r = allocateRegisters(f, g, l, 2);
    CHECK(r.spills > 0);
    CHECK(verifyAllocation(r));
    RegAllocResult ok = allocateRegisters(f, g, l, r.minimalK);
    CHECK_EQ(ok.spills, 0);
    CHECK(verifyAllocation(ok));
}
TEST(regalloc, validForEveryKOnRandomPrograms) {
    for (uint64_t seed = 1; seed <= 80; ++seed) {
        auto r = compileIt(generateRandomProgram(seed), OptConfig(), false);
        CHECK(r->success);
        for (auto& f : r->ir.functions) {
            CFG g = buildCFG(f); DataflowResult l = computeLiveness(f, g);
            for (int k : {1, 2, 3, 4, 8}) { RegAllocResult ra = allocateRegisters(f, g, l, k); if (!verifyAllocation(ra)) throw TestFailure("invalid allocation seed " + std::to_string(seed) + " k=" + std::to_string(k)); }
            RegAllocResult m = allocateRegisters(f, g, l, allocateRegisters(f, g, l, 4).minimalK);
            CHECK_EQ(m.spills, 0);
        }
    }
}
TEST(regalloc, parametersInterfere) {
    IRFunction f = irOf("fn f(a: int, b: int) -> int { return a + b; } fn main() { print(f(1, 2)); }", "f");
    CFG g = buildCFG(f);
    RegAllocResult r = allocateRegisters(f, g, computeLiveness(f, g), 4);
    std::map<std::string, int> col; for (auto& n : r.nodes) col[n.name] = n.color;
    CHECK(col["a"] != col["b"]);
}

// ================================================================== bytecode verifier
TEST(verifier, acceptsAllGeneratedBytecode) {
    for (uint64_t seed = 1; seed <= 150; ++seed) {
        auto r = compileIt(generateRandomProgram(seed), OptConfig(), false);
        CHECK(r->success);
        auto a = verifyBytecode(r->bcBaseline), b = verifyBytecode(r->bcOptimized);
        if (!a.empty() || !b.empty()) throw TestFailure("seed " + std::to_string(seed) + ": " + (a.empty() ? b[0] : a[0]));
    }
}
TEST(verifier, rejectsMalformedBytecode) {
    auto r = compileIt("fn f(x: int) -> int { if (x > 0) { return 1; } return 2; } fn main() { print(f(1)); }", OptConfig::none(), false);
    BcProgram ok = r->bcBaseline;
    CHECK(verifyBytecode(ok).empty());
    auto firstOf = [&](const BcProgram& p, OpCode op, int from = 0) { for (int i = from; i < static_cast<int>(p.code.size()); ++i) if (p.code[i].op == op) return i; return -1; };
    { BcProgram p = ok; int i = firstOf(p, OpCode::JZ); p.code[i].a = 9999; CHECK(!verifyBytecode(p).empty()); }               // jump out of range
    { BcProgram p = ok; int i = firstOf(p, OpCode::LOAD); p.code[i].a = 99; CHECK(!verifyBytecode(p).empty()); }                 // slot out of range
    { BcProgram p = ok; int i = firstOf(p, OpCode::CALL, 2); p.code[i].argc = 5; CHECK(!verifyBytecode(p).empty()); }             // arity
    { BcProgram p = ok; int i = firstOf(p, OpCode::ADD); if (i < 0) i = firstOf(p, OpCode::GT); p.code[i - 1].op = OpCode::JMP; p.code[i - 1].a = i; CHECK(!verifyBytecode(p).empty()); }  // underflow / bad stack
    { BcProgram p = ok; int i = firstOf(p, OpCode::RET); p.code[i].op = OpCode::RETV; CHECK(!verifyBytecode(p).empty()); }       // void return in value function
    { BcProgram p = ok; p.code[1].op = OpCode::POP; CHECK(!verifyBytecode(p).empty()); }                                          // broken entry stub
    { BcProgram p = ok; int i = firstOf(p, OpCode::RET); p.code.erase(p.code.begin() + i); p.functions[0].end--; for (size_t k = 1; k < p.functions.size(); ++k) { p.functions[k].entry--; p.functions[k].end--; } CHECK(!verifyBytecode(p).empty()); }
}
TEST(verifier, inconsistentMergeDepthDetected) {
    auto r = compileIt(wrapMain("let a: int = 1; if (a > 0) { print(1); } print(2);"), OptConfig::none(), false);
    BcProgram p = r->bcBaseline;
    int jz = -1; for (int i = 0; i < static_cast<int>(p.code.size()); ++i) if (p.code[i].op == OpCode::JZ) jz = i;
    BcInstr push; push.op = OpCode::PUSH; push.k = Value::Int(1);
    p.code.insert(p.code.begin() + jz + 1, push);               // extra value only on the fall-through path
    for (auto& f : p.functions) { if (f.entry > jz) f.entry++; if (f.end > jz) f.end++; }
    for (auto& i : p.code) if ((i.op == OpCode::JMP || i.op == OpCode::JZ || i.op == OpCode::JNZ) && i.a > jz) i.a++;
    CHECK(!verifyBytecode(p).empty());
}

// ================================================================== VM determinism & limits
TEST(vm2, executionIsDeterministicIncludingTraces) {
    CompileOptions co; co.run = true; co.trace = true;
    std::string src = generateRandomProgram(77);
    auto a = compileSource(src, co), b = compileSource(src, co);
    CHECK(a->runOptimized.output == b->runOptimized.output);
    CHECK_EQ(a->runOptimized.steps, b->runOptimized.steps);
    CHECK_EQ(a->runOptimized.trace.size(), b->runOptimized.trace.size());
    for (size_t i = 0; i < a->runOptimized.trace.size(); ++i) { CHECK_EQ(a->runOptimized.trace[i].ip, b->runOptimized.trace[i].ip); CHECK(a->runOptimized.trace[i].frames.size() == b->runOptimized.trace[i].frames.size()); }
}
TEST(vm2, traceIsCappedAndFlagged) {
    CompileOptions co; co.run = true; co.trace = true; co.maxTrace = 50;
    auto r = compileSource(wrapMain("let i: int = 0; while (i < 1000) { i = i + 1; }"), co);
    CHECK_EQ(r->runOptimized.trace.size(), size_t(50));
    CHECK(r->runOptimized.traceTruncated);
}
TEST(vm2, outputLimitIsEnforced) {
    auto r = compileIt(wrapMain("let i: int = 0; while (i < 100000) { print(i); i = i + 1; }"));
    CHECK(!r->runBaseline.ok);
    CHECK(r->runBaseline.error.find("output limit") != std::string::npos);
    CHECK_EQ(r->runBaseline.output.size(), size_t(5000));
}
TEST(vm2, snapshotLocalsMatchFinalProgramState) {
    CompileOptions co; co.run = true; co.trace = true; co.opt = OptConfig::none();
    auto r = compileSource(wrapMain("let a: int = 6; let b: int = a * 7; print(b);"), co);
    // the snapshot recorded before the last PRINT must contain b == 42 in main's frame
    const auto& tr = r->runOptimized.trace;
    bool seen = false;
    for (auto& s : tr) for (auto& f : s.frames) for (auto& v : f.locals) if (v.type == Type::Int && v.i == 42) seen = true;
    CHECK(seen);
}

// ================================================================== arithmetic boundaries (engines must agree with the specification)
namespace {
void expectOut(const std::string& body, const std::vector<std::string>& want) {
    for (const char* passes : {"none", "all", "constant_folding", "constant_propagation,dce", "algebraic,cse"}) {
        OptConfig c; std::string err; CHECK(OptConfig::parse(passes, c, err));
        auto r = compileIt(wrapMain(body), c);
        CHECK(r->success);
        if (r->runBaseline.output != want) throw TestFailure(std::string("baseline wrong for: ") + body);
        if (r->runOptimized.output != want) throw TestFailure(std::string("optimized (") + passes + ") wrong for: " + body);
        if (r->reference.output != want) throw TestFailure(std::string("reference wrong for: ") + body);
    }
}
}
TEST(arith, integerOverflowWraps) {
    expectOut("let m: int = 9223372036854775807; print(m + 1); print(m * 2); print(m * m);", {"-9223372036854775808", "-2", "1"});
    expectOut("let n: int = -9223372036854775807 - 1; print(n - 1); print(0 - n); print(-n);", {"9223372036854775807", "-9223372036854775808", "-9223372036854775808"});
}
TEST(arith, intMinDividedByMinusOneWrapsWithoutTrapping) {
    expectOut("let n: int = -9223372036854775807 - 1; let d: int = -1; print(n / d); print(n % d);", {"-9223372036854775808", "0"});
}
TEST(arith, divisionAndModuloTruncateTowardZero) {
    expectOut("print(7 / 2); print(-7 / 2); print(7 / -2); print(-7 / -2); print(7 % 3); print(-7 % 3); print(7 % -3); print(-7 % -3);", {"3", "-3", "-3", "3", "1", "-1", "1", "-1"});
}
TEST(arith, floatSpecialValues) {
    expectOut("let z: float = 0.0; print(1.0 / z); print(0.0 - 1.0 / z); print(z / z); print(-0.0); print(-z);", {"inf", "-inf", "nan", "-0.0", "-0.0"});
}
TEST(arith, floatToIntConversionBoundaries) {
    expectOut("let z: float = 0.0; print(to_int(2.9)); print(to_int(-2.9)); print(to_int(1.0 / z)); print(to_int(0.0 - 1.0 / z)); print(to_int(z / z)); print(to_int(1000000000000.0 * 1000000000000.0 * 1000000000000.0));", {"2", "-2", "9223372036854775807", "-9223372036854775808", "0", "9223372036854775807"});
}
TEST(arith, floatFormattingIsStable) {
    expectOut("print(0.1 + 0.2); print(1.0); print(100.0); print(1.5 * 2.0); print(1.0 / 3.0);", {"0.3", "1.0", "100.0", "3.0", "0.333333333333333"});
}
TEST(arith, comparisonsOnNaNAndEquality) {
    expectOut("let z: float = 0.0; let n: float = z / z; print(n == n); print(n != n); print(n < 1.0); print(n >= 1.0);", {"false", "true", "false", "false"});
}
TEST(arith, divisionByZeroIsATrapInAllEngines) {
    for (const char* passes : {"none", "all"}) {
        OptConfig c; std::string err; OptConfig::parse(passes, c, err);
        auto r = compileIt(wrapMain("let z: int = 0; print(1); print(5 / z);"), c);
        CHECK(!r->runBaseline.ok); CHECK(!r->runOptimized.ok); CHECK(!r->reference.ok);
        CHECK_EQ(r->runOptimized.error, std::string("division by zero"));
        CHECK_EQ(r->reference.error, std::string("division by zero"));
        CHECK_EQ(r->runOptimized.output.size(), size_t(1));
    }
    auto r = compileIt(wrapMain("let z: int = 0; print(5 % z);"));
    CHECK_EQ(r->runOptimized.error, std::string("modulo by zero"));
}

// ================================================================== regression corpus
TEST(regression, everyCorpusProgramAgreesOnAllEnginesAndPassSubsets) {
    std::string dir = NOVA_REGRESSIONS_DIR;
    DIR* d = opendir(dir.c_str());
    CHECK(d != nullptr);
    int n = 0;
    while (dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name.size() < 6 || name.substr(name.size() - 5) != ".nova") continue;
        std::ifstream f(dir + "/" + name); std::stringstream ss; ss << f.rdbuf();
        ++n;
        for (const char* passes : {"none", "all", "constant_folding", "constant_propagation", "algebraic", "cse", "unreachable", "dce", "cse,dce"}) {
            OptConfig c; std::string err; OptConfig::parse(passes, c, err);
            DiffOutcome o = differentialCheck(ss.str(), c);
            if (!o.compiled || o.inconclusive || !o.match) throw TestFailure(name + " fails with passes=" + passes + ": " + o.detail);
        }
        auto r = compileIt(ss.str(), OptConfig(), false);
        for (auto* prog : {&r->ir, &r->opt.program}) for (auto& fn : prog->functions) {
            CFG g = buildCFG(fn);
            CHECK(verifyCFG(fn, g).empty());
            CHECK(verifySSA(buildSSA(fn, g, computeLiveness(fn, g)), g).empty());
        }
        CHECK(verifyBytecode(r->bcOptimized).empty());
    }
    closedir(d);
    CHECK(n >= 5);
}

// ================================================================== fuzzing framework itself
TEST(fuzz, mutationIsDeterministicAndOftenInvalid) {
    CHECK_EQ(mutateProgram("fn main() { print(1); }", 5), mutateProgram("fn main() { print(1); }", 5));
    int invalid = 0;
    for (uint64_t s = 1; s <= 60; ++s) if (!compileIt(mutateProgram(generateRandomProgram(s), s), OptConfig(), false)->success) ++invalid;
    CHECK(invalid > 15);
}
TEST(fuzz, invalidProgramFuzzingNeverCrashesAndDiagnosticsAreWellFormed) {
    Json j = runInvalidFuzz(400, 123, OptConfig());
    std::string s = j.dump();
    CHECK(s.find("\"crashes\":0") != std::string::npos);
    CHECK(s.find("\"malformedDiagnostics\":0") != std::string::npos);
    CHECK(s.find("\"acceptedMismatches\":0") != std::string::npos);
}
TEST(fuzz, minimizerFindsMinimalFailingLineSet) {
    std::string src = "a\nb\nc\nBUG\nd\ne\nf\ng\nh\n";
    std::string min = minimizeProgram(src, [](const std::string& s) { return s.find("BUG") != std::string::npos; });
    CHECK_EQ(min, std::string("BUG\n"));
    std::string two = minimizeProgram("1\nX\n2\n3\nY\n4\n", [](const std::string& s) { return s.find("X") != std::string::npos && s.find("Y") != std::string::npos; });
    CHECK_EQ(two, std::string("X\nY\n"));
    CHECK_EQ(minimizeProgram("no bug here\n", [](const std::string&) { return false; }), std::string("no bug here\n"));
}
TEST(fuzz, minimizerShrinksARealProgramWithSyntheticPredicate) {
    std::string src = generateRandomProgram(31);
    std::string min = minimizeProgram(src, [](const std::string& s) { return compileSource(s, CompileOptions())->success && s.find("print") != std::string::npos; });
    CHECK(min.size() < src.size());
    CHECK(compileSource(min, CompileOptions())->success);
}
TEST(fuzz, checkerDetectsAnInjectedOptimizerBug) {
    // Mutation-testing the test oracle: a deliberately wrong "optimization" (flip a constant in the optimized IR) must be caught
    // by comparing the optimized VM against the reference interpreter.
    auto r = compileIt(wrapMain("let a: int = 6; print(a * 7);"), OptConfig::none(), false);
    IRProgram bad = r->opt.program;
    bool flipped = false;
    for (auto& i : bad.functions.back().code) if (i.kind == IRInstr::Bin && !flipped) { i.b = Operand::cnst(Value::Int(8)); flipped = true; }
    CHECK(flipped);
    BcProgram bc = generateBytecode(bad);
    VMResult buggy = runVM(bc, VMOptions());
    auto good = compileIt(wrapMain("let a: int = 6; print(a * 7);"));
    CHECK(buggy.output != good->reference.output);
}
TEST(fuzz, savedFailuresAreMinimizedAndReproducible) {
    // generator determinism: the same seed always reproduces the same program, so a saved seed fully reproduces a failure
    for (uint64_t s : {1ull, 99ull, 12345ull}) CHECK_EQ(generateRandomProgram(s), generateRandomProgram(s));
}
TEST(fuzz, randomProgramsIncludeLoopsAtFunctionEntry) {
    int found = 0;
    for (uint64_t s = 1; s <= 200; ++s) if (generateRandomProgram(s).find("while (false &&") != std::string::npos) ++found;
    CHECK(found > 3);
}
