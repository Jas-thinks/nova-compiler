#!/usr/bin/env python3
"""Generate LaTeX data fragments for the report from the REAL project (compiler output, tests, fuzzers, git log).

Every number/table/listing in the report that is not prose comes from this script, so it cannot drift from the code.
Usage: python3 report/tools/gen_data.py   (writes report/gen/*.tex)
"""
import glob
import json
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GEN = os.path.join(ROOT, "report", "gen")
NOVAC = os.path.join(ROOT, "compiler", "build", "novac")
os.makedirs(GEN, exist_ok=True)


def sh(cmd, inp=None, cwd=ROOT, timeout=900):
    p = subprocess.run(cmd, input=inp.encode() if isinstance(inp, str) else inp, capture_output=True, cwd=cwd, timeout=timeout, shell=isinstance(cmd, str))
    return p.stdout.decode("utf-8", "replace"), p.stderr.decode("utf-8", "replace"), p.returncode


def tex(s):
    """Escape text for normal LaTeX context."""
    s = str(s)
    rep = {"\\": r"\textbackslash{}", "&": r"\&", "%": r"\%", "$": r"\$", "#": r"\#", "_": r"\_", "{": r"\{", "}": r"\}", "~": r"\textasciitilde{}", "^": r"\textasciicircum{}",
           "<": r"\textless{}", ">": r"\textgreater{}", "|": r"\textbar{}", "→": r"$\rightarrow$", "≤": r"$\leq$", "≥": r"$\geq$", "…": r"\ldots{}", "×": r"$\times$",
           "φ": r"$\varphi$", "∅": r"$\emptyset$", "≡": r"$\equiv$", "−": "-", "—": "---", "–": "--", "‘": "`", "’": "'", "“": "``", "”": "''", "é": r"\'e", "·": r"$\cdot$", "ε": r"$\varepsilon$"}
    return "".join(rep.get(c, c) for c in s)


def ascii_only(s):
    """lstlisting cannot show non-ASCII glyphs reliably: map the few we use."""
    m = {"→": "->", "≤": "<=", "≥": ">=", "…": "...", "×": "x", "φ": "phi", "∅": "{}", "—": "--", "–": "-", "’": "'", "‘": "'", "“": '"', "”": '"', "é": "e", "·": ".", "↦": "->", "−": "-"}
    return "".join(m.get(c, c) for c in s).encode("ascii", "replace").decode()


def write(name, text):
    with open(os.path.join(GEN, name), "w") as f:
        f.write(text)
    print("wrote", name)


def listing(code, lang="", extra=""):
    return "\\begin{lstlisting}[" + (f"language={lang}," if lang else "") + extra + "]\n" + ascii_only(code).rstrip("\n") + "\n\\end{lstlisting}\n"


macros = {}


def macro(name, value):
    macros[name] = value


# ------------------------------------------------------------------------------------------------- 1. lines of code
def count_lines(patterns):
    n = 0
    for pat in patterns:
        for f in glob.glob(os.path.join(ROOT, pat), recursive=True):
            if os.path.isfile(f):
                n += sum(1 for _ in open(f, errors="replace"))
    return n


groups = [
    ("Lexer", ["compiler/src/lexer/*.cpp"]), ("LR(1) parser generator and parser", ["compiler/src/parser/*.cpp"]), ("AST construction", ["compiler/src/ast/*.cpp"]),
    ("Semantic analysis", ["compiler/src/semantic/*.cpp"]), ("IR generation", ["compiler/src/ir/*.cpp"]), ("Optimizer (incl. inlining)", ["compiler/src/optimizer/*.cpp"]),
    ("CFG / SSA / data-flow analyses", ["compiler/src/analysis/*.cpp"]), ("Bytecode generator, verifier, x86-64 backend", ["compiler/src/codegen/*.cpp"]),
    ("Bytecode VM and reference interpreter", ["compiler/src/vm/*.cpp"]), ("Pipeline, diagnostics, CLI, fuzzers", ["compiler/src/pipeline/*.cpp", "compiler/src/diagnostics/*.cpp", "compiler/src/tools/*.cpp", "compiler/src/testing/*.cpp"]),
    ("NOVA-SQL compiler and engine", ["compiler/src/sql/*.cpp", "compiler/src/sql/*.hpp"]), ("C++ headers", ["compiler/include/nova/*.hpp"]), ("Native runtime (C)", ["compiler/runtime/*.c"]),
    ("C++ unit tests", ["compiler/tests/*.cpp", "compiler/tests/*.hpp"]),
    ("Backend service (Python)", ["backend/app/*.py"]), ("Backend tests (Python)", ["backend/tests/*.py"]),
    ("Frontend studio (TypeScript/React)", ["frontend/src/**/*.ts", "frontend/src/**/*.tsx", "frontend/src/**/*.css"]),
    ("Scripts (shell/Python)", ["scripts/*.sh", "scripts/*.py"]),
]
rows, total = [], 0
for name, pats in groups:
    n = count_lines(pats)
    total += n
    rows.append((name, n))
t = "\\begin{tabular}{@{}lr@{}}\\toprule\n\\textbf{Component} & \\textbf{Lines}\\\\ \\midrule\n"
for name, n in rows:
    t += f"{tex(name)} & {n:,}\\\\\n".replace(",", "{,}")
