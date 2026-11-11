#include <dirent.h>
#include <fstream>
#include <sstream>
#include "nova/pipeline.hpp"
#include "test_framework.hpp"

using namespace nova;

namespace {

std::unique_ptr<CompileResult> build(const std::string& src, const OptConfig& cfg = OptConfig(), bool run = true) {
    CompileOptions co; co.opt = cfg; co.run = run;
    return compileSource(src, co);
}
std::string joinOut(const std::vector<std::string>& v) { std::string s; for (auto& x : v) s += x + "\n"; return s; }
std::string runOut(const std::string& src, const OptConfig& cfg = OptConfig()) {
    auto r = build(src, cfg);
    if (!r->success) return "COMPILE ERROR: " + (r->diagnostics.items().empty() ? std::string() : r->diagnostics.items()[0].message);
    return joinOut(r->runOptimized.output);
}
bool hasDiag(const CompileResult& r, const std::string& code) { for (auto& d : r.diagnostics.items()) if (d.code == code) return true; return false; }
std::string firstError(const CompileResult& r) { for (auto& d : r.diagnostics.items()) if (d.severity == Severity::Error) return d.message; return ""; }
std::string wrap(const std::string& body) { return "fn main() {\n" + body + "\n}\n"; }
size_t countHist(const OptResult& o, const std::string& pass) { size_t n = 0; for (auto& h : o.history) if (h.pass == pass) ++n; return n; }

} // namespace

// ------------------------------------------------------------------ lexer
TEST(lexer, tokenKindsAndPositions) {
    auto r = lex("fn main() {\n  let x: int = 42;\n}");
    CHECK(!r.diagnostics.hasErrors());
    CHECK_EQ(r.tokens[0].lexeme, std::string("fn"));
    CHECK(r.tokens[0].cat == TokCat::Keyword);
    CHECK(r.tokens[1].cat == TokCat::Identifier);
    CHECK_EQ(r.tokens[1].loc.col, 4);
    // 'let' is on line 2, column 3
    CHECK_EQ(r.tokens[5].lexeme, std::string("let"));
    CHECK_EQ(r.tokens[5].loc.line, 2);
    CHECK_EQ(r.tokens[5].loc.col, 3);
    CHECK(r.tokens.back().cat == TokCat::Eof);
}
TEST(lexer, numbers) {
    auto r = lex("12 3.75 0");
    CHECK(r.tokens[0].cat == TokCat::IntLiteral);
    CHECK(r.tokens[1].cat == TokCat::FloatLiteral);
    CHECK_EQ(r.tokens[1].lexeme, std::string("3.75"));
}
TEST(lexer, twoCharOperators) {
    auto r = lex("== != <= >= && || ->");
    CHECK_EQ(r.tokens.size(), size_t(8));
    CHECK_EQ(r.tokens[6].lexeme, std::string("->"));
}
TEST(lexer, commentsSkipped) {
    auto r = lex("1 // line\n/* block\n comment */ 2");
    CHECK_EQ(r.tokens.size(), size_t(3));
    CHECK_EQ(r.tokens[1].loc.line, 3);
}
TEST(lexer, invalidCharacter) {
    auto r = lex("let a = 5 @ 3;");
    CHECK(r.diagnostics.hasErrors());
    CHECK_EQ(r.diagnostics.items()[0].code, std::string("E-LEX-001"));
    CHECK_EQ(r.diagnostics.items()[0].loc.col, 11);
}
TEST(lexer, singleAmpersandSuggestion) {
    auto r = lex("a & b");
    CHECK(r.diagnostics.hasErrors());
    CHECK(r.diagnostics.items()[0].suggestion.find("&&") != std::string::npos);
}
TEST(lexer, unterminatedComment) {
    CHECK(lex("1 /* never closed").diagnostics.hasErrors());
}
TEST(lexer, malformedNumbers) {
    CHECK(lex("3.").diagnostics.hasErrors());
    CHECK(lex("12abc").diagnostics.hasErrors());
    CHECK(lex("99999999999999999999").diagnostics.hasErrors());
}

