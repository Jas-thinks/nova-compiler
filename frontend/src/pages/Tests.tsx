import { useMemo, useState } from 'react'
import { Loader2, PlayCircle } from 'lucide-react'
import { Chip, Empty, PageHeader, Panel, Stat } from '../components/ui'
import { api } from '../services/api'
import { pct } from '../lib/format'
import type { TestRunResponse } from '../types/api'

export default function Tests() {
  const [count, setCount] = useState(300)
  const [seed, setSeed] = useState(1)
  const [res, setRes] = useState<TestRunResponse | null>(null)
  const [busy, setBusy] = useState(false)
  const [err, setErr] = useState<string | null>(null)
  const [ranAt, setRanAt] = useState<string | null>(null)

  const run = async () => {
    setBusy(true); setErr(null)
    try { setRes(await api.runTests(count, seed, true)); setRanAt(new Date().toLocaleTimeString()) } catch (e) { setErr(e instanceof Error ? e.message : String(e)) } finally { setBusy(false) }
  }
  const suites = useMemo(() => {
    const m = new Map<string, { p: number; f: number }>()
    res?.unit?.tests.forEach((t) => { const s = m.get(t.suite) ?? { p: 0, f: 0 }; t.passed ? s.p++ : s.f++; m.set(t.suite, s) })
    return [...m.entries()]
  }, [res])
  const d = res?.differential
  const u = res?.unit

  return (
    <div className="max-w-6xl">
      <PageHeader title="Compiler Tests" subtitle="Nothing here is pre-recorded: pressing Run executes the C++ unit-test binary and the differential fuzzer on the server, and reports exactly what they print." />
      <div className="panel p-3 mb-4 flex items-end gap-3 flex-wrap">
        <label className="grid gap-1 text-fg-dim">Random programs<input type="number" min={0} max={2000} value={count} onChange={(e) => setCount(Math.max(0, Math.min(2000, +e.target.value)))} className="bg-ink-800 border border-ink-600 rounded h-8 px-2 w-32 text-fg" /></label>
        <label className="grid gap-1 text-fg-dim">Seed<input type="number" min={0} value={seed} onChange={(e) => setSeed(Math.max(0, +e.target.value))} className="bg-ink-800 border border-ink-600 rounded h-8 px-2 w-32 text-fg" /></label>
        <button className="btn btn-primary" onClick={() => void run()} disabled={busy}>{busy ? <Loader2 size={14} className="animate-spin" /> : <PlayCircle size={14} />} Run all tests</button>
        {ranAt && <span className="text-fg-mute">last run {ranAt}</span>}
      </div>
      {err && <div className="text-bad mb-3">{err}</div>}
      {!res && !busy && <Panel><Empty title="No test run yet" hint="Run the suite to see C++ unit-test results (lexer, parser, AST, semantic, IR, optimizer, VM, end-to-end) and the differential test of random programs." /></Panel>}
      {res && (
        <div className="grid gap-4">
          {u && (<div>
            <div className="grid grid-cols-[repeat(auto-fill,minmax(150px,1fr))] gap-3 mb-3">
              <Stat label="C++ unit tests" value={`${u.passed} / ${u.total}`} tone={u.failed ? 'bad' : 'ok'} sub={u.failed ? `${u.failed} failing` : 'all passing'} />
              {d && <Stat label="Random programs" value={`${d.passed} / ${d.compiled - d.inconclusive}`} tone={d.failed ? 'bad' : 'ok'} sub={`${d.failed} mismatches · ${d.inconclusive} inconclusive`} />}
              {d && <Stat label="IR size reduction" value={`−${pct(d.irInstructionsBefore, d.irInstructionsAfter)}`} sub={`${d.irInstructionsBefore} → ${d.irInstructionsAfter} instrs`} />}
              {d && <Stat label="VM steps reduction" value={`−${pct(d.vmStepsBefore, d.vmStepsAfter)}`} sub={`${d.vmStepsBefore} → ${d.vmStepsAfter} executed`} />}
            </div>
            <Panel title="Unit tests by suite">
              <table className="w-full"><thead><tr><th className="th">Suite</th><th className="th">Passed</th><th className="th">Failed</th><th className="th">Tests</th></tr></thead>
                <tbody>{suites.map(([name, c]) => (
                  <tr key={name} className="align-top"><td className="td font-medium">{name}</td><td className="td text-ok">{c.p}</td><td className={`td ${c.f ? 'text-bad' : 'text-fg-mute'}`}>{c.f}</td>
                    <td className="td code text-[11.5px] text-fg-dim">{u.tests.filter((t) => t.suite === name).map((t) => <div key={t.name} className={t.passed ? '' : 'text-bad'}>{t.passed ? '✓' : '✗'} {t.name}{!t.passed && <div className="whitespace-pre-wrap">{t.message}</div>}</div>)}</td></tr>))}</tbody></table>
            </Panel>
          </div>)}
          {d && (
            <Panel title="Differential testing: reference interpreter vs VM (unoptimized) vs VM (optimized)" bodyClass="p-3 space-y-2">
              <p className="text-fg-dim">{d.requested} programs were generated (seed {d.seed}) from the NOVA grammar. Each was compiled, then executed three ways; outputs and runtime errors must be identical.</p>
              <div className="flex gap-2 flex-wrap"><Chip tone="ok">{d.passed} identical</Chip><Chip tone={d.failed ? 'bad' : 'neutral'}>{d.failed} mismatched</Chip><Chip>{d.inconclusive} hit step limit</Chip><Chip tone={d.generatorFailures ? 'bad' : 'neutral'}>{d.generatorFailures} generator failures</Chip></div>
              {d.failures.map((f) => <details key={f.seed} className="rounded border border-bad/40 p-2"><summary className="cursor-pointer text-bad">seed {f.seed}: {f.kind} — {f.detail}</summary><pre className="code mt-2 overflow-auto">{f.source}</pre></details>)}
            </Panel>
          )}
        </div>
      )}
    </div>
  )
}