t += "\\midrule\n\\textbf{Total} & \\textbf{" + f"{total:,}".replace(",", "{,}") + "}\\\\ \\bottomrule\n\\end{tabular}\n"
write("loc_table.tex", t)
macro("TotalLOC", f"{total:,}".replace(",", "{,}"))
macro("SQLLOC", f"{count_lines(['compiler/src/sql/*.cpp', 'compiler/src/sql/*.hpp']):,}".replace(",", "{,}"))
macro("CompilerLOC", f"{sum(n for nme, n in rows if nme in [g[0] for g in groups[:13]]):,}".replace(",", "{,}"))

# ------------------------------------------------------------------------------------------------- 2. tests
out, err, rc = sh([os.path.join(ROOT, "compiler", "build", "nova_tests")])
cpp = {}
for m in re.finditer(r"\[ (PASS|FAIL) \] ([A-Za-z0-9]+)\.", out):
    cpp.setdefault(m.group(2), [0, 0])[0 if m.group(1) == "PASS" else 1] += 1
cpp_total = sum(a + b for a, b in cpp.values())
cpp_fail = sum(b for a, b in cpp.values())
desc = {"lexer": "tokens, positions, comments, malformed literals", "parser": "LR(1) parsing, precedence, associativity, errors", "grammar": "FIRST/FOLLOW, LR(1) tables, zero conflicts",
        "ast": "AST shape and source ranges", "astvalid": "AST structural validation", "semantic": "scopes, types, calls, returns, diagnostics", "semantic2": "additional semantic rules",
        "ir": "IR generation", "optimizer": "each optimization pass and fixed point", "inlining": "interprocedural inlining", "arith": "integer/float semantics, traps",
        "vm": "bytecode VM, calls, runtime errors", "vm2": "trace capping, deterministic snapshots", "verifier": "static bytecode verifier", "e2e": "end-to-end programs",
        "differential": "VM vs reference interpreter vs optimized VM", "fuzz": "random program generator, mutation fuzzing, minimizer", "regression": "corpus of all examples, all pass subsets",
        "cfg": "basic blocks, edges, dominators", "cfg2": "loops, reachability, entry loops", "dataflow": "liveness, reaching definitions", "dataflow2": "iteration history, convergence",
        "ssa": "pruned SSA construction", "ssa2": "SSA verifier, phi operands", "analysis": "analysis JSON artifacts", "recovery": "panic-mode syntax error recovery",
        "regalloc": "x86-64 backend and register allocation", "heap": "arrays and strings", "boundary": "limits and boundary conditions", "sql": "NOVA-SQL compiler and engine"}
t = "\\begin{tabular}{@{}llr@{}}\\toprule\n\\textbf{Suite} & \\textbf{What it checks} & \\textbf{Tests}\\\\ \\midrule\n"
for k, (a, b) in sorted(cpp.items(), key=lambda kv: (-sum(kv[1]), kv[0])):
    t += f"\\texttt{{{tex(k)}}} & {tex(desc.get(k, ''))} & {a + b}\\\\\n"
t += f"\\midrule\n\\multicolumn{{2}}{{@{{}}l}}{{\\textbf{{Total C++ unit tests}}}} & \\textbf{{{cpp_total}}}\\\\ \\bottomrule\n\\end{{tabular}}\n"
write("tests_cpp.tex", t)
macro("CppTests", cpp_total)
macro("CppTestsFailed", cpp_fail)

out, err, rc = sh([os.path.join(ROOT, "backend", ".venv", "bin", "python"), "-m", "pytest", "--collect-only", "-q"], cwd=os.path.join(ROOT, "backend"))
be = {}
for line in out.splitlines():
    if "::" in line:
        be[line.split("::")[0].replace("tests/", "")] = be.get(line.split("::")[0].replace("tests/", ""), 0) + 1
bdesc = {"test_api.py": "REST API: compile, optimize, debug sessions, limits, validation, sandbox", "test_ai_assist.py": "verified AI fix/debug loop with a scripted fake model",
         "test_lsp.py": "language server protocol and stdio framing", "test_quickfix.py": "quick fixes, auto-fix, runtime analysis, asm endpoint", "test_sql.py": "NOVA-SQL API, work limits, 200-case SQLite differential"}
t = "\\begin{tabular}{@{}llr@{}}\\toprule\n\\textbf{File} & \\textbf{What it checks} & \\textbf{Tests}\\\\ \\midrule\n"
for k, v in sorted(be.items()):
    t += f"\\texttt{{{tex(k)}}} & {tex(bdesc.get(k, ''))} & {v}\\\\\n"
t += f"\\midrule\n\\multicolumn{{2}}{{@{{}}l}}{{\\textbf{{Total backend tests}}}} & \\textbf{{{sum(be.values())}}}\\\\ \\bottomrule\n\\end{{tabular}}\n"
write("tests_backend.tex", t)
macro("BackendTests", sum(be.values()))

out, err, rc = sh("npx vitest run --reporter=json 2>/dev/null", cwd=os.path.join(ROOT, "frontend"))
fe = {}
try:
    d = json.loads(out[out.index("{"):])
    for f in d["testResults"]:
        fe[f["name"].split("/src/")[-1]] = len(f["assertionResults"])
