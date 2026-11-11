import { useCallback, useEffect, useState } from 'react'
import { Cpu, Loader2 } from 'lucide-react'
import { Chip, Empty, PageHeader, Panel, Stat } from '../components/ui'
import { api } from '../services/api'
import { useNova } from '../state/NovaContext'
import type { AsmResponse } from '../types/api'

function AsmLine({ l }: { l: string }) {
  if (!l.trim()) return <div>&nbsp;</div>
  if (!l.startsWith(' ')) return <div className="text-nova-strong font-semibold mt-2">{l}</div>
  const t = l.trim()
  if (t.startsWith('.')) return <div className="text-fg-mute">{l}</div>
  const [op, ...rest] = t.split(/\s+/)
  const operands = rest.join(' ')
  const tone = op.startsWith('j') || op === 'call' || op === 'ret' || op === 'leave' ? 'text-warn' : op.startsWith('cmp') || op.startsWith('set') || op === 'testq' ? 'text-ok' : 'text-fg'
  const parts = operands.split(/(%r[a-z0-9]+|%e[a-z]+|%xmm\d|-?\d*\(%rbp\)|\$-?\d+)/g)
  return (
    <div className="pl-4">
      <span className={`${tone} inline-block w-20`}>{op}</span>
      {parts.map((p, i) => <span key={i} className={/^%r(bx|1[2-5])$/.test(p) ? 'text-nova font-semibold' : /\(%rbp\)/.test(p) ? 'text-bad' : p.startsWith('$') ? 'text-fg-dim' : p.startsWith('%') ? 'text-fg' : 'text-fg-dim'}>{p}</span>)}
    </div>
  )
}

export default function Native() {
  const { source, passesParam, examples, loadExample } = useNova()
  const [res, setRes] = useState<AsmResponse | null>(null)
  const [regalloc, setRegalloc] = useState(true)
  const [busy, setBusy] = useState(false)
  const [err, setErr] = useState<string | null>(null)

  const load = useCallback(async () => {
    setBusy(true); setErr(null)
    try { setRes(await api.asm(source, passesParam, regalloc)) } catch (e) { setErr(e instanceof Error ? e.message : String(e)) } finally { setBusy(false) }
  }, [source, passesParam, regalloc])
  useEffect(() => { const t = window.setTimeout(() => void load(), 300); return () => window.clearTimeout(t) }, [load])

  const c = res?.compare
  const cut = c ? Math.round((1 - c.with_regalloc.memoryOperands / Math.max(1, c.without_regalloc.memoryOperands)) * 100) : 0
  return (
    <div className="h-full flex flex-col">
      <PageHeader title="Native x86-64 backend"
        subtitle="The IR is lowered to System V x86-64 assembly: every value gets a stack slot, and a linear-scan register allocator (liveness-based intervals) moves the longest-lived non-float values into the callee-saved registers %rbx, %r12–%r15, spilling the interval that ends furthest away. Build and run it with scripts/nova-native.sh."
        right={<div className="flex items-center gap-2">
          <select className="btn" value="" onChange={(e) => { const ex = examples.find((x) => x.id === e.target.value); if (ex) loadExample(ex) }}>
            <option value="">Load example…</option>{examples.filter((x) => x.category !== 'errors').map((x) => <option key={x.id} value={x.id}>{x.title}</option>)}
          </select>
          <label className="flex items-center gap-1.5 text-fg-dim"><input type="checkbox" checked={regalloc} onChange={(e) => setRegalloc(e.target.checked)} /> register allocation</label>
          {busy && <Loader2 size={14} className="animate-spin" />}
        </div>} />
      {err && <div className="panel border-bad/40 p-3 text-bad mb-3">{err}</div>}
      {res && !res.success && <div className="panel border-bad/40 p-3 mb-3"><b className="text-bad">The program does not compile</b> — fix these first:{res.diagnostics?.map((d, i) => <div key={i} className="mt-1">{d.code} {d.loc.line}:{d.loc.col} — {d.message}</div>)}</div>}
      {res?.success && res.stats && c && (
        <>
          <div className="grid grid-cols-[repeat(auto-fill,minmax(160px,1fr))] gap-3 mb-3">
            <Stat label="Instructions" value={res.stats.instructions} sub={`${res.stats.functions} function(s)`} />
            <Stat label="Stack-memory operands" value={res.stats.memoryOperands} sub={regalloc ? `was ${c.without_regalloc.memoryOperands} without allocation` : 'allocation off'} />
            <Stat label="Memory traffic removed" value={`${cut}%`} tone={cut > 0 ? 'ok' : undefined} sub="static count, not a timing" />
            <Stat label="Callee-saved register uses" value={res.stats.calleeSavedRegisterUses} sub="%rbx %r12–%r15" />
          </div>
          <div className="panel p-3 mb-3 space-y-2">
            <div className="text-2xs uppercase tracking-wider text-fg-mute">Stack-memory operands (lower is better)</div>
            {([['without register allocation', c.without_regalloc.memoryOperands, 'bg-bad/70'], ['with linear-scan allocation', c.with_regalloc.memoryOperands, 'bg-ok/80']] as const).map(([label, v, color]) => (
              <div key={label} className="flex items-center gap-3"><div className="w-52 text-fg-dim">{label}</div>
                <div className="flex-1 h-4 bg-ink-800 rounded overflow-hidden"><div className={`h-full ${color}`} style={{ width: `${(100 * v) / Math.max(1, c.without_regalloc.memoryOperands)}%` }} /></div><div className="w-12 text-right tabular-nums">{v}</div></div>
            ))}
          </div>
        </>
      )}
      <Panel title={<span className="flex items-center gap-2"><Cpu size={13} /> Generated assembly (AT&amp;T syntax)</span>} className="flex-1 min-h-[300px]" right={<><Chip tone="nova">%rbx %r12–%r15</Chip><Chip tone="bad">stack slot (%rbp)</Chip></>}>
        {res?.assembly ? <div className="code text-[12px] leading-[1.45] p-3 whitespace-pre">{res.assembly.split('\n').map((l, i) => <AsmLine key={i} l={l} />)}</div> : <Empty title={busy ? 'Generating…' : 'No assembly yet'} />}
      </Panel>
    </div>
  )
}
