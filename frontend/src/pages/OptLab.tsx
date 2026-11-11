import { useEffect, useMemo, useRef, useState } from 'react'
import { Loader2, RefreshCw } from 'lucide-react'
import { Chip, Empty, PageHeader, Panel, Stat, DiagnosticList } from '../components/ui'
import { useNova } from '../state/NovaContext'
import { api } from '../services/api'
import { diffLines } from '../lib/diff'
import { PASS_IDS, PASS_TITLES, pct } from '../lib/format'
import type { IRFunction, OptimizeResponse } from '../types/api'

const DESCRIPTIONS: Record<string, string> = {
  constant_propagation: 'Replaces variables that are known constants (or plain copies of another name) with that value. Block-local dataflow plus function-wide facts for variables assigned exactly once.',
  constant_folding: 'Evaluates operations whose operands are all constants at compile time, and turns branches on constant conditions into jumps or deletes them. Integer division by zero is never folded.',
  algebraic: 'Identities such as x+0, x*1, x*0, x-x, x%1 and strength reduction x*2 → x+x. Float rules are restricted so IEEE semantics (−0.0, NaN) are preserved.',
  cse: 'Local value numbering: a repeated expression whose operands are unchanged reuses the earlier result. Entries are invalidated when an operand is reassigned.',
  unreachable: 'CFG reachability from the entry block; removes unreachable blocks, jumps to the next instruction and labels nobody uses.',
  dce: 'Backward live-variable analysis over the CFG; deletes pure instructions whose result is never read. Possibly-trapping divisions are kept.',
}

function fnLines(f: IRFunction) { return f.instructions.map((i) => (i.kind === 'label' ? i.text : '    ' + i.text)) }

function SideBySide({ a, b }: { a: IRFunction; b?: IRFunction }) {
  const ops = useMemo(() => diffLines(fnLines(a), b ? fnLines(b) : []), [a, b])
  const left = ops.filter((o) => o.type !== 'added')
  const right = ops.filter((o) => o.type !== 'removed')
  const col = (list: typeof ops, mark: 'removed' | 'added') => (
    <pre className="code leading-[1.5] py-1">{list.map((o, i) => <div key={i} className={`px-3 whitespace-pre ${o.type === mark ? (mark === 'removed' ? 'bg-bad/15 text-bad' : 'bg-ok/15 text-ok') : ''}`}>{o.text || ' '}</div>)}</pre>
  )
  return (
    <div className="grid grid-cols-2 divide-x divide-ink-700">
      <div><div className="px-3 py-1 text-2xs uppercase tracking-wider text-fg-mute border-b border-ink-800">Original · {a.count}</div>{col(left, 'removed')}</div>
      <div><div className="px-3 py-1 text-2xs uppercase tracking-wider text-fg-mute border-b border-ink-800">Optimized · {b?.count ?? 0}</div>{col(right, 'added')}</div>
    </div>
  )
}