// ------------------------------------------------------------------ parser
TEST(parser, grammarIsLR1WithoutConflicts) {
    const LR1Table& t = novaTable();
    CHECK(t.conflicts.empty());
    CHECK(t.stateCount() > 100);
}
TEST(parser, firstAndFollowSets) {
    const Grammar& g = novaTable().grammar;
    int expr = g.symbol("Expr"), rel = g.symbol("RelExpr");
    CHECK(g.first[expr].count(g.symbol("INT_LIT")));
    CHECK(g.first[expr].count(g.symbol("(")));
    CHECK(g.first[expr].count(g.symbol("-")));
    CHECK(!g.first[expr].count(g.symbol("+")));
    CHECK(g.follow[rel].count(g.symbol(")")));
    CHECK(g.follow[rel].count(g.symbol("<")));
    CHECK(g.nullable[g.symbol("StmtList")]);
    CHECK(!g.nullable[g.symbol("Expr")]);
}
TEST(parser, acceptsValidProgram) {
    auto lr = lex("fn main() { let x: int = 1 + 2 * 3; }");
    auto pr = parseTokens(lr.tokens, novaTable());
    CHECK(pr.ok);
    CHECK(pr.tree != nullptr);
    CHECK(pr.shifts > 0 && pr.reduces > 0);
    CHECK_EQ(pr.trace.back().action, std::string("accept"));
}
TEST(parser, traceIsConsistent) {
    auto lr = lex("fn main() { print(1); }");
    auto pr = parseTokens(lr.tokens, novaTable());
    CHECK(pr.ok);
    int s = 0, rr = 0;
    for (auto& st : pr.trace) { if (st.action.rfind("shift", 0) == 0) ++s; if (st.action.rfind("reduce", 0) == 0) ++rr; }
    CHECK_EQ(s, pr.shifts);
    CHECK_EQ(rr, pr.reduces);
    CHECK_EQ(static_cast<int>(pr.trace.size()), pr.totalSteps);
}
TEST(parser, missingSemicolon) {
    auto r = build("fn main() {\n let a: int = 5\n print(a);\n}", OptConfig(), false);
    CHECK(!r->success);
    CHECK_EQ(r->failedStage, std::string("parser"));
    CHECK(hasDiag(*r, "E-SYN-001"));
    CHECK_EQ(r->diagnostics.items()[0].loc.line, 2);   // reported at the end of the previous line
}
TEST(parser, missingParen) {
    auto r = build("fn main() { print((1 + 2); }", OptConfig(), false);
    CHECK(!r->success);
    CHECK(hasDiag(*r, "E-SYN-002"));
}
TEST(parser, invalidExpression) {
    auto r = build("fn main() { let a: int = ; }", OptConfig(), false);
    CHECK(hasDiag(*r, "E-SYN-005"));
}
TEST(parser, unexpectedEof) {
    auto r = build("fn main() { print(1);", OptConfig(), false);
    CHECK(hasDiag(*r, "E-SYN-004"));
}
TEST(parser, danglingElseCannotOccur) {
    // braces are mandatory, so `if (a) print(1);` is rejected rather than silently accepted
    auto r = build("fn main() { if (true) print(1); }", OptConfig(), false);
    CHECK(!r->success);
}
TEST(parser, elseIfChain) {
    CHECK(build("fn main() { let x: int = 1; if (x == 1) { print(1); } else if (x == 2) { print(2); } else { print(3); } }")->success);
}
TEST(parser, tableTraceDetailsPresent) {
    auto lr = lex("fn main() { }");
    auto pr = parseTokens(lr.tokens, novaTable());
    CHECK(pr.ok);
    CHECK(!pr.trace[0].stack.empty());
    CHECK_EQ(pr.trace[0].stack[0], std::string("0"));
}

// ------------------------------------------------------------------ AST
TEST(ast, precedenceAndAssociativity) {
    auto lr = lex("fn main() { let x: int = 1 - 2 - 3 * 4; }");
    auto pr = parseTokens(lr.tokens, novaTable());
    auto ast = buildAst(*pr.tree);
    auto* decl = static_cast<VarDecl*>(ast->functions[0]->body->stmts[0].get());
    auto* top = static_cast<Binary*>(decl->init.get());
    CHECK_EQ(top->op, std::string("-"));                      // (1-2) - (3*4)
    CHECK_EQ(static_cast<Binary*>(top->lhs.get())->op, std::string("-"));
    CHECK_EQ(static_cast<Binary*>(top->rhs.get())->op, std::string("*"));
}
TEST(ast, sourceLocationsPreserved) {
    auto lr = lex("fn main() {\n  print(7);\n}");
    auto pr = parseTokens(lr.tokens, novaTable());
    auto ast = buildAst(*pr.tree);
    Node* p = ast->functions[0]->body->stmts[0].get();
    CHECK_EQ(p->loc.line, 2);
    CHECK_EQ(p->loc.col, 3);
}
TEST(ast, traversalCountsNodes) {
    auto lr = lex("fn f(a: int) -> int { return a + 1; } fn main() { print(f(2)); }");
    auto pr = parseTokens(lr.tokens, novaTable());
    auto ast = buildAst(*pr.tree);
    int fn = 0, calls = 0;
    walk(ast.get(), [&](Node* n) { if (n->kind == NodeKind::Function) ++fn; if (n->kind == NodeKind::Call) ++calls; });
    CHECK_EQ(fn, 2);
    CHECK_EQ(calls, 1);
}
TEST(ast, unaryAndLogical) {
    auto r = build(wrap("print(!(1 < 2) || -3 < 0 && true);"));
    CHECK(r->success);
    CHECK_EQ(joinOut(r->runOptimized.output), std::string("true\n"));
}

