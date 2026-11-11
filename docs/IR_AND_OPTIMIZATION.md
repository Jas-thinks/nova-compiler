# Intermediate Representation and Optimization

## IR
Three-address code held as structured `IRInstr` objects (`compiler/include/nova/ir.hpp`), not strings. Text is derived on demand.

| Kind | Text form | Fields |
|---|---|---|
| Copy | `x = a` | dst, a |
| Bin | `t1 = a + b` | dst, op, a, b |
| Un | `t1 = -a`, `!a`, `to_float(a)`, `to_int(a)` | dst, op, a |
| Label / Jump | `L1:` / `goto L1` | target |
| JumpIfFalse / JumpIfTrue | `if_false t goto L2` | a, target |
| Call | `t3 = call f(a, b)` / `call f(a)` | dst?, callee, args |
| Return / Print | `return x` / `print x` | a? |

Operands are `Const` (typed value), `Var` (source variable, uniquified per function: `x`, `x.1`, …) or `Temp` (`t1`, …). Every instruction carries the source line/column that produced it, and `IRInstr::explain()` produces a sentence per instruction that the website shows. `&&` / `||` lower to short-circuit jumps; `if`/`while` lower to labels and conditional jumps.

Example (`let result: int = 10 + 20 * 2; print(result);`):
```
t1 = 20 * 2
t2 = 10 + t1
result = t2
print result
```

## Optimizer (`compiler/src/optimizer/optimizer.cpp`)
Six independently switchable passes run per function, in this order, **to a fixed point** (max 12 rounds):

1. **Constant & copy propagation** – block-local environment (`var → const | var`), reset at labels and invalidated on redefinition; plus function-wide propagation for variables assigned exactly once to a constant (sound because NOVA requires declaration before use).
2. **Constant folding** – evaluates `Bin`/`Un` on constants using the *same* `evalBin`/`evalUn` the VM uses; folds `if_false/if_true` on constants into `goto` or deletes them. Integer division/modulo by zero is **not** folded.
3. **Algebraic simplification** – `x+0, 0+x, x-0, x*1, 1*x, x/1, x*0 (int), x-x (int), x%1 (int)`, strength reduction `x*2 → x+x`, reflexive int comparisons. Float rules are restricted (e.g. `x+0.0` is not simplified because of `-0.0`; `x*0.0` never is, because of NaN/inf).
4. **Common subexpression elimination** – local value numbering with commutative normalisation; entries die when an operand or the holder is reassigned, and at labels.
5. **Unreachable-code elimination** – builds basic blocks and a CFG, marks blocks reachable from entry, deletes the rest; then removes jumps to the next instruction and unreferenced labels.
6. **Dead-code elimination** – iterative backward **live-variable analysis** over the CFG; removes pure instructions whose destination is not live. Integer `/` and `%` with a possibly-zero divisor are *not* removable (they may trap). Calls and prints are never removed.

Every rewrite appends an `OptRecord{pass, function, before, after, explanation, line, col, round}`. `OptResult` also holds per-pass counts and before/after instruction counts; the Optimization Lab displays exactly these.

## Correctness checking
* Each pass is tested in isolation (`optimizer.*` unit tests) and all combinations are checked against the reference interpreter in `optimizer.eachPassPreservesSemanticsOnExamples`.
* **Differential fuzzing** (`novac fuzz`): random valid programs are run through (a) a reference AST interpreter that shares nothing with IR/optimizer/VM except the primitive arithmetic helper `value.hpp`, (b) the unoptimized VM, and (c) the optimized VM. Output *and* runtime error text must be identical. On the development machine 3000 programs with all passes, and 800 with each of several pass subsets, showed zero mismatches (re-run via `./scripts/test.sh` or the Compiler Tests page for current numbers).
* The shared arithmetic helper means a bug in primitive semantics (e.g. wrap-around) would be common to all three engines; it is covered by direct unit tests.