except Exception:
    pass
t = "\\begin{tabular}{@{}lr@{}}\\toprule\n\\textbf{File} & \\textbf{Tests}\\\\ \\midrule\n"
for k, v in sorted(fe.items()):
    t += f"\\texttt{{{tex(k)}}} & {v}\\\\\n"
t += f"\\midrule\n\\textbf{{Total frontend tests}} & \\textbf{{{sum(fe.values())}}}\\\\ \\bottomrule\n\\end{{tabular}}\n"
write("tests_frontend.tex", t)
macro("FrontendTests", sum(fe.values()))
macro("TotalTests", cpp_total + sum(be.values()) + sum(fe.values()))

# ------------------------------------------------------------------------------------------------- 3. grammar
src = open(os.path.join(ROOT, "compiler/src/parser/grammar.cpp")).read()
g = re.search(r'R"G\((.*?)\)G"', src, re.S).group(1).strip()
write("grammar.tex", listing(g))
gr = json.loads(sh([NOVAC, "grammar"])[0])
macro("LRStates", gr["states"])
macro("LRProductions", len(gr["grammar"]["productions"]) - 1)
macro("LRTerminals", len(gr["grammar"]["terminals"]))
macro("LRNonterminals", len(gr["grammar"]["nonterminals"]))
macro("LRConflicts", len(gr["conflicts"]))
macro("LRBuildMs", f"{gr['buildMillis']:.1f}")
macro("LRShift", gr["shiftEntries"])
macro("LRReduce", gr["reduceEntries"])
macro("LRGoto", gr["gotoEntries"])

# ------------------------------------------------------------------------------------------------- 4. ISA
isa = json.loads(sh([NOVAC, "isa"])[0])
t = "\\begin{longtable}{@{}lp{0.78\\textwidth}@{}}\\toprule\n\\textbf{Opcode} & \\textbf{Effect}\\\\ \\midrule\\endhead\n"
for i in isa:
    t += f"\\texttt{{{tex(i['op'])}}} & {tex(i['description'])}\\\\\n"
t += "\\bottomrule\n\\end{longtable}\n"
write("isa.tex", t)
macro("NumOpcodes", len(isa))

# ------------------------------------------------------------------------------------------------- 5. diagnostics catalogue
codes = {}
for f in glob.glob(os.path.join(ROOT, "compiler/src/**/*.cpp"), recursive=True):
    s = open(f).read()
    for m in re.finditer(r'"([A-Z][A-Za-z ]+)",\s*"((?:E|W)-[A-Z]+-\d+)"', s):
        codes.setdefault(m.group(2), m.group(1))
    for m in re.finditer(r'd\.category = "([^"]+)"; d\.code = "((?:E|W)-[A-Z]+-\d+)"', s):
        codes.setdefault(m.group(2), m.group(1))
    for m in re.finditer(r'error\("([^"]+)",\s*"(E-[A-Z]+-\d+)"', s):
        codes.setdefault(m.group(2), m.group(1))
t = "\\begin{longtable}{@{}llp{0.55\\textwidth}@{}}\\toprule\n\\textbf{Code} & \\textbf{Phase} & \\textbf{Category}\\\\ \\midrule\\endhead\n"
phase = {"LEX": "lexical", "SYN": "syntax", "SEM": "semantic", "SQL": "NOVA-SQL"}
for c in sorted(codes):
    t += f"\\texttt{{{c}}} & {phase.get(c.split('-')[1], '')} & {tex(codes[c])}\\\\\n"
t += "\\bottomrule\n\\end{longtable}\n"
write("diag.tex", t)
macro("NumDiagCodes", len(codes))

# ------------------------------------------------------------------------------------------------- 6. endpoints
main_py = open(os.path.join(ROOT, "backend/app/main.py")).read()
eps = []
for m in re.finditer(r'@app\.(get|post)\("([^"]+)"[^\n]*\)\ndef (\w+)\([^)]*\):\n(?:    """(.*?)""")?', main_py, re.S):
    eps.append((m.group(1).upper(), m.group(2), (m.group(4) or "").strip().split("\n")[0]))
t = "\\begin{longtable}{@{}llp{0.55\\textwidth}@{}}\\toprule\n\\textbf{Method} & \\textbf{Path} & \\textbf{Purpose}\\\\ \\midrule\\endhead\n"
for mth, path, doc in eps:
    t += f"{mth} & \\texttt{{{tex(path)}}} & {tex(doc)}\\\\\n"
t += "\\bottomrule\n\\end{longtable}\n"
write("endpoints.tex", t)
macro("NumEndpoints", len(eps))

# ------------------------------------------------------------------------------------------------- 7. git log
log, _, _ = sh(["git", "log", "--reverse", "--format=%h|%ad|%s", "--date=short"])
t = "\\begin{longtable}{@{}lll@{}}\\toprule\n\\textbf{Commit} & \\textbf{Date} & \\textbf{Message}\\\\ \\midrule\\endhead\n"
n = 0
for line in log.strip().splitlines():
    h, d, s = line.split("|", 2)
    t += f"\\texttt{{{h}}} & {d} & \\parbox[t]{{0.68\\textwidth}}{{{tex(s[:160])}}}\\\\\n"
    n += 1