// ------------------------------------------------------------------ semantic
TEST(semantic, undefinedVariableWithSuggestion) {
    auto r = build(wrap("let counter: int = 1; print(countr);"), OptConfig(), false);
    CHECK(hasDiag(*r, "E-SEM-001"));
    bool ok = false;
    for (auto& d : r->diagnostics.items()) if (d.code == "E-SEM-001" && d.suggestion.find("counter") != std::string::npos) ok = true;
    CHECK(ok);
}
TEST(semantic, duplicateDeclaration) {
    CHECK(hasDiag(*build(wrap("let a: int = 1; let a: int = 2; print(a);"), OptConfig(), false), "E-SEM-002"));
}
TEST(semantic, shadowingAllowedInInnerScope) {
    auto r = build(wrap("let a: int = 1; { let a: int = 2; print(a); } print(a);"));
    CHECK(r->success);
    CHECK_EQ(joinOut(r->runOptimized.output), std::string("2\n1\n"));
}
TEST(semantic, typeMismatchKinds) {
    CHECK(hasDiag(*build(wrap("let a: int = 1.5;"), OptConfig(), false), "E-SEM-004"));
    CHECK(hasDiag(*build(wrap("let a: int = 1; let b: float = 2.0; print(a + b);"), OptConfig(), false), "E-SEM-005"));
    CHECK(hasDiag(*build(wrap("if (1) { print(1); }"), OptConfig(), false), "E-SEM-004"));
    CHECK(hasDiag(*build(wrap("print(!5);"), OptConfig(), false), "E-SEM-005"));
    CHECK(hasDiag(*build(wrap("print(1.5 % 2.0);"), OptConfig(), false), "E-SEM-005"));
}
TEST(semantic, functionCallValidation) {
    std::string base = "fn add(a: int, b: int) -> int { return a + b; }\nfn main() { ";
    CHECK(hasDiag(*build(base + "print(add(1)); }", OptConfig(), false), "E-SEM-007"));
    CHECK(hasDiag(*build(base + "print(add(1, 2.0)); }", OptConfig(), false), "E-SEM-004"));
    CHECK(hasDiag(*build(base + "print(adc(1, 2)); }", OptConfig(), false), "E-SEM-006"));
}
TEST(semantic, returnChecks) {
    CHECK(hasDiag(*build("fn f() -> int { } fn main() { print(f()); }", OptConfig(), false), "E-SEM-009"));
    CHECK(hasDiag(*build("fn f() -> int { return 1.5; } fn main() { print(f()); }", OptConfig(), false), "E-SEM-004"));
    CHECK(hasDiag(*build("fn f() { return 1; } fn main() { f(); }", OptConfig(), false), "E-SEM-008"));
    CHECK(build("fn f(x: int) -> int { if (x > 0) { return 1; } else { return 2; } } fn main() { print(f(1)); }")->success);
}
TEST(semantic, voidValueUse) {
    CHECK(hasDiag(*build("fn f() { } fn main() { let a: int = f(); }", OptConfig(), false), "E-SEM-013"));
}
TEST(semantic, mainRequired) {
    CHECK(hasDiag(*build("fn f() { }", OptConfig(), false), "E-SEM-010"));
    CHECK(hasDiag(*build("fn main(a: int) { }", OptConfig(), false), "E-SEM-011"));
}
TEST(semantic, duplicateFunction) {
    CHECK(hasDiag(*build("fn f() { } fn f() { } fn main() { }", OptConfig(), false), "E-SEM-003"));
}
TEST(semantic, warningsDoNotBlockCompilation) {
    auto r = build(wrap("let unused: int = 5; return; print(1);"));
    CHECK(r->success);
    CHECK(hasDiag(*r, "W-SEM-001"));
    CHECK(hasDiag(*r, "W-SEM-002"));
}
TEST(semantic, symbolTableStructure) {
    auto r = build("fn f(p: int) -> int { let q: int = p; { let z: int = q; print(z); } return q; } fn main() { print(f(1)); }");
    CHECK(r->success);
    CHECK(r->sem->scopes.size() >= 4);      // global, f, inner block, main
    int syms = 0; for (auto& s : r->sem->scopes) syms += static_cast<int>(s->symbols.size());
    CHECK_EQ(syms, 3);
    CHECK(r->sem->findFunction("to_float") != nullptr);
}
TEST(semantic, editDistance) {
    CHECK_EQ(editDistance("kitten", "sitting"), 3);
    CHECK_EQ(editDistance("", "abc"), 3);
    CHECK_EQ(closestMatch("countr", {"counter", "xyz"}), std::string("counter"));
    CHECK_EQ(closestMatch("zzzzzz", {"counter"}), std::string(""));
}

// ------------------------------------------------------------------ IR
TEST(ir, threeAddressForm) {
    auto r = build(wrap("let result: int = 10 + 20 * 2; print(result);"), OptConfig::none(), false);
    CHECK(r->success);
    auto& c = r->ir.functions[0].code;
    CHECK_EQ(c[0].str(), std::string("t1 = 20 * 2"));
    CHECK_EQ(c[1].str(), std::string("t2 = 10 + t1"));
    CHECK_EQ(c[2].str(), std::string("result = t2"));
    CHECK_EQ(c[3].str(), std::string("print result"));
}
TEST(ir, structuredInstructions) {
    auto r = build(wrap("let a: int = 1; let b: int = a + 2;"), OptConfig::none(), false);
    auto& i = r->ir.functions[0].code[1];
    CHECK(i.kind == IRInstr::Bin);
    CHECK(i.bop == BinOp::Add);
    CHECK(i.a.kind == Operand::Var);
    CHECK(i.b.isConst());
    CHECK(i.line == 1 + 1);
}
TEST(ir, controlFlowLabels) {
    auto r = build(wrap("let i: int = 0; while (i < 3) { i = i + 1; }"), OptConfig::none(), false);
    CHECK(r->ir.countOf(IRInstr::Label) == 2);
    CHECK(r->ir.countOf(IRInstr::JumpIfFalse) == 1);
    CHECK(r->ir.countOf(IRInstr::Jump) == 1);
}
TEST(ir, shortCircuitLowering) {
    auto r = build(wrap("let a: bool = true; let b: bool = a && false;"), OptConfig::none(), false);
    CHECK(r->ir.countOf(IRInstr::JumpIfFalse) == 1);
}

