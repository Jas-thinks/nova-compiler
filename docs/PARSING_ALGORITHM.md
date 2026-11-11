# Parsing Algorithm — Canonical LR(1)

NOVA uses a genuine **table-driven canonical LR(1)** parser. Everything is implemented in `compiler/src/parser/` and runs on every compilation (the tables are built once per process start; ≈10 ms).

## 1. Grammar representation (`grammar.cpp`)
`Grammar::fromText` reads the textual productions, interns symbols (terminals vs nonterminals), and adds the augmented production `S' → Program`. Productions are `{lhs, rhs[]}`.

## 2. FIRST / FOLLOW (`Grammar::computeSets`)
Fixed-point iteration computes `nullable`, `FIRST` for all symbols and `FOLLOW` for nonterminals. `firstOfSeq(β, a)` returns `FIRST(β a)` and is the workhorse of closure. (`FOLLOW` is not needed by canonical LR(1) itself, but is computed and displayed for teaching.)

## 3. Items, closure, goto (`lr1.cpp`)
An LR(1) item `[A → α • β, a]` is packed into 32 bits (`production<<16 | dot<<8 | lookahead`).

* **closure(I):** for every `[A → α • B β, a]` and every production `B → γ`, add `[B → • γ, b]` for each `b ∈ FIRST(β a)`; repeat until no change.
* **goto(I, X):** move the dot over `X` in every item that has `• X`, then take the closure. States are identified by their *kernel* (the sorted item vector).
* The **canonical collection** is built by a worklist starting from `closure({[S' → • Program, $]})`.

## 4. ACTION / GOTO tables
For each state:
* `[A → α • a β, b]` with terminal `a` and `goto(I, a) = J` → `ACTION[I, a] = shift J`
* `[A → α •, a]`, `A ≠ S'` → `ACTION[I, a] = reduce (A → α)`
* `[S' → Program •, $]` → `ACTION[I, $] = accept`
* `goto(I, A) = J` for nonterminal `A` → `GOTO[I, A] = J`

Whenever a cell would receive two different entries a **conflict** is recorded. The NOVA grammar produces none (241 states; 662 shift / 1209 reduce / 410 goto entries). This is enforced by a unit test and displayed on the dashboard and Syntax page.

## 5. The driver (`parser.cpp`)
A stack of `(state, parse-node)` pairs. Loop: read `ACTION[top.state, lookahead]`:

| Action | Effect |
|---|---|
| shift *s* | push the token and state *s*, advance the input |
| reduce *p* | pop \|rhs\| entries, build a parse node for `lhs`, push it with `GOTO[newTop, lhs]` |
| accept | return the parse tree |
| error | produce a diagnostic and stop |

Every step is recorded (stack contents, upcoming input, action, explanation) and returned to the website's trace viewer. The trace is capped at 4000 steps (the cap is reported); counters are exact regardless.

## 6. Syntax error reporting
The expected terminals are exactly those with a non-empty `ACTION` cell in the failing state. The message is specialised: missing `;` / `)` / `}` are reported at the *end of the previous token* (where the fix belongs), end-of-file inside a construct, "expected an expression" when operands are expected, otherwise "unexpected X; expected …". **Limitation:** the parser halts at the first syntax error; there is no panic-mode recovery.

## 7. Why LR(1) and not LALR(1)?
Canonical LR(1) is the most general deterministic bottom-up method, and for a 73-production grammar the table has only 241 states, so there is no need for LALR state merging. (A smaller Pager/LALR table would be a possible extension.)
