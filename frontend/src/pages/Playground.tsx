import { lazy, Suspense, useMemo, useState } from 'react'
import { AlertCircle, FileCode, Lightbulb, Terminal } from 'lucide-react'
import { useNova } from '../state/NovaContext'
import { Chip, DiagnosticList, Empty, Panel } from '../components/ui'
import PipelineBar from '../components/PipelineBar'
import TokenTable from '../features/lexer/TokenTable'
import { AstOutline } from '../features/ast/AstTree'
import { SymbolTable } from '../features/semantic/SymbolTables'
import IRListing from '../features/ir/IRListing'
import BytecodeListing from '../features/bytecode/BytecodeListing'
import { api } from '../services/api'
import { PASS_TITLES } from '../lib/format'
import type { CompileResponse, Diagnostic } from '../types/api'

const CodeEditor = lazy(() => import('../components/CodeEditor'))

function Inspector({ r, stage }: { r: CompileResponse; stage: string }) {
  const st = r.stages.find((s) => s.id === stage)
  if (!st || st.status === 'skipped') return <Empty title="Stage did not run" hint="An earlier stage failed. See Problems below." />
  if (st.status === 'error') return <DiagnosticList items={r.diagnostics.filter((d) => d.severity === 'error')} />
  switch (stage) {
    case 'lexer': return <TokenTable tokens={r.tokens} />
    case 'parser': {
      const p = r.parser
      return (
        <div className="p-3 space-y-3">
          <div className="flex gap-2 flex-wrap"><Chip tone="nova">{p.algorithm}</Chip><Chip>{p.states} states</Chip><Chip>{p.shifts} shifts</Chip><Chip>{p.reduces} reductions</Chip><Chip>max stack {p.maxStackDepth}</Chip></div>
          <table className="w-full code">
            <thead><tr><th className="th w-10">#</th><th className="th">Action</th><th className="th">Lookahead</th></tr></thead>
            <tbody>{p.trace.slice(-14).map((s) => <tr key={s.step}><td className="td text-fg-mute">{s.step}</td><td className="td">{s.action}</td><td className="td text-fg-dim">{s.lookahead}</td></tr>)}</tbody>
          </table>
          <p className="text-fg-mute">Showing the last 14 of {p.totalSteps} steps. Open Syntax Analysis for the full ACTION/GOTO trace.</p>
        </div>
      )
    }
    case 'ast': return r.ast ? <div className="py-1"><AstOutline node={r.ast} onSelect={() => undefined} /></div> : null
    case 'semantic': return r.semantic ? <div>{r.semantic.scopes.filter((s) => s.symbols.length).map((s) => <div key={s.id}><div className="px-3 pt-2 text-2xs uppercase tracking-wider text-fg-mute">{s.kind === 'function' ? `fn ${s.owner}` : `block in ${s.owner} (line ${s.loc.line})`}</div><SymbolTable scope={s} /></div>)}</div> : null
    case 'ir': return r.ir ? <IRListing program={r.ir.original} showExplain={false} /> : null
    case 'optimizer': return r.optimization ? (
      <div className="p-3 space-y-2">
        <div className="flex gap-2 flex-wrap"><Chip tone="nova">{r.optimization.instructionsBefore} → {r.optimization.instructionsAfter} IR instrs</Chip><Chip>{r.optimization.history.length} transformations</Chip><Chip>{r.optimization.rounds} round(s)</Chip></div>
        {r.optimization.history.slice(0, 40).map((h, i) => (
          <div key={i} className="rounded border border-ink-700 bg-ink-850 p-2">
            <div className="flex items-center gap-2"><Chip tone="nova">{PASS_TITLES[h.pass] ?? h.pass}</Chip><span className="text-fg-mute code">fn {h.function}{h.line ? ` · line ${h.line}` : ''}</span></div>
            <div className="code mt-1"><span className="text-bad line-through opacity-80">{h.before}</span> <span className="text-fg-mute">→</span> <span className="text-ok">{h.after}</span></div>
          </div>
        ))}
        {r.optimization.history.length === 0 && <p className="text-fg-mute">No transformations were applicable (or all passes are disabled).</p>}
      </div>
    ) : null
    case 'codegen': return r.bytecode ? <BytecodeListing bytecode={r.bytecode.optimized} showDesc={false} /> : null
    default: return null
  }
}