// ------------------------------------------------------------------ optimizer
TEST(optimizer, constantFolding) {
    OptConfig c = OptConfig::none(); c.constantFolding = true;
    auto r = build(wrap("let a: int = 2 * 3 + 4; print(a);"), c);
    CHECK(countHist(r->opt, "constant_folding") >= 1);   // folding alone cannot chain without propagation
    CHECK_EQ(joinOut(r->runOptimized.output), std::string("10\n"));
}
TEST(optimizer, constantPropagationAndDce) {
    auto r = build(wrap("let a: int = 5; let b: int = a + 1; print(b);"));
    CHECK(r->opt.instructionsAfter < r->opt.instructionsBefore);
    CHECK_EQ(r->opt.program.functions[0].code.size(), size_t(2));      // print 6; return
    CHECK_EQ(r->opt.program.functions[0].code[0].str(), std::string("print 6"));
}
TEST(optimizer, algebraicSimplification) {
    OptConfig c = OptConfig::none(); c.algebraic = true;
    auto r = build(wrap("let x: int = 7; let y: int = x * 1 + 0; let z: int = x - x; let w: int = x * 2; print(y + z + w);"), c);
    CHECK(countHist(r->opt, "algebraic") >= 4);
    CHECK_EQ(joinOut(r->runOptimized.output), joinOut(r->runBaseline.output));
}
TEST(optimizer, cse) {
    OptConfig c = OptConfig::none(); c.cse = true;
    auto r = build("fn f(a: int, b: int) -> int { let x: int = a * b + 1; let y: int = a * b + 2; return x + y; } fn main() { print(f(3, 4)); }", c);
    CHECK(countHist(r->opt, "cse") >= 1);
    CHECK_EQ(joinOut(r->runOptimized.output), std::string("27\n"));
}
TEST(optimizer, cseInvalidatedByReassignment) {
    // a*b is recomputed after a changes: reusing the old value would be wrong
    OptConfig c = OptConfig::none(); c.cse = true;
    auto r = build("fn main() { let a: int = 2; let b: int = 3; let x: int = a * b; a = 10; let y: int = a * b; print(x); print(y); }", c);
    CHECK_EQ(joinOut(r->runOptimized.output), std::string("6\n30\n"));
}
TEST(optimizer, unreachableAfterReturnAndDeadBranch) {
    auto r = build("fn f() -> int { return 1; return 2; } fn main() { if (false) { print(99); } print(f()); }");
    CHECK(countHist(r->opt, "unreachable") >= 1);
    CHECK_EQ(joinOut(r->runOptimized.output), std::string("1\n"));
    CHECK(r->opt.program.countOf(IRInstr::JumpIfFalse) == 0);
}
TEST(optimizer, deadStoreByLiveness) {
    OptConfig c = OptConfig::none(); c.dce = true;
    auto r = build(wrap("let x: int = 1; x = 2; x = 3; print(x);"), c);
    CHECK(countHist(r->opt, "dce") >= 2);
    CHECK_EQ(joinOut(r->runOptimized.output), std::string("3\n"));
}
TEST(optimizer, divisionByZeroPreserved) {
    auto r = build(wrap("let z: int = 0; print(1 / z); print(2);"));
    CHECK(!r->runOptimized.ok);
    CHECK_EQ(r->runOptimized.error, std::string("division by zero"));
    CHECK_EQ(r->runOptimized.output.size(), size_t(0));
}
TEST(optimizer, possiblyTrappingDivisionNotRemoved) {
    // result unused but the division can trap: DCE must keep it
    auto r = build(wrap("let z: int = 0; let unused: int = 5 / z; print(1);"));
    CHECK(!r->runOptimized.ok);
}
TEST(optimizer, disabledPassesDoNothing) {
    auto r = build(wrap("let a: int = 1 + 2; print(a);"), OptConfig::none());
    CHECK(r->opt.history.empty());
    CHECK_EQ(r->opt.instructionsAfter, r->opt.instructionsBefore);
}
TEST(optimizer, recordsExplanations) {
    auto r = build(wrap("print(2 * 3);"));
    CHECK(!r->opt.history.empty());
    for (auto& h : r->opt.history) { CHECK(!h.explanation.empty()); CHECK(!h.pass.empty()); CHECK(h.line > 0); }
}
TEST(optimizer, floatSafetyKeepsSignedZero) {
    // x + 0.0 is NOT x for x = -0.0, so it must not be simplified for floats
    OptConfig c = OptConfig::none(); c.algebraic = true;
    auto r = build(wrap("let x: float = -0.0; let y: float = 0.0; print(x + y);"), c);
    CHECK_EQ(joinOut(r->runOptimized.output), joinOut(r->runBaseline.output));
}
TEST(optimizer, eachPassPreservesSemanticsOnExamples) {
    const char* passes[] = {"constant_folding", "constant_propagation", "algebraic", "cse", "unreachable", "dce", "all"};
    std::string src = "fn g(a: int, b: int) -> int { let t: int = a * b; if (t > 10 && a != 0) { return t - 0; } return t + 1; }\n"
                      "fn main() { let i: int = 0; while (i < 5) { print(g(i, i + 1) * 1); i = i + 1; } print(g(2,3) == g(2,3)); }";
    for (auto p : passes) {
        OptConfig c; std::string err; CHECK(OptConfig::parse(p, c, err));
        auto r = build(src, c);
        CHECK(r->success);
        CHECK(r->runOptimized.output == r->runBaseline.output);
        CHECK(r->runOptimized.output == r->reference.output);
    }
}