t += "\\bottomrule\n\\end{longtable}\n"
write("gitlog.tex", t)
macro("NumCommits", n)

# ------------------------------------------------------------------------------------------------- 8. example programs + outputs
t = ""
ex_files = sorted(glob.glob(os.path.join(ROOT, "examples", "*.nova")))
macro("NumExamples", len(ex_files))
for f in ex_files:
    body = open(f).read()
    title = re.search(r"@title (.*)", body).group(1)
    desc_ = re.search(r"@desc (.*)", body).group(1)
    res = json.loads(sh([NOVAC, "compile", "--run"], body)[0])
    code = "\n".join(l for l in body.split("\n") if not l.startswith("// @"))
    t += f"\\subsection*{{{tex(os.path.basename(f))} --- {tex(title)}}}\n\\noindent {tex(desc_)}\n"
    t += listing(code, "nova")
    if not res["success"]:
        diag = "\n".join(f"{d['code']} at {d['loc']['line']}:{d['loc']['col']}: {d['message']}" + (f"\n    hint: {d['suggestion']}" if d.get('suggestion') else "") for d in res["diagnostics"] if d["severity"] == "error")
        t += "\\begin{tcolorbox}[colback=red!4,colframe=red!60!black,title={Compiler output},fonttitle=\\bfseries\\small,boxsep=2pt,left=4pt,right=4pt,top=2pt,bottom=2pt]\n" + listing(diag, extra="basicstyle=\\ttfamily\\scriptsize,frame=none,backgroundcolor=\\color{white}") + "\\end{tcolorbox}\n"
    else:
        ex = res["execution"]["optimized"]
        o = "\n".join(ex["output"][:12]) + ("\n..." if len(ex["output"]) > 12 else "")
        if not ex["ok"]:
            o += f"\n[runtime error: {ex['error']['message']} at line {ex['error']['line']}]"
        t += "\\begin{tcolorbox}[colback=green!4,colframe=green!45!black,title={Program output (optimized VM)},fonttitle=\\bfseries\\small,boxsep=2pt,left=4pt,right=4pt,top=2pt,bottom=2pt]\n" + listing(o or "(no output)", extra="basicstyle=\\ttfamily\\scriptsize,frame=none,backgroundcolor=\\color{white}") + "\\end{tcolorbox}\n"
write("examples.tex", t)
times = []
for f in ex_files:
    r_ = json.loads(sh([NOVAC, "compile", "--run"], open(f).read())[0])
    times.append(sum(st["durationMs"] for st in r_["stages"]))
macro("CompileMsAvg", f"{sum(times) / len(times):.2f}")
macro("CompileMsMax", f"{max(times):.2f}")

# ------------------------------------------------------------------------------------------------- 9. parse trace sample
prog = "fn main() { print(1 + 2 * 3); }"
res = json.loads(sh([NOVAC, "compile"], prog)[0])
macro("SampleParseSteps", res["parser"]["totalSteps"])
macro("SampleParseShifts", res["parser"]["shifts"])
macro("SampleParseReduces", res["parser"]["reduces"])
macro("SampleTokens", len(res["tokens"]))
t = "\\begin{longtable}{@{}rp{0.30\\textwidth}lp{0.30\\textwidth}@{}}\\toprule\n\\textbf{\\#} & \\textbf{Stack (states and symbols)} & \\textbf{Look-ahead} & \\textbf{Action}\\\\ \\midrule\\endhead\n"
for s in res["parser"]["trace"][:26]:
    stack = " ".join(s["stack"][-9:])
    if len(s["stack"]) > 9:
        stack = "\\ldots{} " + tex(stack)
    else:
        stack = tex(stack)
    t += f"{s['step']} & \\texttt{{\\scriptsize {stack}}} & \\texttt{{{tex(s['lookahead'])}}} & \\texttt{{\\scriptsize {tex(s['action'])}}}"
    if s["prod"] >= 0:
        t += f" \\newline {{\\scriptsize {tex(gr['grammar']['productions'][s['prod']]['text'])}}}"
    t += "\\\\\n"
t += "\\bottomrule\n\\end{longtable}\n"
write("parse_trace.tex", t)
tokrows = "\\begin{tabular}{@{}rllrl@{}}\\toprule\n\\textbf{\\#} & \\textbf{Category} & \\textbf{Lexeme} & \\textbf{Line:Col} & \\textbf{Grammar terminal}\\\\ \\midrule\n"
for i, tk in enumerate(res["tokens"][:20]):
    tokrows += f"{i} & {tex(tk['category'])} & \\texttt{{{tex(tk['lexeme'])}}} & {tk['loc']['line']}:{tk['loc']['col']} & \\texttt{{{tex(tk['terminal'])}}}\\\\\n"
tokrows += "\\bottomrule\n\\end{tabular}\n"
write("tokens.tex", tokrows)

