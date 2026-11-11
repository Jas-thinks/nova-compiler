import { useEffect, useState } from 'react'
import { PageHeader, Panel, Chip } from '../components/ui'
import { api } from '../services/api'
import { PASS_IDS, PASS_TITLES } from '../lib/format'
import type { GrammarInfo } from '../types/api'

const tabs = ['Language', 'Grammar', 'Bytecode', 'Optimizations', 'REST API', 'Architecture'] as const

const EXAMPLE = `fn gcd(a: int, b: int) -> int {
    while (b != 0) {
        let r: int = a % b;
        a = b;
        b = r;
    }
    return a;
}

fn main() {
    print(gcd(48, 18));   // 6
}`

export default function Docs() {
  const [tab, setTab] = useState<(typeof tabs)[number]>('Language')
  const [g, setG] = useState<GrammarInfo | null>(null)
  const [isa, setIsa] = useState<{ op: string; description: string }[]>([])
  useEffect(() => { api.grammar().then(setG).catch(() => undefined); api.isa().then(setIsa).catch(() => undefined) }, [])
  const P = ({ children }: { children: React.ReactNode }) => <p className="text-fg-dim leading-relaxed mb-2">{children}</p>
  return (
    <div className="max-w-5xl">
      <PageHeader title="Documentation" subtitle="Quick reference. The full documents live in the repository's docs/ folder." />
      <div className="flex gap-1 mb-3 border-b border-ink-700">{tabs.map((t) => <button key={t} onClick={() => setTab(t)} className={`px-3 h-8 -mb-px border-b-2 ${tab === t ? 'border-nova text-fg' : 'border-transparent text-fg-dim hover:text-fg'}`}>{t}</button>)}</div>

      {tab === 'Language' && (
        <div className="grid gap-3">
          <Panel title="NOVA at a glance" bodyClass="p-4">
            <P>NOVA is a small, statically typed, imperative language. A program is a list of functions; execution starts at <code className="code text-warn">fn main()</code>.</P>
            <ul className="list-disc pl-5 text-fg-dim space-y-1 mb-3">
              <li><b>Types:</b> <code className="code">int</code> (64-bit, wraps on overflow), <code className="code">float</code> (IEEE-754 double), <code className="code">bool</code>. No implicit conversions — use <code className="code">to_float(i)</code> and <code className="code">to_int(f)</code> (truncates).</li>
              <li><b>Statements:</b> <code className="code">let x: T = e;</code> · <code className="code">x = e;</code> · <code className="code">if (c) {'{…}'} else {'{…}'}</code> · <code className="code">while (c) {'{…}'}</code> · <code className="code">return e;</code> · <code className="code">print(e);</code> · call statements · nested blocks. Braces are mandatory.</li>
              <li><b>Operators</b> (high → low): unary <code className="code">- !</code>; <code className="code">* / %</code>; <code className="code">+ -</code>; <code className="code">&lt; &lt;= &gt; &gt;=</code>; <code className="code">== !=</code>; <code className="code">&amp;&amp;</code>; <code className="code">||</code>. All binary operators are left-associative; <code className="code">&amp;&amp;</code>/<code className="code">||</code> short-circuit.</li>
              <li><b>Functions:</b> <code className="code">fn name(p: T, …) -&gt; R {'{…}'}</code>; omit <code className="code">-&gt; R</code> for no result. Recursion and forward references are allowed. Non-void functions must return on every path.</li>
              <li><b>Scopes:</b> each block opens a scope; inner declarations may shadow outer ones but not redeclare within the same scope.</li>
              <li><b>Comments:</b> <code className="code">// line</code> and <code className="code">/* block */</code>.</li>
            </ul>
            <pre className="code bg-ink-850 border border-ink-700 rounded p-3 overflow-auto">{EXAMPLE}</pre>
          </Panel>
          <Panel title="Known limitations" bodyClass="p-4 text-fg-dim"><ul className="list-disc pl-5 space-y-1"><li>No strings, arrays, structs or heap — hence the runtime view shows only frames, slots and operand stacks.</li><li>The parser stops at the first syntax error (no error-recovery resynchronization).</li><li>Optimizations are intra-procedural; local analyses are per basic block except liveness, reachability and single-assignment constants.</li><li>The debugger replays a recorded trace (default cap 20,000 steps; the VM itself is capped by an instruction limit).</li></ul></Panel>
        </div>
      )}

      {tab === 'Grammar' && (
        <Panel title="Context-free grammar (as loaded by the LR(1) generator)" bodyClass="p-0">
          {g ? <table className="w-full code"><tbody>{g.grammar.productions.slice(1).map((p) => <tr key={p.id}><td className="td text-fg-mute w-10">{p.id}</td><td className="td"><span className="text-nova-strong">{p.lhs}</span> → {p.rhs.length ? p.rhs.join(' ') : 'ε'}</td></tr>)}</tbody></table> : <div className="p-4 text-fg-mute">Loading from the compiler…</div>}
          {g && <div className="p-3 text-fg-dim border-t border-ink-700">Unambiguous by construction: expression levels encode precedence; braces are mandatory so there is no dangling-else. {g.states} canonical LR(1) states, {g.conflicts.length} conflicts.</div>}
        </Panel>
      )}

      {tab === 'Bytecode' && (
        <Panel title="NOVA VM instruction set (from the compiler)" bodyClass="p-0">
          <table className="w-full"><thead><tr><th className="th w-24">Opcode</th><th className="th">Effect</th></tr></thead><tbody>{isa.map((i) => <tr key={i.op}><td className="td code text-nova-strong">{i.op}</td><td className="td text-fg-dim">{i.description}</td></tr>)}</tbody></table>
          <div className="p-3 text-fg-dim border-t border-ink-700">Values are tagged (int / float / bool). A CALL pops its arguments into the first local slots of a new frame; the entry stub is <code className="code">CALL main; HALT</code>.</div>
        </Panel>
      )}

      {tab === 'Optimizations' && (
        <div className="grid gap-2">{PASS_IDS.map((p) => <Panel key={p} title={PASS_TITLES[p]} bodyClass="p-3 text-fg-dim">{{
          inlining: 'Interprocedural: replaces calls to small non-recursive functions (checked on the call graph) by a renamed copy of their body, then lets the other passes optimize across the old call boundary.',
          constant_propagation: 'Substitutes known constants and copies for variable uses (block-local, plus single-assignment constants function-wide).',
          constant_folding: 'Evaluates constant expressions and constant branches at compile time. Never folds integer division by zero.',
          algebraic: 'x+0, x*1, x*0, x-x, x%1, x/1, x*2→x+x, reflexive comparisons on ints. Float identities restricted to IEEE-safe ones.',
          cse: 'Local value numbering with invalidation on reassignment; commutative operands are normalised.',
          unreachable: 'CFG reachability; also removes jumps to the next instruction and unused labels.',
          dce: 'Backward live-variable dataflow; removes dead stores. Trap-capable integer divisions are retained.',
        }[p]}</Panel>)}</div>
      )}

      {tab === 'REST API' && (
        <Panel title="Endpoints (JSON)" bodyClass="p-0">
          <table className="w-full"><tbody>{[
            ['GET', '/api/health', 'Backend + compiler status, LR(1) summary'], ['GET', '/api/examples', 'Example programs'], ['GET', '/api/grammar', 'Grammar, FIRST/FOLLOW, ACTION/GOTO'],
            ['POST', '/api/compile', 'Run all stages; returns tokens, parser trace, AST, symbols, IR, optimization history, bytecode'],
            ['POST', '/api/execute', 'Compile and run on the VM'], ['POST', '/api/optimize', 'Optimize with chosen passes; before/after metrics'],
            ['POST', '/api/debug/start', 'Create a debug session (records VM state)'], ['POST', '/api/debug/step', 'Step forward/back by instruction, line or over'],
            ['POST', '/api/debug/continue', 'Run to next breakpoint'], ['POST', '/api/debug/reset', 'Rewind to start'], ['POST', '/api/tests/run', 'Run C++ unit tests + differential fuzzing'],
          ].map(([m, p, d]) => <tr key={p}><td className="td w-16"><Chip tone={m === 'GET' ? 'ok' : 'nova'}>{m}</Chip></td><td className="td code">{p}</td><td className="td text-fg-dim">{d}</td></tr>)}</tbody></table>
        </Panel>
      )}

      {tab === 'Architecture' && (
        <Panel title="How the pieces fit" bodyClass="p-4">
          <pre className="code text-fg-dim overflow-auto leading-relaxed">{`  Browser (React/TS)            FastAPI (Python)                C++17 compiler engine
  ───────────────────           ─────────────────────           ──────────────────────────────
  Monaco editor, views   ───►   validates request        ───►  novac compile  (stdin: source)
  no compiler logic             runs novac with limits          lexer → LR(1) parser → AST → semantic
  debugger = cursor over  ◄───  JSON + debug sessions    ◄───   → IR → optimizer → bytecode → VM
  VM snapshots                                                  JSON on stdout`}</pre>
        </Panel>
      )}
    </div>
  )
}