// ------------------------------------------------------------------ bytecode + VM
TEST(vm, bytecodeForArithmetic) {
    auto r = build(wrap("print(10 + 20 * 2);"), OptConfig::none(), false);
    std::string t = r->bcBaseline.text();
    CHECK(t.find("PUSH 20") != std::string::npos);
    CHECK(t.find("MUL") != std::string::npos);
    CHECK(t.find("PRINT") != std::string::npos);
    CHECK(t.find("HALT") != std::string::npos);
}
TEST(vm, arithmeticAndOutput) {
    CHECK_EQ(runOut(wrap("print(10 + 20 * 2); print(7 / 2); print(-7 % 3); print(2.5 * 2.0);")), std::string("50\n3\n-1\n5.0\n"));
}
TEST(vm, comparisonsAndLogic) {
    CHECK_EQ(runOut(wrap("print(1 < 2); print(2.0 >= 3.0); print(true != false); print(1 == 1 && 2 > 3);")), std::string("true\nfalse\ntrue\nfalse\n"));
}
TEST(vm, shortCircuitSkipsRhs) {
    // rhs would divide by zero; short-circuit must prevent it
    CHECK_EQ(runOut(wrap("let z: int = 0; if (z != 0 && 10 / z > 1) { print(1); } else { print(2); }")), std::string("2\n"));
    CHECK_EQ(runOut(wrap("let z: int = 0; if (z == 0 || 10 / z > 1) { print(1); }")), std::string("1\n"));
}
TEST(vm, loops) {
    CHECK_EQ(runOut(wrap("let i: int = 0; let s: int = 0; while (i < 5) { s = s + i; i = i + 1; } print(s);")), std::string("10\n"));
}
TEST(vm, recursionAndCalls) {
    CHECK_EQ(runOut("fn fact(n: int) -> int { if (n <= 1) { return 1; } return n * fact(n - 1); } fn main() { print(fact(10)); }"), std::string("3628800\n"));
}
TEST(vm, parameterOrder) {
    CHECK_EQ(runOut("fn sub(a: int, b: int, c: int) -> int { return a - b - c; } fn main() { print(sub(10, 3, 2)); }"), std::string("5\n"));
}
TEST(vm, voidFunctionAndDiscardedResult) {
    CHECK_EQ(runOut("fn say(x: int) { print(x); } fn id(x: int) -> int { return x; } fn main() { say(1); id(2); say(3); }"), std::string("1\n3\n"));
}
TEST(vm, castsAndFloatFormatting) {
    CHECK_EQ(runOut(wrap("print(to_float(3)); print(to_int(2.99)); print(to_int(-2.99)); print(1.0 / 3.0 * 3.0);")), std::string("3.0\n2\n-2\n1.0\n"));
}
TEST(vm, integerWraparoundIsDefined) {
    CHECK_EQ(runOut(wrap("let m: int = 9223372036854775807; print(m + 1);")), std::string("-9223372036854775808\n"));
}
TEST(vm, divisionByZeroTrap) {
    auto r = build(wrap("print(1); let z: int = 0; print(5 / z); print(2);"), OptConfig::none());
    CHECK(!r->runBaseline.ok);
    CHECK_EQ(r->runBaseline.error, std::string("division by zero"));
    CHECK_EQ(r->runBaseline.output.size(), size_t(1));
    CHECK(r->runBaseline.errorLine == 1 + 1);
}
TEST(vm, stackOverflowTrap) {
    auto r = build("fn f(n: int) -> int { return f(n + 1); } fn main() { print(f(0)); }");
    CHECK(!r->runBaseline.ok);
    CHECK(r->runBaseline.error.find("stack overflow") != std::string::npos);
}
TEST(vm, stepLimit) {
    CompileOptions co; co.run = true; co.maxSteps = 1000;
    auto r = compileSource(wrap("while (true) { }"), co);
    CHECK(!r->runBaseline.ok);
    CHECK(r->runBaseline.error.find("step limit") != std::string::npos);
}
TEST(vm, traceSnapshots) {
    CompileOptions co; co.run = true; co.trace = true; co.opt = OptConfig::none();
    auto r = compileSource(wrap("let a: int = 2; let b: int = a + 3; print(b);"), co);
    auto& tr = r->runOptimized.trace;
    CHECK(tr.size() > 5);
    CHECK_EQ(tr[0].ip, 0);
    CHECK(tr.back().terminal);
    CHECK_EQ(static_cast<int64_t>(tr.size()), r->runOptimized.steps + 1);
    // after the call to main there is exactly one real frame
    CHECK_EQ(tr[2].frames.size(), size_t(1));
}
TEST(vm, traceShowsFrameCreationAndDestruction) {
    CompileOptions co; co.run = true; co.trace = true; co.opt = OptConfig::none();
    auto r = compileSource("fn f(x: int) -> int { return x + 1; } fn main() { print(f(1)); }", co);
    size_t maxFrames = 0;
    for (auto& s : r->runOptimized.trace) maxFrames = std::max(maxFrames, s.frames.size());
    CHECK_EQ(maxFrames, size_t(2));
    CHECK_EQ(r->runOptimized.trace.back().frames.size(), size_t(0));
}
TEST(vm, sourceMapPresent) {
    auto r = build(wrap("let a: int = 1;\nprint(a);"), OptConfig::none(), false);
    bool any = false;
    for (auto& i : r->bcBaseline.code) if (i.line == 3) any = true;
    CHECK(any);
}