export default function Playground() {
  const { source, setSource, result, exec, stale, examples, loadExample, stage, setStage, compile, run, compiling, running } = useNova()
  const [tab, setTab] = useState<'output' | 'problems'>('output')
  const [reveal, setReveal] = useState<number | undefined>()
  const [explain, setExplain] = useState<{ id: string; text: string } | null>(null)

  const diagnostics = useMemo<Diagnostic[]>(() => {
    const d = !result || stale ? [] : [...result.diagnostics]
    const e = exec?.error
    if (e && !stale) d.push({ severity: 'error', phase: 'runtime', category: 'Runtime error', code: 'E-RT', message: e.message, explanation: '', suggestion: '', loc: { line: e.line, col: 1, endLine: e.line, endCol: 200 } })
    return d
  }, [result, stale, exec])

  const doExplain = async (d: Diagnostic) => {
    setReveal(d.loc.line)
    const key = `${d.code}:${d.loc.line}:${d.loc.col}`
    try {
      const r = await api.explain(source, d)
      setExplain({ id: key, text: r.explanation ?? r.note ?? 'No explanation available.' })
    } catch (e) { setExplain({ id: key, text: e instanceof Error ? e.message : String(e) }) }
  }

  return (
    <div className="h-full min-h-[640px] grid gap-3 grid-cols-[minmax(0,1.15fr)_150px_minmax(0,1fr)] grid-rows-[minmax(0,1fr)_230px]">
      <Panel title="Source · main.nova" icon={<FileCode size={13} />} className="row-span-1"
        right={
          <select className="bg-ink-800 border border-ink-600 rounded h-6 px-1.5 text-[12px] text-fg max-w-[190px]" value="" onChange={(e) => { const ex = examples.find((x) => x.id === e.target.value); if (ex) loadExample(ex) }}>
            <option value="">Load example…</option>
            {['basics', 'functions', 'loops', 'optimization', 'errors', 'runtime'].map((cat) => (
              <optgroup key={cat} label={cat}>{examples.filter((x) => x.category === cat).map((x) => <option key={x.id} value={x.id}>{x.title}</option>)}</optgroup>
            ))}
          </select>
        }>
        <Suspense fallback={<div className="p-4 text-fg-mute">Loading editor…</div>}>
          <CodeEditor value={source} onChange={setSource} diagnostics={diagnostics} revealLine={reveal} />
        </Suspense>
      </Panel>

      <Panel title="Pipeline" bodyClass="p-2">
        <PipelineBar vertical stages={result?.stages} selected={stage} onSelect={setStage} />
        <div className="mt-3 flex flex-col gap-1.5">
          <button className="btn justify-center" disabled={compiling || running} onClick={() => void compile()}>Compile</button>
          <button className="btn btn-primary justify-center" disabled={compiling || running} onClick={() => void run()}>Run</button>
        </div>
      </Panel>

      <Panel title={`Stage inspector · ${result?.stages.find((s) => s.id === stage)?.name ?? '—'}`} right={stale && <Chip tone="warn">stale</Chip>}>
        {result ? <Inspector r={result} stage={stage} /> : <Empty icon={<Lightbulb size={26} />} title="Compile to inspect the pipeline" hint="Press Compile (Ctrl+Enter). Each stage's real output appears here; click a stage on the left to switch." />}
      </Panel>

      <Panel className="col-span-3"
        title={<span className="flex gap-1">
          <button onClick={() => setTab('output')} className={`px-2 h-6 rounded flex items-center gap-1 ${tab === 'output' ? 'bg-ink-700 text-fg' : 'hover:text-fg'}`}><Terminal size={12} /> Output</button>
          <button onClick={() => setTab('problems')} className={`px-2 h-6 rounded flex items-center gap-1 ${tab === 'problems' ? 'bg-ink-700 text-fg' : 'hover:text-fg'}`}><AlertCircle size={12} /> Problems {result && result.diagnostics.length > 0 && <Chip tone={result.errorCount ? 'bad' : 'warn'}>{result.diagnostics.length}</Chip>}</button>
        </span>}>
        {tab === 'output' ? (
          exec ? (
            <div className="p-3 code space-y-2">
              {exec.output.length === 0 && !exec.error && <div className="text-fg-mute">(program produced no output)</div>}
              {exec.output.map((l, i) => <div key={i}><span className="text-ink-500 select-none mr-3">{i + 1}</span>{l}</div>)}
              {exec.error && <div className="text-bad">⚠ runtime error: {exec.error.message}{exec.error.line ? ` (line ${exec.error.line})` : ''}</div>}
              {exec.execution && (
                <div className="pt-2 mt-2 border-t border-ink-700 flex flex-wrap gap-2 font-sans">
                  <Chip>{exec.execution.optimized.steps} VM instructions executed (optimized)</Chip>
                  <Chip>{exec.execution.baseline.steps} unoptimized</Chip>
                  <Chip tone={exec.execution.optimizationPreservesBehavior ? 'ok' : 'bad'}>optimization {exec.execution.optimizationPreservesBehavior ? 'preserved' : 'CHANGED'} behaviour</Chip>
                  <Chip tone={exec.execution.matchesReference ? 'ok' : 'bad'}>{exec.execution.matchesReference ? 'matches' : 'differs from'} reference interpreter</Chip>
                </div>
              )}
            </div>
          ) : <Empty title="No program output yet" hint="Press Run to compile and execute the program on the NOVA VM." />
        ) : result ? (
          <div>
            <DiagnosticList items={result.diagnostics} onSelect={(d) => void doExplain(d)} />
            {explain && <div className="m-3 p-3 rounded border border-nova/30 bg-nova/5"><div className="text-2xs uppercase tracking-wider text-nova mb-1">Plain-language explanation (optional AI, rephrases the compiler's diagnostic)</div>{explain.text}</div>}
          </div>
        ) : <Empty title="No diagnostics yet" />}
      </Panel>
    </div>
  )
}
