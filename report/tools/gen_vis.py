#!/usr/bin/env python3
"""Generate the data-driven figures of the Visualisation chapter from REAL compiler output (writes report/gen/vis_*.tex)."""
import json, os, subprocess, collections

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GEN = os.path.join(ROOT, "report", "gen")
NOVAC = os.path.join(ROOT, "compiler", "build", "novac")


def run(args, src):
    return json.loads(subprocess.run([NOVAC] + args, input=src.encode(), capture_output=True).stdout)


def write(name, text):
    open(os.path.join(GEN, name), "w").write(text)
    print("wrote", name)


fib = open(os.path.join(ROOT, "examples/03_fibonacci.nova")).read()
res = run(["compile", "--run"], fib)

# 1. parse-stack depth over the LR(1) trace
tr = res["parser"]["trace"]
pts = [(s["step"], (len(s["stack"]) + 1) // 2) for s in tr]
pts = pts[::3] + [pts[-1]]
coords_ = " ".join(f"({a},{b})" for a, b in pts)
write("vis_fig_parse.tex", "\\begin{tikzpicture}\n\\begin{axis}[width=0.92\\textwidth,height=5.2cm,xlabel={parse step},ylabel={stack depth (symbols)},font=\\sffamily\\small,axis x line*=bottom,axis y line*=left,xmin=0,ymin=0,grid=major,grid style={dotted,grey!50}]\n\\addplot[nova,thick,const plot] coordinates {" + coords_ + "};\n\\end{axis}\n\\end{tikzpicture}\n")
write("vis_parse_meta.tex", f"\\newcommand{{\\VisParseSteps}}{{{res['parser']['totalSteps']}}}\\newcommand{{\\VisParseMaxDepth}}{{{res['parser']['maxStackDepth']}}}\n")

# 2. VM opcode histogram (optimized program)
oc = sorted(res["execution"]["optimized"]["opCounts"].items(), key=lambda kv: -kv[1])[:9]
syms = ",".join(k for k, _ in reversed(oc))
coords = " ".join(f"({v},{k})" for k, v in reversed(oc))
total = sum(res["execution"]["optimized"]["opCounts"].values())
share = round(100 * sum(v for k, v in oc if k in ("LOAD", "STORE", "PUSH")) / total)
write("vis_fig_opcodes.tex", "\\begin{tikzpicture}\n\\begin{axis}[xbar,width=0.85\\textwidth,height=6.2cm,bar width=9pt,xmin=0,xmax=4000,symbolic y coords={" + syms + "},ytick=data,nodes near coords,axis x line*=bottom,axis y line*=left,xlabel={times executed},font=\\sffamily\\small,enlarge y limits=0.08]\n\\addplot[fill=nova!75,draw=none] coordinates {" + coords + "};\n\\end{axis}\n\\end{tikzpicture}\n")
write("vis_opcodes.tex", f"\\newcommand{{\\VisOpSyms}}{{{syms}}}\n\\newcommand{{\\VisOpCoords}}{{{coords}}}\n\\newcommand{{\\VisOpTotal}}{{{total:,}}}\n\\newcommand{{\\VisOpShare}}{{{share}}}\n".replace(",}", "{,}}") if False else
      f"\\newcommand{{\\VisOpSyms}}{{{syms}}}\n\\newcommand{{\\VisOpCoords}}{{{coords}}}\n\\newcommand{{\\VisOpTotal}}{{{total}}}\n\\newcommand{{\\VisOpShare}}{{{share}}}\n")

# 3. optimizer activity per round per pass (04_optimization_demo)
demo = open(os.path.join(ROOT, "examples/04_optimization_demo.nova")).read()
r2 = run(["compile"], demo)
hist = r2["optimization"]["history"]
rounds = sorted({h["round"] for h in hist})
passes = [p for p in ["constant_propagation", "constant_folding", "algebraic", "cse", "unreachable", "dce"] if any(h["pass"] == p for h in hist)]
cnt = collections.Counter((h["round"], h["pass"]) for h in hist)
lines = ""
colors = {"constant_propagation": "nova", "constant_folding": "teal", "algebraic": "amber", "cse": "crimson", "unreachable": "grey", "dce": "novadark"}
for p in passes:
    lines += f"\\addplot[fill={colors[p]}!80,draw=none] coordinates {{" + " ".join(f"({r},{cnt[(r, p)]})" for r in rounds) + "};\n"
write("vis_fig_rounds.tex", "\\begin{tikzpicture}\n\\begin{axis}[ybar stacked,width=0.9\\textwidth,height=6cm,bar width=22pt,xlabel={optimization round},ylabel={rewrites},xtick={" + ",".join(map(str, rounds)) + "},font=\\sffamily\\small,axis x line*=bottom,axis y line*=left,legend style={at={(1.02,0.5)},anchor=west,font=\\sffamily\\scriptsize},ymin=0,enlarge x limits=0.12]\n" + lines + "\\legend{" + ",".join(p.replace("_", " ") for p in passes) + "}\n\\end{axis}\n\\end{tikzpicture}\n")
write("vis_rounds_meta.tex", f"\\newcommand{{\\VisRounds}}{{{len(rounds)}}}\\newcommand{{\\VisRewrites}}{{{len(hist)}}}\\newcommand{{\\VisRoundXTicks}}{{{','.join(map(str, rounds))}}}\n")

# 4. live intervals + colours (graph-colouring visualiser, K=4) for fib_iter
fa = [f for f in res["analysis"]["original"] if f["name"] == "fib_iter"][0]
ra = fa["registerAllocation"]
nodes = sorted(ra["nodes"], key=lambda n: (n["first"], n["name"]))
pal = ["nova", "teal", "amber", "crimson", "novadark", "grey"]
tikz = ""
maxx = max(n["last"] for n in nodes)
for i, n in enumerate(nodes):
    y = -i * 0.62
    col = pal[n["color"] % len(pal)] if not n["spilled"] else "black"
    tikz += f"\\node[anchor=east,font=\\ttfamily\\scriptsize] at (-0.15,{y}) {{{n['name'].replace('_', chr(92) + '_')}}};\n"
    tikz += f"\\fill[{col}!75,rounded corners=1.5pt] ({n['first'] * 0.55},{y - 0.2}) rectangle ({n['last'] * 0.55 + 0.4},{y + 0.2});\n"
    tikz += f"\\node[anchor=west,font=\\sffamily\\tiny,color=grey] at ({n['last'] * 0.55 + 0.55},{y}) {{{'spilled' if n['spilled'] else 'colour ' + str(n['color'])}}};\n"
tikz += f"\\draw[->,thick] (0,{-len(nodes) * 0.62 + 0.2}) -- ({maxx * 0.55 + 1.2},{-len(nodes) * 0.62 + 0.2}) node[right,font=\\sffamily\\scriptsize]{{IR instruction index}};\n"
write("vis_intervals.tex", tikz)
write("vis_intervals_meta.tex", f"\\newcommand{{\\VisRaK}}{{{ra['k']}}}\\newcommand{{\\VisRaColors}}{{{ra['colorsUsed']}}}\\newcommand{{\\VisRaSpills}}{{{ra['spills']}}}\\newcommand{{\\VisRaMinK}}{{{ra['minimalK']}}}\\newcommand{{\\VisRaVars}}{{{len(nodes)}}}\n")

# 5. SQL operator rows (optimized plan with measured counts)
sql = open(os.path.join(ROOT, "examples/sql/01_orders_report.sql")).read()
sres = run(["sql"], sql)
sel = [s for s in sres["statements"] if s["kind"] == "SELECT"][0]
flat = []
def walk(n, d):
    flat.append((n["op"], n["rows"], d))
    for c in n["children"]:
        walk(c, d + 1)
walk(sel["plan"]["tree"], 0)
tik = ""
for i, (op, rows, d) in enumerate(flat):
    y = -i * 0.55
    tik += f"\\node[anchor=west,font=\\sffamily\\scriptsize] at ({d * 0.3},{y}) {{{op}}};\n"
    tik += f"\\fill[nova!70,rounded corners=1pt] (4.6,{y - 0.17}) rectangle ({4.6 + rows * 0.9},{y + 0.17});\n"
    tik += f"\\node[anchor=west,font=\\sffamily\\scriptsize] at ({4.7 + rows * 0.9},{y}) {{{rows}}};\n"
write("vis_sqlrows.tex", tik)
st = sel["stats"]
write("vis_sqlrows_meta.tex", f"\\newcommand{{\\VisSqlScanned}}{{{st['rowsScanned']}}}\\newcommand{{\\VisSqlPairs}}{{{st['joinPairsCompared']}}}\\newcommand{{\\VisSqlOps}}{{{len(flat)}}}\n")