# ------------------------------------------------------------------------------------------------- 10. IR / optimizer demos
demo = open(os.path.join(ROOT, "examples/04_optimization_demo.nova")).read()
code = "\n".join(l for l in demo.split("\n") if not l.startswith("// @"))
res = json.loads(sh([NOVAC, "compile", "--run"], demo)[0])
orig = res["ir"]["original"]["functions"]
opt = res["ir"]["optimized"]["functions"]


def fn_text(f):
    return "\n".join(("" if i["kind"] == "label" else "    ") + i["text"] for i in f["instructions"])


write("opt_demo_src.tex", listing(code, "nova"))
write("opt_demo_ir_before.tex", listing("\n".join(f"function {f['name']}:\n{fn_text(f)}" for f in orig if f["name"] == "main")))
write("opt_demo_ir_after.tex", listing("\n".join(f"function {f['name']}:\n{fn_text(f)}" for f in opt if f["name"] == "main")))
hist = res["optimization"]["history"]
perpass = res["optimization"]["perPass"]
t = "\\begin{longtable}{@{}rp{0.2\\textwidth}p{0.28\\textwidth}p{0.36\\textwidth}@{}}\\toprule\n\\textbf{Rd} & \\textbf{Pass} & \\textbf{Before} $\\rightarrow$ \\textbf{after} & \\textbf{Explanation}\\\\ \\midrule\\endhead\n"
for h in hist[:18]:
    t += f"{h['round']} & \\texttt{{{tex(h['pass'])}}} & \\texttt{{\\scriptsize {tex(h['before'])}}} $\\rightarrow$ \\texttt{{\\scriptsize {tex(h['after'])}}} & {{\\scriptsize {tex(h['explanation'][:230])}}}\\\\\n"
t += "\\bottomrule\n\\end{longtable}\n"
write("opt_demo_history.tex", t)
macro("OptDemoBefore", res["optimization"]["instructionsBefore"])
macro("OptDemoAfter", res["optimization"]["instructionsAfter"])
macro("OptDemoRounds", res["optimization"]["rounds"])
macro("OptDemoVMBefore", res["execution"]["baseline"]["steps"])
macro("OptDemoVMAfter", res["execution"]["optimized"]["steps"])

inl = "fn sq(x: int) -> int { return x * x; }\nfn hyp(a: int, b: int) -> int { return sq(a) + sq(b); }\nfn main() { print(hyp(3, 4)); }\n"
res = json.loads(sh([NOVAC, "compile", "--run"], inl)[0])
write("inl_src.tex", listing(inl, "nova"))
write("inl_ir_before.tex", listing("\n".join(f"function {f['name']}:\n{fn_text(f)}" for f in res["ir"]["original"]["functions"])))
write("inl_ir_after.tex", listing("\n".join(f"function {f['name']}:\n{fn_text(f)}" for f in res["ir"]["optimized"]["functions"] if f["name"] == "main")))
macro("InlOut", res["execution"]["optimized"]["output"][0])
macro("InlBefore", res["optimization"]["instructionsBefore"])
macro("InlAfter", res["optimization"]["instructionsAfter"])
macro("InlCount", res["optimization"]["perPass"].get("inlining", 0))

# CFG/dataflow demo
loop = "fn sum(n: int) -> int {\n    let s: int = 0;\n    let i: int = 1;\n    while (i <= n) {\n        s = s + i;\n        i = i + 1;\n    }\n    return s;\n}\nfn main() { print(sum(10)); }\n"
res = json.loads(sh([NOVAC, "compile", "--run"], loop)[0])
fa = res["analysis"]["original"][0]
write("loop_src.tex", listing(loop, "nova"))
write("loop_ir.tex", listing(fn_text(res["ir"]["original"]["functions"][0])))
cfg = "\\begin{tabular}{@{}lllll@{}}\\toprule\n\\textbf{Block} & \\textbf{Labels} & \\textbf{Successors} & \\textbf{idom} & \\textbf{Loop header}\\\\ \\midrule\n"
for b in fa["blocks"]:
    cfg += f"B{b['id']} & {tex(', '.join(b['labels']) or '-')} & {tex(', '.join('B'+str(s) for s in b['succ']) or '-')} & {('B'+str(b['idom'])) if b['idom']>=0 else '-'} & {'yes' if b.get('loopHeader') else 'no'}\\\\\n"
cfg += "\\bottomrule\n\\end{tabular}\n"
write("loop_cfg.tex", cfg)
lv = fa["liveness"]
t = "\\begin{tabular}{@{}lllll@{}}\\toprule\n\\textbf{Block} & \\textbf{USE} & \\textbf{DEF} & \\textbf{IN} & \\textbf{OUT}\\\\ \\midrule\n"
fmt = lambda xs: "\\{" + ", ".join(tex(x) for x in xs) + "\\}" if xs else "$\\emptyset$"
for i in range(len(fa["blocks"])):
    t += f"B{i} & {fmt(lv['use'][i])} & {fmt(lv['def'][i])} & {fmt(lv['in'][i])} & {fmt(lv['out'][i])}\\\\\n"
t += "\\bottomrule\n\\end{tabular}\n"
write("loop_liveness.tex", t)
macro("LoopLivenessIters", lv["iterations"])
ssa = fa["ssa"]
ssat = ""
for b in ssa["blocks"]:
    ssat += f"B{b['id']}:\n"
    for ph in b.get("phis", []):
        ssat += f"    {ph['text']}\n"
    for c in b["code"]:
        ssat += f"    {c}\n"