// ------------------------------------------------------------------ end to end
TEST(e2e, allStagesReportedForValidProgram) {
    auto r = build(wrap("print(1);"));
    CHECK(r->success);
    CHECK_EQ(r->stages.size(), size_t(7));
    for (auto& s : r->stages) CHECK_EQ(s.status, std::string("ok"));
}
TEST(e2e, failureSkipsLaterStages) {
    auto r = build(wrap("print(undefined);"), OptConfig(), false);
    CHECK(!r->success);
    CHECK_EQ(r->failedStage, std::string("semantic"));
    CHECK_EQ(r->stages[4].status, std::string("skipped"));
    CHECK_EQ(r->stages[3].status, std::string("error"));
}
TEST(e2e, lexicalErrorStopsPipeline) {
    auto r = build("fn main() { let a = 5 # 3; }", OptConfig(), false);
    CHECK_EQ(r->failedStage, std::string("lexer"));
    CHECK_EQ(r->stages[1].status, std::string("skipped"));
}
TEST(e2e, jsonContainsEveryArtifact) {
    CompileOptions co; co.run = true;
    auto r = compileSource(wrap("print(1 + 2);"), co);
    std::string j = resultToJson(*r, co).dump();
    for (const char* key : {"\"tokens\"", "\"parser\"", "\"ast\"", "\"semantic\"", "\"ir\"", "\"optimization\"", "\"bytecode\"", "\"execution\""})
        CHECK(j.find(key) != std::string::npos);
}
TEST(e2e, referenceInterpreterAgreesWithVm) {
    auto r = build("fn f(n: int) -> int { if (n < 2) { return n; } return f(n - 1) + f(n - 2); } fn main() { print(f(15)); print(to_float(f(10)) / 4.0); }");
    CHECK(r->reference.output == r->runBaseline.output);
    CHECK_EQ(r->reference.output[0], std::string("610"));
}
TEST(e2e, allShippedExamplesBehave) {
    std::string dir = NOVA_EXAMPLES_DIR;
    DIR* d = opendir(dir.c_str());
    CHECK(d != nullptr);
    int n = 0;
    while (dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name.size() < 6 || name.substr(name.size() - 5) != ".nova") continue;
        std::ifstream f(dir + "/" + name);
        std::stringstream ss; ss << f.rdbuf();
        auto r = build(ss.str());
        ++n;
        if (name[0] == 'e') { CHECK(!r->success); CHECK(r->diagnostics.hasErrors()); }
        else {
            CHECK(r->success);
            if (name[0] == 'r') CHECK(!r->runBaseline.ok);
            CHECK(r->runBaseline.output == r->runOptimized.output);
            CHECK(r->runBaseline.ok == r->runOptimized.ok);
            CHECK(r->reference.limitHit || r->reference.output == r->runBaseline.output);
        }
    }
    closedir(d);
    CHECK(n >= 10);
}

// ------------------------------------------------------------------ differential
TEST(differential, randomProgramsAllPasses) {
    Json j = runFuzz(150, 7, OptConfig());
    std::string s = j.dump();
    CHECK(s.find("\"failed\":0") != std::string::npos);
    CHECK(s.find("\"generatorFailures\":0") != std::string::npos);
}
TEST(differential, randomProgramsNoOptimizationBaseline) {
    Json j = runFuzz(60, 99, OptConfig::none());
    CHECK(j.dump().find("\"failed\":0") != std::string::npos);
}
TEST(differential, generatorIsDeterministic) {
    CHECK_EQ(generateRandomProgram(42), generateRandomProgram(42));
    CHECK(generateRandomProgram(42) != generateRandomProgram(43));
}

// ------------------------------------------------------------------ CFG / data-flow / SSA
#include "nova/analysis.hpp"

namespace {
IRFunction mainIR(const std::string& body, const OptConfig& cfg = OptConfig::none()) {
    auto r = build(wrap(body), cfg, false);
    CHECK(r->success);
    return r->opt.program.functions.back();   // main is the last function
}
IRFunction fnIR(const std::string& src, const std::string& name) {
    auto r = build(src, OptConfig::none(), false);
    CHECK(r->success);
    for (auto& f : r->ir.functions) if (f.name == name) return f;
    throw TestFailure("function not found");
}
bool hasEdge(const CFG& g, int a, int b, const std::string& kind) { for (auto& e : g.edges) if (e.from == a && e.to == b && e.kind == kind) return true; return false; }
}

