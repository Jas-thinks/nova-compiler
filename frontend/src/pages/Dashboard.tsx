import { useEffect, useState } from 'react'
import { ArrowRight, Gauge, ShieldAlert } from 'lucide-react'
import { api } from '../services/api'
import type { Capabilities, LastTests } from '../types/api'
import { useNova } from '../state/NovaContext'
import { Chip, PageHeader, Panel, Stat } from '../components/ui'
import PipelineBar from '../components/PipelineBar'
import { ms, pct } from '../lib/format'

export default function Dashboard() {
  const { health, result, examples, loadExample, setStage, exec } = useNova()
  const [caps, setCaps] = useState<Capabilities | null>(null)
  const [last, setLast] = useState<LastTests | null>(null)
  useEffect(() => { api.capabilities().then(setCaps).catch(() => undefined); api.lastTests().then(setLast).catch(() => undefined) }, [])
  const u = last?.result?.unit, df = last?.result?.differential
  const go = (r: string) => { window.location.hash = '/' + r }
  const sum = (id: string) => result?.stages.find((s) => s.id === id)?.summary
  const total = result?.stages.reduce((a, s) => a + s.durationMs, 0)

  return (
    <div className="max-w-6xl">
      <PageHeader
        title="NOVA — Interactive Compiler Engineering Platform"
        subtitle="A real compiler for a small statically typed language. The C++17 engine does all the work — lexing, canonical LR(1) parsing, semantic analysis, IR, optimization, bytecode generation and a virtual machine. This website only visualises its output."
      />

      <div className="grid grid-cols-[repeat(auto-fill,minmax(150px,1fr))] gap-3 mb-4">
        <Stat label="Parser" value="LR(1)" sub={health?.parser ? `${health.parser.states} states · ${health.parser.conflicts} conflicts` : 'backend offline'} />
        <Stat label="Backend" value={health ? (health.compiler_found ? 'online' : 'no compiler') : '…'} tone={health?.compiler_found ? 'ok' : 'bad'} sub={health ? `timeout ${health.limits.compile_timeout_s}s · ${health.limits.max_vm_steps.toLocaleString()} steps max` : undefined} />
        <Stat label="Examples" value={examples.length} sub="shipped programs" />
        <Stat label="Last compile" value={result ? (result.success ? 'success' : 'failed') : '—'} tone={result ? (result.success ? 'ok' : 'bad') : undefined} sub={total !== undefined ? `${ms(total)} total` : 'not compiled yet'} />
      </div>

      <div className="grid md:grid-cols-2 gap-4 mb-4">
        <Panel title="Verified compiler capabilities (probed live)" bodyClass="p-3 space-y-2">
          {caps ? (<>
            <div className="flex gap-1.5 flex-wrap">{Object.entries(caps.nova.analyses).map(([k, v]) => <Chip key={k} tone={v ? 'ok' : 'bad'}>{v ? '✓' : '✗'} {k.replace('_', ' ')}</Chip>)}</div>
            <div className="text-fg-dim">Pipeline: {caps.nova.pipeline_stages.join(' → ')}</div>
            <div className="text-fg-dim">{caps.nova.parser.algorithm}: {caps.nova.parser.productions} productions, {caps.nova.parser.states} states, {caps.nova.parser.conflicts} conflicts · {caps.nova.optimization_passes.length} optimization passes · {caps.nova.bytecode_instructions} VM opcodes</div>
            <div className="text-fg-dim">Probe program matches reference interpreter: <b className={caps.nova.execution_verified_against_reference ? 'text-ok' : 'text-bad'}>{String(caps.nova.execution_verified_against_reference)}</b></div>
            <div className="text-fg-dim">Other languages: {caps.explorer.filter((l) => l.id !== 'nova').map((l) => `${l.name} (${l.status.replace('_', ' ')})`).join(' · ')}</div>
          </>) : <span className="text-fg-mute">Capabilities load from the backend (compiles a probe program).</span>}
        </Panel>
        <Panel title="Last test run on this server" bodyClass="p-3 space-y-2">
          {last?.ran && last.at ? (<>
            <div className="text-fg-dim">Run at {new Date(last.at * 1000).toLocaleString()}</div>
            {u && <div className="flex gap-2 flex-wrap"><Chip tone={u.failed ? 'bad' : 'ok'}>{u.passed}/{u.total} C++ tests passing</Chip></div>}
            {df && <div className="flex gap-2 flex-wrap"><Chip tone={df.failed ? 'bad' : 'ok'}>{df.passed} identical / {df.failed} mismatched random programs</Chip><Chip>{df.inconclusive} inconclusive</Chip></div>}
          </>) : <div className="text-fg-mute">No test run recorded since the backend started. Open <button className="underline" onClick={() => go('tests')}>Compiler Tests</button> and run the suites — results appear here.</div>}
        </Panel>
      </div>
      {health?.sandbox && (
        <div className="panel p-3 mb-4 flex gap-3 border-warn/30"><ShieldAlert size={18} className="text-warn shrink-0 mt-0.5" />
          <div className="text-fg-dim"><b className="text-fg">Execution boundaries.</b> Per-process timeout, {Math.round(health.sandbox.output_cap_bytes / 1048576)} MiB output cap, {health.sandbox.rlimits_available ? 'CPU/memory rlimits active' : <b className="text-bad">rlimits UNAVAILABLE on this platform</b>}, argv-only subprocesses (no shell). Isolation: {health.sandbox.isolation}.</div></div>
      )}

      <Panel title="Compilation pipeline" icon={<Gauge size={13} />} className="mb-4" bodyClass="p-3">
        <PipelineBar stages={result?.stages} onSelect={(s) => { setStage(s); go('playground') }} />
        {result ? (
          <div className="grid grid-cols-[repeat(auto-fill,minmax(180px,1fr))] gap-2 mt-3 text-fg-dim">
            <div>Tokens: <b className="text-fg">{sum('lexer')?.tokens}</b></div>
            <div>Parser steps: <b className="text-fg">{sum('parser')?.steps ?? '—'}</b> <span className="text-fg-mute">({sum('parser')?.shifts} shift / {sum('parser')?.reduces} reduce)</span></div>
            <div>AST nodes: <b className="text-fg">{sum('ast')?.nodes ?? '—'}</b></div>
            <div>Scopes / symbols: <b className="text-fg">{sum('semantic')?.scopes ?? '—'} / {sum('semantic')?.symbols ?? '—'}</b></div>
            <div>IR instructions: <b className="text-fg">{sum('ir')?.instructions ?? '—'}</b></div>
            {result.optimization && <div>After optimization: <b className="text-fg">{result.optimization.instructionsAfter}</b> <span className="text-fg-mute">(−{pct(result.optimization.instructionsBefore, result.optimization.instructionsAfter)})</span></div>}
            {result.bytecode && <div>Bytecode: <b className="text-fg">{result.bytecode.optimized.count}</b> instrs</div>}
            {exec?.execution && <div>VM steps: <b className="text-fg">{exec.execution.optimized.steps}</b></div>}
          </div>
        ) : <p className="mt-3 text-fg-mute">Nothing compiled yet — all numbers on this page come from real compiler runs. Open the Playground and press Compile.</p>}
      </Panel>

      <div className="grid md:grid-cols-2 gap-4">
        <Panel title="Quick start" bodyClass="p-3 space-y-2">
          <ol className="list-decimal pl-5 space-y-1.5 text-fg-dim">
            <li>Open the <b className="text-fg">Playground</b> and write or load a NOVA program.</li>
            <li><b className="text-fg">Compile</b> and click through each stage (tokens → parse trace → AST → symbols → IR).</li>
            <li>Toggle passes in the <b className="text-fg">Optimization Lab</b> and compare before/after.</li>
            <li><b className="text-fg">Run</b>, then <b className="text-fg">Debug</b>: step, step back, set breakpoints, inspect the stack.</li>
            <li>Introduce an error to see exact locations and suggested fixes.</li>
          </ol>
          <div className="flex gap-2 pt-2"><button className="btn btn-primary" onClick={() => go('playground')}>Open Playground <ArrowRight size={14} /></button><button className="btn" onClick={() => go('learn')}>Learn Mode</button></div>
        </Panel>
        <Panel title="Example programs" bodyClass="p-1">
          <ul>
            {examples.map((e) => (
              <li key={e.id}>
                <button onClick={() => { loadExample(e); go('playground') }} className="w-full text-left px-3 py-1.5 rounded hover:bg-ink-800 flex items-center gap-2">
                  <span className="flex-1 min-w-0"><span className="block truncate font-medium">{e.title}</span><span className="block truncate text-fg-mute text-[12px]">{e.description}</span></span>
                  <Chip tone={e.category === 'errors' || e.category === 'runtime' ? 'bad' : 'neutral'}>{e.category}</Chip>
                </button>
              </li>
            ))}
            {examples.length === 0 && <li className="p-3 text-fg-mute">Examples load from the backend.</li>}
          </ul>
        </Panel>
      </div>
    </div>
  )
}