write("loop_ssa.tex", listing(ssat))
macro("LoopPhis", ssa["phiCount"])

# ------------------------------------------------------------------------------------------------- 11. native: assembly + regalloc compare
fibsrc = open(os.path.join(ROOT, "examples/03_fibonacci.nova")).read()
asm_on = sh([NOVAC, "asm"], fibsrc)[0]
asm_off = sh([NOVAC, "asm", "--no-regalloc"], fibsrc)[0]


def stats(a):
    ins = [l for l in a.splitlines() if l.startswith("    ") and not l.lstrip().startswith(".")]
    return len(ins), sum(1 for l in ins if "(%rbp)" in l), sum(1 for l in ins if any(r in l for r in ("%rbx", "%r12", "%r13", "%r14", "%r15")))


a_on, a_off = stats(asm_on), stats(asm_off)
macro("AsmInstrOn", a_on[0])
macro("AsmInstrOff", a_off[0])
macro("AsmMemOn", a_on[1])
macro("AsmMemOff", a_off[1])
macro("AsmRegUses", a_on[2])
macro("AsmMemCut", round(100 * (1 - a_on[1] / a_off[1])))
fn = asm_on.split("nova_fn_fib_rec:")[1].split(".size")[0]
write("asm_snippet.tex", listing("nova_fn_fib_rec:" + "\n".join(fn.splitlines()[:46]), "", "basicstyle=\\ttfamily\\scriptsize"))
write("asm_snippet_off.tex", listing("nova_fn_fib_rec:" + "\n".join(asm_off.split("nova_fn_fib_rec:")[1].split(".size")[0].splitlines()[:24]), "", "basicstyle=\\ttfamily\\scriptsize"))
arr = open(os.path.join(ROOT, "examples/09_arrays.nova")).read()
macro("ArrExampleAsmLines", len(sh([NOVAC, "asm"], arr)[0].splitlines()))

# ------------------------------------------------------------------------------------------------- 12. SQL demo
sql = open(os.path.join(ROOT, "examples/sql/01_orders_report.sql")).read()
res = json.loads(sh([NOVAC, "sql"], sql)[0])
sel = [s for s in res["statements"] if s["kind"] == "SELECT"][0]
write("sql_script.tex", listing(sql, "sql"))
write("sql_rows.tex", listing(json.dumps(sel["columns"]) + "\n" + "\n".join(json.dumps(r) for r in sel["rows"])))
write("sql_plan_before.tex", listing(sel["plan"]["original"]))
write("sql_plan_after.tex", listing(sel["plan"]["optimizedWithStats"]))
t = "\\begin{longtable}{@{}lp{0.72\\textwidth}@{}}\\toprule\n\\textbf{Rule} & \\textbf{Effect}\\\\ \\midrule\\endhead\n"
for h in sel["plan"]["history"]:
    t += f"\\texttt{{{tex(h['rule'])}}} & {tex(h['description'])}\\\\\n"
t += "\\bottomrule\n\\end{longtable}\n"
write("sql_history.tex", t)
cmp_sql = "CREATE TABLE a (id INT, v INT);\nINSERT INTO a VALUES (1,10),(2,20);\nSELECT CASE WHEN v > 10 THEN 'hi' ELSE 'lo' END, v * 2 + 1 FROM a WHERE v BETWEEN 5 AND 50;"
res2 = json.loads(sh([NOVAC, "sql"], cmp_sql)[0])
s2 = res2["statements"][-1]
bc = ""
for b in s2["bytecode"]:
    bc += f"{b['source']}\n" + "\n".join("    " + c for c in b["code"]) + "\n"
write("sql_bytecode.tex", listing(bc))
write("sql_bytecode_src.tex", listing(cmp_sql, "sql"))
null_exprs = ["NULL AND 0", "NULL AND 1", "NULL OR 1", "NULL OR 0", "NOT NULL", "NULL = NULL", "1 IN (2, NULL)", "1 IN (1, NULL)", "2 NOT IN (1, NULL)", "7 / 2", "-7 / 2", "7 / 0", "COALESCE(NULL, 7)"]
res3 = json.loads(sh([NOVAC, "sql"], "SELECT " + ", ".join(null_exprs) + ";")[0])["statements"][0]
t = "\\begin{tabular}{@{}llp{0.04\\textwidth}ll@{}}\\toprule\n\\textbf{Expression} & \\textbf{Result} & & \\textbf{Expression} & \\textbf{Result}\\\\ \\midrule\n"
show = lambda v: "NULL" if v is None else str(v)
half = (len(null_exprs) + 1) // 2
for i in range(half):
    a = f"\\texttt{{{tex(null_exprs[i])}}} & \\texttt{{{show(res3['rows'][0][i])}}}"
    b = f"\\texttt{{{tex(null_exprs[i + half])}}} & \\texttt{{{show(res3['rows'][0][i + half])}}}" if i + half < len(null_exprs) else " & "
    t += a + " & & " + b + "\\\\\n"