TEST(cfg, straightLineIsOneBlock) {
    IRFunction f = mainIR("let a: int = 1; print(a);");
    CFG g = buildCFG(f);
    CHECK_EQ(g.blocks.size(), size_t(1));
    CHECK(g.edges.empty());
}
TEST(cfg, ifElseDiamond) {
    IRFunction f = mainIR("let a: int = 1; if (a > 0) { print(1); } else { print(2); } print(3);");
    CFG g = buildCFG(f);
    CHECK_EQ(g.blocks.size(), size_t(4));          // cond | then | else | join
    CHECK(hasEdge(g, 0, 2, "false"));              // if_false jumps to the else block
    CHECK(hasEdge(g, 0, 1, "true"));
    CHECK(hasEdge(g, 1, 3, "jump"));
    CHECK(hasEdge(g, 2, 3, "fall"));
    CHECK_EQ(g.blocks[3].pred.size(), size_t(2));
    CHECK_EQ(g.blocks[3].idom, 0);                 // the join is dominated by the condition block
    for (auto& e : g.edges) CHECK(!e.back);
}
TEST(cfg, whileLoopHasBackEdgeAndHeader) {
    IRFunction f = mainIR("let i: int = 0; while (i < 3) { i = i + 1; } print(i);");
    CFG g = buildCFG(f);
    int back = 0, hdr = 0;
    for (auto& e : g.edges) if (e.back) ++back;
    for (auto& b : g.blocks) if (b.loopHeader) ++hdr;
    CHECK_EQ(back, 1);
    CHECK_EQ(hdr, 1);
}
TEST(cfg, leadersAndBlockBoundariesCoverAllInstructions) {
    IRFunction f = fnIR("fn f(n: int) -> int { let s: int = 0; while (n > 0) { if (n % 2 == 0) { s = s + n; } n = n - 1; } return s; } fn main() { print(f(5)); }", "f");
    CFG g = buildCFG(f);
    size_t next = 0;
    for (auto& b : g.blocks) { CHECK_EQ(b.begin, next); CHECK(b.end > b.begin); next = b.end; }
    CHECK_EQ(next, f.code.size());
    // every block except the last ends with a terminator or is followed by a label-leader
    for (auto& b : g.blocks) if (b.id + 1 < (int)g.blocks.size()) {
        auto k = f.code[b.end - 1].kind;
        bool term = k == IRInstr::Jump || k == IRInstr::JumpIfFalse || k == IRInstr::JumpIfTrue || k == IRInstr::Return;
        CHECK(term || f.code[b.end].kind == IRInstr::Label);
    }
}
TEST(cfg, unreachableBlocksMarked) {
    IRFunction f = fnIR("fn f() -> int { return 1; return 2; } fn main() { print(f()); }", "f");
    CFG g = buildCFG(f);
    bool anyUnreachable = false;
    for (auto& b : g.blocks) if (!b.reachable) anyUnreachable = true;
    CHECK(anyUnreachable);
}

TEST(dataflow, livenessOfStraightLine) {
    IRFunction f = mainIR("let a: int = 1; let b: int = a + 2; print(b);");
    CFG g = buildCFG(f);
    DataflowResult l = computeLiveness(f, g);
    CHECK(l.converged);
    CHECK(l.in[0].empty());                        // nothing live on entry (all variables are initialised first)
    CHECK(l.gen[0].count("a") == 0);               // `a` is defined before its use inside the block
}
TEST(dataflow, livenessAcrossLoop) {
    IRFunction f = mainIR("let i: int = 0; let s: int = 0; while (i < 3) { s = s + i; i = i + 1; } print(s);");
    CFG g = buildCFG(f);
    DataflowResult l = computeLiveness(f, g);
    CHECK(l.converged);
    // find the loop header: both i and s are live on entry to it
    for (auto& b : g.blocks) if (b.loopHeader) { CHECK(l.in[b.id].count("i")); CHECK(l.in[b.id].count("s")); }
    CHECK(l.iterations >= 2);                      // the loop needs more than one pass to reach the fixed point
    // fixed-point property: OUT[b] = union of IN[succ]
    for (auto& b : g.blocks) { std::set<std::string> u; for (int s : b.succ) u.insert(l.in[s].begin(), l.in[s].end()); CHECK(u == l.out[b.id]); }
}
TEST(dataflow, reachingDefinitionsAtJoin) {
    IRFunction f = mainIR("let x: int = 1; if (x > 0) { x = 2; } else { x = 3; } print(x);");
    CFG g = buildCFG(f);
    DataflowResult rd = computeReachingDefs(f, g);
    CHECK(rd.converged);
    int join = static_cast<int>(g.blocks.size()) - 1;
    int xdefs = 0;
    for (auto& id : rd.in[join]) if (rd.defText.at(id).rfind("x = ", 0) == 0) ++xdefs;
    CHECK_EQ(xdefs, 2);                            // `x = 2` and `x = 3` reach the join, `x = 1` is killed on both paths
}
TEST(dataflow, reachingDefsFixedPointEquation) {
    IRFunction f = mainIR("let i: int = 0; while (i < 3) { i = i + 1; } print(i);");
    CFG g = buildCFG(f);
    DataflowResult rd = computeReachingDefs(f, g);
    for (auto& b : g.blocks) {
        std::set<std::string> out = rd.gen[b.id];
        for (auto& d : rd.in[b.id]) if (!rd.kill[b.id].count(d)) out.insert(d);
        CHECK(out == rd.out[b.id]);
    }
}

