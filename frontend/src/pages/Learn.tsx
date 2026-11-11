import { useState, type ReactNode } from 'react'
import { ArrowLeft, ArrowRight } from 'lucide-react'
import Gate from '../components/Gate'
import { Chip, Empty, Panel, SourceView } from '../components/ui'
import TokenTable from '../features/lexer/TokenTable'
import { AstOutline } from '../features/ast/AstTree'
import { SymbolTable } from '../features/semantic/SymbolTables'
import IRListing from '../features/ir/IRListing'
import BytecodeListing from '../features/bytecode/BytecodeListing'
import { useNova } from '../state/NovaContext'
import { PASS_TITLES } from '../lib/format'
import type { CompileResponse, ParseNode } from '../types/api'

const countTree = (n: ParseNode): number => 1 + n.children.reduce((s, c) => s + countTree(c), 0)

interface LearnStage {
  id: string
  title: string
  what: string
  why: string
  algorithm: string
  handoff: string
  input: (r: CompileResponse, src: string) => ReactNode
  output: (r: CompileResponse) => ReactNode
  facts: (r: CompileResponse) => string[]
}

const STAGES: LearnStage[] = [
  {
    id: 'lexer', title: 'Lexical analysis',
    what: 'Splits the raw character stream into tokens: keywords, identifiers, literals, operators and punctuation, each with its line and column.',
    why: 'The parser works on a small alphabet of tokens, not characters. Scanning also removes whitespace and comments and detects malformed literals early.',
    algorithm: 'A hand-written deterministic scanner (equivalent to a DFA) using longest match: identifiers are checked against the keyword table, "=" vs "==" and "-" vs "->" are resolved by one-character lookahead.',
    handoff: 'The token list (ending with an EOF marker $) is the input stream of the LR(1) parser.',
    input: (_r, src) => <SourceView source={src} className="max-h-64 overflow-auto" />,
    output: (r) => <div className="max-h-64 overflow-auto"><TokenTable tokens={r.tokens} limit={40} /></div>,
    facts: (r) => [`${r.tokens.length - 1} tokens produced in ${r.stages[0].durationMs.toFixed(2)} ms`, `${r.tokens.filter((t) => t.category === 'KEYWORD').length} keywords, ${r.tokens.filter((t) => t.category === 'IDENTIFIER').length} identifiers`],
  },
  {
    id: 'parser', title: 'Syntax analysis (canonical LR(1))',
    what: 'Checks that the token sequence is a sentence of the NOVA grammar and builds the parse tree by repeatedly shifting tokens and reducing them by grammar productions.',
    why: 'Token order carries the program structure (precedence, nesting, statements). Syntax errors are caught here with the exact position of the problem.',
    algorithm: 'Canonical LR(1): the compiler builds all LR(1) item sets (closure + goto), derives ACTION/GOTO tables, and runs a stack machine: look up ACTION[state, lookahead] → shift, reduce, accept or error.',
    handoff: 'The accepted parse tree is converted into an abstract syntax tree.',
    input: (r) => <div className="p-3 code text-fg-dim max-h-64 overflow-auto">{r.tokens.map((t) => t.lexeme || '$').join(' ')}</div>,
    output: (r) => (
      <div className="max-h-64 overflow-auto"><table className="w-full code"><thead><tr><th className="th">#</th><th className="th">Action</th><th className="th">Lookahead</th></tr></thead>
        <tbody>{r.parser.trace.slice(0, 18).map((s) => <tr key={s.step}><td className="td text-fg-mute">{s.step}</td><td className="td">{s.action}</td><td className="td text-fg-dim">{s.lookahead}</td></tr>)}</tbody></table></div>),
    facts: (r) => [`${r.parser.states} LR(1) states, no conflicts`, `${r.parser.totalSteps} parser steps: ${r.parser.shifts} shifts, ${r.parser.reduces} reductions`, `parse tree has ${r.parser.tree ? countTree(r.parser.tree) : 0} nodes`],
  },
  {
    id: 'ast', title: 'Abstract syntax tree',
    what: 'Turns the verbose parse tree into a compact typed tree: Function, Block, VarDecl, Binary, Call… Each node keeps its source span.',
    why: 'Later phases need program meaning, not grammar details. Operator precedence and associativity are already encoded in the tree shape.',
    algorithm: 'A structural walk over the parse tree; the grammar is unambiguous so each production maps to exactly one AST node kind.',
    handoff: 'The AST is the input to semantic analysis, which annotates it with types and symbols.',
    input: (r) => <div className="p-3 text-fg-dim">Parse tree with {r.parser.tree ? countTree(r.parser.tree) : '?'} nodes</div>,
    output: (r) => r.ast ? <div className="max-h-64 overflow-auto"><AstOutline node={r.ast} onSelect={() => undefined} /></div> : null,
    facts: (r) => [`${r.stages[2].summary.nodes} AST nodes, ${r.stages[2].summary.functions} function(s)`],
  },
  {
    id: 'semantic', title: 'Semantic analysis',
    what: 'Resolves every name to its declaration through nested scopes and type-checks every expression, assignment, call and return.',
    why: 'A grammatically valid program can still be meaningless (undefined variables, int + bool). These errors cannot be seen by a context-free grammar.',
    algorithm: 'Scope stack + symbol tables; bottom-up type inference with strict rules (no implicit conversions); all-paths-return analysis; edit-distance suggestions for misspelled names.',
    handoff: 'The type- and symbol-annotated AST (guaranteed well-typed) is lowered to IR.',
    input: (r) => <div className="p-3 text-fg-dim">AST with {r.stages[2].summary.nodes} nodes</div>,
    output: (r) => r.semantic ? <div className="max-h-64 overflow-auto">{r.semantic.scopes.filter((s) => s.symbols.length).map((s) => <div key={s.id}><div className="px-3 pt-2 text-2xs uppercase tracking-wider text-fg-mute">scope #{s.id} · {s.kind} {s.owner}</div><SymbolTable scope={s} /></div>)}</div> : null,
    facts: (r) => [`${r.stages[3].summary.scopes} scopes, ${r.stages[3].summary.symbols} symbols`, `${r.diagnostics.filter((d) => d.phase === 'semantic' && d.severity === 'warning').length} warning(s)`],
  },
  {
    id: 'ir', title: 'Intermediate representation (3-address code)',
    what: 'Lowers the AST to a flat list of simple instructions with at most one operator, using temporaries (t1, t2, …), labels and jumps.',
    why: 'IR is independent of both the source language and the target. It makes control flow explicit and is the right level for optimization.',
    algorithm: 'Post-order expression walk emitting one instruction per operator; if/while become labels and conditional jumps; && and || become short-circuit jumps.',
    handoff: 'The IR is optimized, then translated to bytecode.',
    input: (r) => <div className="p-3 text-fg-dim">Annotated AST ({r.stages[2].summary.nodes} nodes)</div>,
    output: (r) => r.ir ? <div className="max-h-64 overflow-auto"><IRListing program={r.ir.original} showExplain={false} /></div> : null,
    facts: (r) => [`${r.ir?.original.instructionCount} IR instructions in ${r.ir?.original.functions.length} function(s)`],
  },
  {
    id: 'optimizer', title: 'Optimization',
    what: 'Rewrites the IR into an equivalent but cheaper program using constant propagation/folding, algebraic identities, CSE, unreachable-code and dead-code elimination.',
    why: 'Removes work that can be done at compile time or never matters at run time, without changing what the program prints.',
    algorithm: 'Passes run to a fixed point. Dataflow analyses include block-local copy/constant maps, value numbering, CFG reachability and backward live-variable analysis.',
    handoff: 'The optimized IR is the input of code generation. Every rewrite is logged with its explanation.',
    input: (r) => r.ir ? <div className="max-h-64 overflow-auto"><IRListing program={r.ir.original} showExplain={false} /></div> : null,
    output: (r) => r.ir ? <div className="max-h-64 overflow-auto"><IRListing program={r.ir.optimized} showExplain={false} /></div> : null,
    facts: (r) => r.optimization ? [`${r.optimization.instructionsBefore} → ${r.optimization.instructionsAfter} IR instructions`, `${r.optimization.history.length} transformations in ${r.optimization.rounds} round(s)`, ...Object.entries(r.optimization.perPass).map(([k, v]) => `${PASS_TITLES[k] ?? k}: ${v}`)] : [],
  },
  {
    id: 'codegen', title: 'Code generation (NOVA VM bytecode)',
    what: 'Translates each IR instruction into stack-machine bytecode: operands are pushed, the operation pops them and pushes a result, and variables live in numbered local slots.',
    why: 'The VM executes bytecode directly. (NOVA targets its own virtual machine — this is not native machine code.)',
    algorithm: 'Linear emission per function with label fix-up for jumps; each instruction records its source line for debugging.',
    handoff: 'The bytecode program is executed by the VM, which the debugger can single-step.',
    input: (r) => r.ir ? <div className="max-h-64 overflow-auto"><IRListing program={r.ir.optimized} showExplain={false} /></div> : null,
    output: (r) => r.bytecode ? <div className="max-h-64 overflow-auto"><BytecodeListing bytecode={r.bytecode.optimized} showDesc={false} /></div> : null,
    facts: (r) => r.bytecode ? [`${r.bytecode.optimized.count} bytecode instructions (${r.bytecode.baseline.count} without optimization)`] : [],
  },
]

export default function Learn() {
  const [i, setI] = useState(0)
  const { resultSource, exec } = useNova()
  const last = i === STAGES.length
  return (
    <div className="max-w-6xl">
      <div className="flex items-center gap-2 mb-3 flex-wrap">
        <button className="btn" disabled={i === 0} onClick={() => setI(i - 1)}><ArrowLeft size={14} /> Previous stage</button>
        <div className="flex gap-1 flex-1 justify-center">{[...STAGES.map((s) => s.title), 'Execution'].map((t, k) => <button key={t} title={t} onClick={() => setI(k)} className={`h-2 rounded-full transition-all ${k === i ? 'w-10 bg-nova' : 'w-5 bg-ink-600 hover:bg-ink-500'}`} />)}</div>
        <button className="btn btn-primary" disabled={last} onClick={() => setI(i + 1)}>Next stage <ArrowRight size={14} /></button>
      </div>
      <Gate need={(r) => r.success || STAGES[Math.min(i, STAGES.length - 1)].id === 'lexer' || r.stages.find((s) => s.id === STAGES[Math.min(i, STAGES.length - 1)].id)?.status === 'ok'}>
        {(r) => {
          if (last) {
            const ex = exec?.execution
            return (
              <div className="grid gap-3">
                <h2 className="text-lg font-semibold">8 · Execution on the NOVA virtual machine</h2>
                <div className="grid md:grid-cols-2 gap-3 text-fg-dim">
                  <Panel title="What this stage does" bodyClass="p-3">The VM fetches the instruction at the instruction pointer, pops its operands from the operand stack, pushes the result, and moves on. CALL creates a frame holding the callee's local slots; RET destroys it. Runtime errors (e.g. division by zero, stack overflow) stop the machine.</Panel>
                  <Panel title="Why it is needed" bodyClass="p-3">Compilation only produces a program; the VM is what gives it behaviour. Running both the unoptimized and optimized bytecode lets us verify the optimizer preserved semantics.</Panel>
                </div>
                <Panel title="Actual output" bodyClass="p-3">
                  {ex ? (<div className="code space-y-1">{exec.output.map((l, k) => <div key={k}>{l}</div>)}{exec.error && <div className="text-bad">⚠ {exec.error.message}</div>}
                    <div className="pt-2 font-sans flex gap-2 flex-wrap"><Chip>{ex.optimized.steps} instrs executed (optimized)</Chip><Chip>{ex.baseline.steps} (unoptimized)</Chip><Chip tone={ex.optimizationPreservesBehavior ? 'ok' : 'bad'}>outputs identical: {String(ex.optimizationPreservesBehavior)}</Chip></div></div>)
                    : <Empty title="Not executed yet" hint="Press Run in the top bar to execute this program on the VM; the real output will appear here." />}
                </Panel>
              </div>
            )
          }
          const s = STAGES[i]
          const st = r.stages.find((x) => x.id === s.id)
          const ok = st?.status === 'ok'
          return (
            <div className="grid gap-3">
              <div><div className="text-2xs uppercase tracking-wider text-nova">Stage {i + 1} of {STAGES.length + 1}</div><h2 className="text-lg font-semibold">{s.title}</h2></div>
              <div className="grid md:grid-cols-2 gap-3 text-fg-dim">
                <Panel title="What it does" bodyClass="p-3">{s.what}</Panel>
                <Panel title="Why it is required" bodyClass="p-3">{s.why}</Panel>
                <Panel title="Algorithm used" bodyClass="p-3 md:col-span-2">{s.algorithm}</Panel>
              </div>
              {!ok ? <Panel title="Result" bodyClass="p-3 text-bad">This stage {st?.status === 'error' ? 'reported errors' : 'was skipped'} for the current program: {r.diagnostics.filter((d) => d.severity === 'error').map((d) => d.message).join('; ')}. Fix them to continue through the pipeline.</Panel> : (
                <>
                  <div className="grid md:grid-cols-2 gap-3">
                    <Panel title="Input received">{s.input(r, resultSource ?? '')}</Panel>
                    <Panel title="Actual output produced">{s.output(r)}</Panel>
                  </div>
                  <Panel title="Measured facts" bodyClass="p-3 flex gap-2 flex-wrap">{s.facts(r).map((f) => <Chip key={f} tone="nova">{f}</Chip>)}<Chip>{st!.durationMs.toFixed(2)} ms</Chip></Panel>
                </>
              )}
              <Panel title="How the output is passed on" bodyClass="p-3 text-fg-dim">{s.handoff}</Panel>
            </div>
          )
        }}
      </Gate>
    </div>
  )
}