export default function OptLab() {
  const { source, passes, setPass, passesParam, setSource } = useNova()
  const [data, setData] = useState<OptimizeResponse | null>(null)
  const [fail, setFail] = useState<{ stage?: string; diags: NonNullable<OptimizeResponse['diagnostics']> } | null>(null)
  const [loading, setLoading] = useState(false)
  const [err, setErr] = useState<string | null>(null)
  const [filter, setFilter] = useState<string | null>(null)
  const seq = useRef(0)

  const analyze = async () => {
    const id = ++seq.current
    setLoading(true); setErr(null)
    try {
      const r = await api.optimize(source, passesParam)
      if (id !== seq.current) return
      if (!r.success) { setFail({ stage: r.failedStage, diags: r.diagnostics ?? [] }); setData(null) } else { setFail(null); setData(r) }
    } catch (e) { if (id === seq.current) setErr(e instanceof Error ? e.message : String(e)) } finally { if (id === seq.current) setLoading(false) }
  }
  useEffect(() => { const t = setTimeout(() => void analyze(), 250); return () => clearTimeout(t) }, [source, passesParam]) // eslint-disable-line react-hooks/exhaustive-deps

  const m = data?.metrics
  const history = useMemo(() => (data ? data.optimization.history.filter((h) => !filter || h.pass === filter) : []), [data, filter])

  return (
    <div>
      <PageHeader title="Optimization Laboratory" subtitle="Enable or disable individual passes; the compiler re-optimizes your program and re-executes both versions on the VM. Every number below is measured from that run." right={<button className="btn" onClick={() => void analyze()}>{loading ? <Loader2 size={14} className="animate-spin" /> : <RefreshCw size={14} />} Re-run</button>} />
      {err && <div className="text-bad mb-3">{err}</div>}
      <div className="grid grid-cols-[300px_1fr] gap-3">
        <div className="flex flex-col gap-3">
          <Panel title="Optimization passes" bodyClass="p-2">
            <div className="flex gap-1.5 mb-2">
              <button className="btn h-7 px-2" onClick={() => PASS_IDS.forEach((p) => setPass(p, true))}>All</button>
              <button className="btn h-7 px-2" onClick={() => PASS_IDS.forEach((p) => setPass(p, false))}>None</button>
            </div>
            {PASS_IDS.map((p) => (
              <label key={p} className="flex gap-2 p-2 rounded hover:bg-ink-800 cursor-pointer items-start">
                <input type="checkbox" className="mt-0.5 accent-[#7b93ff]" checked={passes[p]} onChange={(e) => setPass(p, e.target.checked)} />
                <span className="min-w-0 flex-1"><span className="flex items-center gap-2 font-medium">{PASS_TITLES[p]}{data && <Chip tone={data.optimization.perPass[p] ? 'nova' : 'neutral'}>{data.optimization.perPass[p] ?? 0}</Chip>}</span>
                  <span className="block text-[11.5px] text-fg-mute leading-snug mt-0.5">{DESCRIPTIONS[p]}</span></span>
              </label>
            ))}
          </Panel>
          <Panel title="Program (edit in Playground)" bodyClass="p-0">
            <textarea value={source} onChange={(e) => setSource(e.target.value)} spellCheck={false} className="w-full h-56 bg-transparent code p-3 resize-y outline-none" />
          </Panel>
        </div>

        <div className="flex flex-col gap-3 min-w-0">
          {fail && <Panel title={`Program does not compile (${fail.stage})`}><DiagnosticList items={fail.diags} /></Panel>}
          {!data && !fail && <Empty title={loading ? 'Optimizing…' : 'No data'} />}
          {data && m && (<>
            <div className="grid grid-cols-[repeat(auto-fill,minmax(150px,1fr))] gap-3">
              <Stat label="IR instructions" value={<>{m.ir_before} → {m.ir_after}</>} sub={`${m.ir_eliminated} eliminated (−${pct(m.ir_before, m.ir_after)})`} />
              <Stat label="Bytecode size" value={<>{m.bytecode_before} → {m.bytecode_after}</>} sub={`−${pct(m.bytecode_before, m.bytecode_after)}`} />
              <Stat label="VM instructions run" value={<>{m.vm_steps_before} → {m.vm_steps_after}</>} sub={`−${pct(m.vm_steps_before, m.vm_steps_after)} dynamic`} />
              <Stat label="Transformations" value={data.optimization.history.length} sub={`${data.optimization.rounds} round(s) to fixpoint`} />
              <Stat label="Semantics" value={m.semantics_preserved ? 'preserved' : 'CHANGED'} tone={m.semantics_preserved ? 'ok' : 'bad'} sub={m.matches_reference_interpreter ? 'matches reference interpreter' : 'differs from reference!'} />
            </div>

            <Panel title="Program output before / after optimization" bodyClass="grid grid-cols-2 divide-x divide-ink-700">
              {(['baseline', 'optimized'] as const).map((k) => (
                <div key={k} className="p-3 code"><div className="text-2xs uppercase tracking-wider text-fg-mute mb-1 font-sans">{k === 'baseline' ? 'Unoptimized' : 'Optimized'} · {data.execution[k].steps} steps</div>
                  {data.execution[k].output.map((l, i) => <div key={i}>{l}</div>)}
                  {data.execution[k].error && <div className="text-bad">⚠ {data.execution[k].error!.message}</div>}
                  {data.execution[k].output.length === 0 && !data.execution[k].error && <span className="text-fg-mute">(no output)</span>}</div>
              ))}
            </Panel>

            <Panel title="Pass impact (counted from the transformation log)">
              <table className="w-full"><thead><tr><th className="th">Pass</th><th className="th">Enabled</th><th className="th">Rewrites</th><th className="th">Instructions deleted</th></tr></thead>
                <tbody>{PASS_IDS.map((p) => {
                  const hs = data.optimization.history.filter((h) => h.pass === p)
                  const del = hs.filter((h) => h.after === '(removed)').length
                  return <tr key={p} className="hover:bg-ink-850"><td className="td">{PASS_TITLES[p]}</td><td className="td">{data.config[p] ? <Chip tone="ok">on</Chip> : <Chip>off</Chip>}</td><td className="td code">{hs.length - del}</td><td className="td code">{del}</td></tr>
                })}</tbody></table>
              <div className="px-3 py-2 text-fg-mute text-[12px]">Net IR change: {m.ir_before} → {m.ir_after} instructions (a pass that rewrites an instruction into a cheaper one deletes nothing; deletions come from folding branches, DCE, unreachable-code removal and propagation enabling them).</div>
            </Panel>

            <Panel title="Original vs optimized IR">
              {data.ir.original.functions.map((f) => (
                <div key={f.name} className="border-b border-ink-700 last:border-0"><div className="px-3 pt-2 text-nova code">fn {f.name}</div>
                  <SideBySide a={f} b={data.ir.optimized.functions.find((x) => x.name === f.name)} /></div>
              ))}
            </Panel>

            <Panel title={`Transformation history (${history.length})`} right={
              <select className="bg-ink-800 border border-ink-600 rounded h-6 px-1 text-[12px]" value={filter ?? ''} onChange={(e) => setFilter(e.target.value || null)}>
                <option value="">all passes</option>{PASS_IDS.map((p) => <option key={p} value={p}>{PASS_TITLES[p]}</option>)}
              </select>}>
              {history.length === 0 ? <div className="p-3 text-fg-mute">No transformations: either all passes are disabled or the program has nothing to optimize.</div> : (
                <table className="w-full"><thead><tr><th className="th w-10">Rnd</th><th className="th">Pass</th><th className="th">Where</th><th className="th">Before</th><th className="th">After</th><th className="th">Why</th></tr></thead>
                  <tbody>{history.map((h, i) => (
                    <tr key={i} className="hover:bg-ink-850"><td className="td text-fg-mute">{h.round}</td><td className="td"><Chip tone="nova">{PASS_TITLES[h.pass] ?? h.pass}</Chip></td>
                      <td className="td code text-fg-dim whitespace-nowrap">{h.function}{h.line ? `:${h.line}` : ''}</td>
                      <td className="td code text-bad">{h.before}</td><td className="td code text-ok">{h.after}</td><td className="td text-fg-dim text-[12px]">{h.explanation}</td></tr>))}</tbody></table>)}
            </Panel>
          </>)}
        </div>
      </div>
    </div>
  )
}