namespace {
void checkSsa(const IRFunction& f) {
    CFG g = buildCFG(f);
    DataflowResult l = computeLiveness(f, g);
    SsaFunction s = buildSSA(f, g, l);
    std::set<std::string> defs(s.definitions.begin(), s.definitions.end());
    CHECK_EQ(defs.size(), s.definitions.size());                    // static single assignment: every name defined exactly once
    for (auto& u : s.uses) CHECK(defs.count(u) || u.find("#undef") != std::string::npos);
    for (auto& u : s.uses) CHECK(u.find("#undef") == std::string::npos);   // every use has a reaching definition
}
}
TEST(ssa, phiAtIfJoin) {
    IRFunction f = mainIR("let x: int = 1; if (x > 0) { x = 2; } else { x = 3; } print(x);");
    CFG g = buildCFG(f);
    SsaFunction s = buildSSA(f, g, computeLiveness(f, g));
    CHECK_EQ(s.phiCount, 1);
    CHECK_EQ(s.blocks.back().phis[0].var, std::string("x"));
    CHECK_EQ(s.blocks.back().phis[0].args.size(), size_t(2));
    checkSsa(f);
}
TEST(ssa, phisAtLoopHeader) {
    IRFunction f = mainIR("let i: int = 0; let s: int = 0; while (i < 3) { s = s + i; i = i + 1; } print(s);");
    CFG g = buildCFG(f);
    SsaFunction s = buildSSA(f, g, computeLiveness(f, g));
    int header = -1; for (auto& b : g.blocks) if (b.loopHeader) header = b.id;
    CHECK(header >= 0);
    std::set<std::string> vars; for (auto& p : s.blocks[header].phis) vars.insert(p.var);
    CHECK(vars.count("i") && vars.count("s"));
    checkSsa(f);
}
TEST(ssa, noPhiWithoutMerge) {
    IRFunction f = mainIR("let a: int = 1; a = a + 1; print(a);");
    CFG g = buildCFG(f);
    CHECK_EQ(buildSSA(f, g, computeLiveness(f, g)).phiCount, 0);
}
TEST(ssa, validOnAllShippedExamplesAndRandomPrograms) {
    for (uint64_t seed = 1; seed <= 150; ++seed) {
        auto r = build(generateRandomProgram(seed), OptConfig::none(), false);
        CHECK(r->success);
        for (auto& f : r->ir.functions) checkSsa(f);
        auto ro = build(generateRandomProgram(seed), OptConfig(), false);
        for (auto& f : ro->opt.program.functions) checkSsa(f);
    }
}
TEST(analysis, jsonIncludesAllArtifacts) {
    CompileOptions co;
    auto r = compileSource(wrap("let i: int = 0; while (i < 2) { i = i + 1; } print(i);"), co);
    std::string j = resultToJson(*r, co).dump();
    for (const char* k : {"\"analysis\"", "\"blocks\"", "\"liveness\"", "\"reachingDefinitions\"", "\"ssa\"", "\"loopHeader\":true", "\"history\""}) CHECK(j.find(k) != std::string::npos);
}

// ------------------------------------------------------------------ arrays and strings
TEST(heap, arraysAndStringsRunIdenticallyInAllEngines) {
    std::string src = wrap("let a: int[] = [3, 4, 5]; a[1] = 10; print(a[0] + a[1] + a[2]); print(len(a));\n"
                           "let b: float[] = new float[2]; b[1] = 1.5; print(b[1] + b[0]);\n"
                           "let s: string = \"ab\" + \"cd\"; print(s); print(len(s)); print(s == \"abcd\"); print(s != \"abcd\");");
    auto r = build(src);
    CHECK(r->success);
    CHECK_EQ(joinOut(r->runBaseline.output), std::string("18\n3\n1.5\nabcd\n4\ntrue\nfalse\n"));
    CHECK(r->runBaseline.output == r->runOptimized.output);
    CHECK(r->runBaseline.output == r->reference.output);
}
TEST(heap, boundsAndSizeTraps) {
    auto r = build(wrap("let a: int[] = [1]; print(a[1]);"));
    CHECK(!r->runBaseline.ok); CHECK_EQ(r->runBaseline.error, std::string("index out of bounds"));
    CHECK_EQ(r->reference.error, std::string("index out of bounds"));
    r = build(wrap("let n: int = 0 - 1; let a: int[] = new int[n]; print(len(a));"));
    CHECK_EQ(r->runBaseline.error, std::string("negative array size"));
    CHECK_EQ(r->runOptimized.error, std::string("negative array size"));
}
TEST(heap, arraysArePassedByReference) {
    std::string src = "fn fill(a: int[]) { a[0] = 42; }\nfn main() { let a: int[] = new int[1]; fill(a); print(a[0]); }\n";
    CHECK_EQ(runOut(src), std::string("42\n"));
}
TEST(heap, typeErrors) {
    CHECK(hasDiag(*build(wrap("let a: int[] = [1, 2.5];"), OptConfig(), false), "E-SEM-004"));       // mixed element types
    CHECK(hasDiag(*build(wrap("let a: int[] = [1]; a[true] = 2;"), OptConfig(), false), "E-SEM-015")); // non-int index
    CHECK(hasDiag(*build(wrap("let a: int[] = [1]; a[0] = 1.5;"), OptConfig(), false), "E-SEM-004"));  // wrong element type
    CHECK(hasDiag(*build(wrap("let x: int = 1; print(x[0]);"), OptConfig(), false), "E-SEM-014"));    // indexing a non-array
    CHECK(hasDiag(*build(wrap("let a: int[] = [1]; print(a);"), OptConfig(), false), "E-SEM-016"));   // printing an array
    CHECK(hasDiag(*build(wrap("let s: string = \"a\" - \"b\";"), OptConfig(), false), "E-SEM-005"));  // unsupported string operator
    CHECK(hasDiag(*build(wrap("print(len(3));"), OptConfig(), false), "E-SEM-004"));                  // len of an int
}
TEST(heap, lexerStringLiterals) {
    auto r = lex("\"a\\n\\\"b\\\\\"");
    CHECK(!r.diagnostics.hasErrors());
    CHECK(r.tokens[0].cat == TokCat::StringLiteral);
    CHECK_EQ(r.tokens[0].lexeme, std::string("a\n\"b\\"));
    CHECK(lex("\"unterminated").diagnostics.hasErrors());
}
TEST(heap, stringOperationsSurviveOptimization) {
    // constant folding must not fold string comparisons/concatenations into wrong values
    std::string src = wrap("let a: string = \"x\"; let b: string = \"y\"; print(a == b); print(a + b == \"xy\"); print(\"p\" == \"p\");");
    CHECK_EQ(runOut(src), std::string("false\ntrue\ntrue\n"));
    CHECK_EQ(runOut(src, OptConfig::none()), std::string("false\ntrue\ntrue\n"));
}