t += "\\bottomrule\n\\end{tabular}\n"
write("sql_nulls.tex", t)
terr = "CREATE TABLE users (id INT PRIMARY KEY, name TEXT NOT NULL, age INT);\nSELECT nme FROM users WHERE age = 'old';"
res4 = json.loads(sh([NOVAC, "sql"], terr)[0])
write("sql_errors.tex", listing("\n".join(f"{d['code']} {d['loc']['line']}:{d['loc']['col']}  {d['message']}\n      hint: {d['suggestion']}" for d in res4["diagnostics"])))

# ------------------------------------------------------------------------------------------------- 13. assistant demos
sys.path.insert(0, os.path.join(ROOT, "backend"))
os.chdir(os.path.join(ROOT, "backend"))
from app import quickfix, ai_assist  # noqa: E402

e03 = open(os.path.join(ROOT, "examples/e03_type_mismatch.nova")).read()
af = quickfix.autofix(e03)
write("autofix_diff.tex", listing(af["diff"], "", "basicstyle=\\ttfamily\\scriptsize"))
write("autofix_steps.tex", "\\begin{enumerate}[nosep]\n" + "".join(f"\\item {tex(s['title'])} (line {s['line']}, {s['code']}: {s['errorsBefore']} $\\rightarrow$ {s['errorsAfter']} errors)\n" for s in af["steps"]) + "\\end{enumerate}\n")
macro("AutofixStatus", af["status"])
sug = quickfix.suggest(e03)
macro("SuggestFixes", len(sug["fixes"]))
macro("SuggestVerified", sum(1 for f in sug["fixes"] if f["verified"]))
status = {}
for name in ["e01_undefined_variable", "e02_missing_semicolon", "e03_type_mismatch", "e04_wrong_arguments", "e05_duplicate_and_return"]:
    r = quickfix.autofix(open(os.path.join(ROOT, f"examples/{name}.nova")).read())
    status[name] = (r["status"], len(r["steps"]), r["remainingErrors"])
t = "\\begin{tabular}{@{}lllr@{}}\\toprule\n\\textbf{Example} & \\textbf{Auto-fix result} & \\textbf{Steps} & \\textbf{Errors left}\\\\ \\midrule\n"
for k, (st, n, rem) in status.items():
    t += f"\\texttt{{{tex(k)}}} & {tex(st.replace('_', ' '))} & {n} & {rem}\\\\\n"
t += "\\bottomrule\n\\end{tabular}\n"
write("autofix_table.tex", t)
r01 = ai_assist.explain_runtime(open(os.path.join(ROOT, "examples/r01_division_by_zero.nova")).read())
write("runtime_explain.tex", listing("error      : " + r01["error"]["message"] + f" (line {r01['error']['line']} in {r01['function']}())\nsource     : " + r01["sourceLine"] + "\nvariables  : " + ", ".join(f"{k}={v}" for k, v in r01["variables"].items()) + "\nroot cause : " + r01["analysis"][0].replace("`", "") + "\nsuggestion : " + r01["suggestions"][0].replace("`", "")))
r02 = ai_assist.explain_runtime(open(os.path.join(ROOT, "examples/r02_stack_overflow.nova")).read())
write("runtime_explain2.tex", listing("error      : " + r02["error"]["message"] + "\nroot cause : " + r02["analysis"][0].replace("`", "") + "\nsuggestion : " + r02["suggestions"][0].replace("`", "")))
os.chdir(ROOT)

# ------------------------------------------------------------------------------------------------- 14. fresh verification runs (numbers are measured now)
t0 = time.time()
fz = json.loads(sh([NOVAC, "fuzz", "--count=2000", "--seed=424242"])[0])
macro("FuzzN", fz["requested"])
macro("FuzzPassed", fz["passed"])
macro("FuzzFailed", fz["failed"])
macro("FuzzInconclusive", fz["inconclusive"])
macro("FuzzIRBefore", f"{fz['irInstructionsBefore']:,}".replace(",", "{,}"))
macro("FuzzIRAfter", f"{fz['irInstructionsAfter']:,}".replace(",", "{,}"))
macro("FuzzStepsBefore", f"{fz['vmStepsBefore']:,}".replace(",", "{,}"))
macro("FuzzStepsAfter", f"{fz['vmStepsAfter']:,}".replace(",", "{,}"))
macro("FuzzIRCut", round(100 * (1 - fz["irInstructionsAfter"] / fz["irInstructionsBefore"])))
macro("FuzzStepCut", round(100 * (1 - fz["vmStepsAfter"] / fz["vmStepsBefore"])))
iv = json.loads(sh([NOVAC, "fuzz", "--mode=invalid", "--count=1000", "--seed=7"])[0])
macro("InvalidN", iv["requested"])
macro("InvalidRejLex", iv["rejectedLexical"])
macro("InvalidRejSyn", iv["rejectedSyntax"])
macro("InvalidRejSem", iv["rejectedSemantic"])
macro("InvalidAccepted", iv["acceptedMutants"])
macro("InvalidCrashes", iv["crashes"])
macro("InvalidBadDiag", iv["malformedDiagnostics"])
nd, _, _ = sh([os.path.join(ROOT, "scripts", "native_diff.sh"), "200"])
m = re.search(r"pass=(\d+) fail=(\d+) skipped\(non-compiling\)=(\d+)", nd)
macro("NativeDiffPass", m.group(1))
macro("NativeDiffFail", m.group(2))
macro("NativeDiffSkipped", m.group(3))
sq, _, _ = sh([sys.executable, os.path.join(ROOT, "scripts", "sql_fuzz.py"), "--count", "3000", "--seed", "555"])
m = re.search(r"pass=(\d+) fail=(\d+) skipped=(\d+)", sq)
macro("SQLFuzzPass", m.group(1))
macro("SQLFuzzFail", m.group(2))
macro("SQLFuzzSkipped", m.group(3))
macro("SQLFuzzTotal", int(m.group(1)) + int(m.group(2)) + int(m.group(3)))
mu, _, _ = sh([sys.executable, os.path.join(ROOT, "scripts", "sql_fuzz.py"), "--mutate", "--count", "1000", "--seed", "11"])
m = re.search(r"(\d+) garbled scripts, crashes/malformed=(\d+)", mu)
macro("SQLMutN", m.group(1))
macro("SQLMutBad", m.group(2))

