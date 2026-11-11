# Advanced Analysis (CFG, data-flow, dominators, SSA)

Implemented in `compiler/src/analysis/analysis.cpp` (header `analysis.hpp`), exposed as `analysis.original[]` / `analysis.optimized[]` (one entry per function) in the compile response, and visualised on the **CFG & Data-flow** page.

## Control-flow graph
* **Leaders:** first instruction; every label; every instruction after a jump / conditional jump / return.
* **Blocks:** maximal runs from a leader to the next leader. Each block records its labels, instructions (with source lines), predecessors and successors.
* **Edges:** `fall`, `jump`, `true`, `false` (conditional jumps produce a `false`/`true` pair).
* **Reachability + reverse post-order** by DFS from the entry; unreachable blocks are flagged (e.g. code after `return`).
* **Dominators:** Cooper–Harvey–Kennedy iterative algorithm over RPO → immediate dominator per block.
* **Loops:** an edge `u → v` is a **back edge** if `v` dominates `u`; `v` is a *loop header*. The UI draws back edges dashed/animated.
* **Source mapping:** every instruction keeps its line; selecting a block highlights its source range.

## Iterative data-flow analyses
Both run a work-until-no-change fixed-point loop (max 200 passes) and record **every iteration's IN/OUT sets**, shown with a stepper and change highlighting.

| Analysis | Direction | Sets | Equations |
|---|---|---|---|
| Liveness | backward | USE, DEF | `IN = USE ∪ (OUT − DEF)`, `OUT = ⋃ IN[succ]` |
| Reaching definitions | forward | GEN, KILL (definitions named `dN`, params `p:x`) | `OUT = GEN ∪ (IN − KILL)`, `IN = ⋃ OUT[pred]` |

(The optimizer's DCE uses its own liveness computation inside `optimizer.cpp`; the analysis module is a separate, display-oriented implementation. Both are tested; unifying them is future work.)

## SSA (pruned, Cytron et al.)
1. Definition sites per variable (parameters are defined in the entry block).
2. Dominance frontiers.
3. φ placement on the iterated dominance frontier of each variable's definition blocks, **pruned** — a φ is inserted only where the variable is live-in (uses the liveness result).
4. Renaming by a pre-order walk of the dominator tree with per-variable version stacks; φ operands are filled from each predecessor's current version.

Names look like `i#0, i#1`; φ text: `i#1 = φ(i#0 [B0], i#2 [B5])`. **Limits:** SSA is produced for visualisation/analysis; it is *not* fed back into code generation, there is no SSA-based optimization or out-of-SSA translation, and temporaries from `&&`/`||` lowering (which are multiply assigned) are handled like ordinary variables.

## Verification
`compiler/tests/test_compiler.cpp` suites `cfg`, `dataflow`, `ssa`, `analysis`: diamond/loop shapes and edge kinds, back-edge/loop-header detection, block boundaries tile the instruction list exactly, unreachable blocks, liveness at loop headers and the fixed-point equation `OUT = ⋃ IN[succ]`, reaching defs at an `if` join (exactly the two assignments reach, the initial one is killed), the reaching-defs equation, φ placement at joins and loop headers, no φ without a merge, and an SSA validity property (each name defined once, every use has a definition) checked on 150 random programs both unoptimized and optimized. Backend tests check the JSON contract.