# ------------------------------------------------------------------------------------------------- 15. benchmarks
bout, _, _ = sh([os.path.join(ROOT, "scripts", "bench.sh")])
rows1 = re.findall(r"^(fib35|primes)\s+(\d+)\s+(\d+)\s+(\d+)\s+([\d.]+)x\s+([\d.]+)x", bout, re.M)
row2 = re.search(r"^inline\s+(\d+)\s+(\d+)\s+n/a\s+([\d.]+)x", bout, re.M)
bench = {r[0]: dict(noreg=int(r[1]), reg=int(r[2]), py=int(r[3]), vsnoreg=r[4], vspy=r[5]) for r in rows1}
t = "\\begin{tabular}{@{}lrrrrr@{}}\\toprule\n\\textbf{Benchmark} & \\textbf{CPython (ms)} & \\textbf{Native, no regalloc (ms)} & \\textbf{Native, regalloc (ms)} & \\textbf{vs.\\ CPython} & \\textbf{regalloc gain}\\\\ \\midrule\n"
for k, v in bench.items():
    t += f"\\texttt{{{k}}} & {v['py']} & {v['noreg']} & {v['reg']} & {v['vspy']}$\\times$ & {v['vsnoreg']}$\\times$\\\\\n"
t += "\\bottomrule\n\\end{tabular}\n"
write("bench_table.tex", t)
for k, v in bench.items():
    for f, val in v.items():
        macro(f"Bench{ {'fib35': 'Fib', 'primes': 'Primes'}[k] }{f.capitalize()}", val)
macro("BenchInlineOff", row2.group(1))
macro("BenchInlineOn", row2.group(2))
macro("BenchInlineGain", row2.group(3))
write("bench_raw.tex", listing(bout))

# ------------------------------------------------------------------------------------------------- 16. LSP demo
frame = lambda m: (lambda b: b"Content-Length: " + str(len(b)).encode() + b"\r\n\r\n" + b)(json.dumps(m).encode())
src = "fn add(a: int, b: int) -> int {\n  return a + b;\n}\nfn main() {\n  let x: int = add(1, 2.5);\n  print(x);\n}\n"
msgs = [{"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {"uri": "file:///demo.nova", "version": 1, "text": src}}},
        {"jsonrpc": "2.0", "id": 2, "method": "textDocument/hover", "params": {"textDocument": {"uri": "file:///demo.nova"}, "position": {"line": 4, "character": 17}}},
        {"jsonrpc": "2.0", "id": 3, "method": "textDocument/definition", "params": {"textDocument": {"uri": "file:///demo.nova"}, "position": {"line": 4, "character": 17}}},
        {"jsonrpc": "2.0", "id": 4, "method": "shutdown"}, {"jsonrpc": "2.0", "method": "exit"}]
o, _, _ = sh([os.path.join(ROOT, "scripts", "nova-lsp.sh")], b"".join(frame(m) for m in msgs))
ob = o.encode() if isinstance(o, str) else o
i, outm = 0, []
while i < len(ob):
    h = ob.index(b"\r\n\r\n", i)
    n = int(ob[i:h].split(b":")[1])
    outm.append(json.loads(ob[h + 4:h + 4 + n]))
    i = h + 4 + n
lines = []
for m in outm:
    if m.get("method") == "textDocument/publishDiagnostics":
        for d in m["params"]["diagnostics"]:
            lines.append(f"diagnostic  line {d['range']['start']['line'] + 1}: [{d['code']}] {d['message'].splitlines()[0]}")
    elif m.get("id") == 2:
        lines.append("hover       " + m["result"]["contents"]["value"].replace("\n", " "))
    elif m.get("id") == 3:
        lines.append(f"definition  -> line {m['result']['range']['start']['line'] + 1}")
write("lsp_demo.tex", listing("\n".join(lines)))

# ------------------------------------------------------------------------------------------------- macros
t = "% AUTO-GENERATED by report/tools/gen_data.py -- do not edit\n"
for k, v in macros.items():
    t += f"\\newcommand{{\\{k}}}{{{v}}}\n"
write("numbers.tex", t)
print(f"done in {time.time() - t0:.0f}s (fuzz phase)")
