import { useEffect, useMemo, useState } from 'react'
import { ChevronLeft, ChevronRight, ChevronsLeft, ChevronsRight, Pause, Play } from 'lucide-react'
import Gate from '../components/Gate'
import { Chip, Empty, PageHeader, Panel } from '../components/ui'
import { api } from '../services/api'
import { useNova } from '../state/NovaContext'
import type { GrammarInfo, ParseNode, ParseStep } from '../types/api'

type Tab = 'trace' | 'grammar' | 'sets' | 'table' | 'tree'

function useGrammar() {
  const [g, setG] = useState<GrammarInfo | null>(null)
  const [err, setErr] = useState<string | null>(null)
  useEffect(() => { api.grammar().then(setG).catch((e: Error) => setErr(e.message)) }, [])
  return { g, err }
}

function StackView({ stack }: { stack: string[] }) {
  return (
    <div className="flex items-center gap-1 flex-wrap code">
      {stack.map((s, i) => i % 2 === 0
        ? <span key={i} className="px-1.5 h-6 inline-flex items-center rounded bg-ink-700 text-warn border border-ink-600" title="LR state">{s}</span>
        : <span key={i} className="px-1.5 h-6 inline-flex items-center rounded bg-nova/15 text-nova-strong border border-nova/30">{s}</span>)}
    </div>
  )
}

function TraceTab({ steps, truncated, total }: { steps: ParseStep[]; truncated: boolean; total: number }) {
  const [i, setI] = useState(0)
  const [playing, setPlaying] = useState(false)
  useEffect(() => {
    if (!playing) return
    const t = setInterval(() => setI((x) => { if (x >= steps.length - 1) { setPlaying(false); return x } return x + 1 }), 250)
    return () => clearInterval(t)
  }, [playing, steps.length])
  const s = steps[i]
  if (!s) return <Empty title="No parser trace" />
  const kind = s.action.startsWith('shift') ? 'ok' : s.action.startsWith('reduce') ? 'nova' : s.action === 'accept' ? 'ok' : 'bad'
  return (
    <div className="flex flex-col gap-3 h-full min-h-0">
      <div className="panel p-3 shrink-0">
        <div className="flex items-center gap-2 mb-3">
          <button className="btn-icon" onClick={() => { setI(0); setPlaying(false) }}><ChevronsLeft size={15} /></button>
          <button className="btn-icon" onClick={() => setI((x) => Math.max(0, x - 1))}><ChevronLeft size={15} /></button>
          <button className="btn-icon" onClick={() => setPlaying((p) => !p)}>{playing ? <Pause size={15} /> : <Play size={15} />}</button>
          <button className="btn-icon" onClick={() => setI((x) => Math.min(steps.length - 1, x + 1))}><ChevronRight size={15} /></button>
          <button className="btn-icon" onClick={() => setI(steps.length - 1)}><ChevronsRight size={15} /></button>
          <input type="range" min={0} max={steps.length - 1} value={i} onChange={(e) => setI(+e.target.value)} className="flex-1 accent-[#7b93ff]" />
          <span className="code text-fg-dim w-24 text-right">step {s.step}/{total}</span>
        </div>
        <div className="grid gap-2">
          <div><div className="text-2xs uppercase tracking-wider text-fg-mute mb-1">Parser stack (state, symbol, state, …)</div><StackView stack={s.stack} /></div>
          <div><div className="text-2xs uppercase tracking-wider text-fg-mute mb-1">Remaining input</div>
            <div className="flex gap-1 code flex-wrap">{s.input.map((t, k) => <span key={k} className={`px-1.5 h-6 inline-flex items-center rounded border ${k === 0 ? 'border-warn text-warn bg-warn/10' : 'border-ink-600 text-fg-dim'}`}>{t}</span>)}<span className="text-fg-mute self-center">…</span></div></div>
          <div className="flex items-center gap-2"><Chip tone={kind as 'ok'}>{s.action}</Chip><span className="text-fg-dim">{s.detail}</span></div>
        </div>
      </div>
      <Panel title={`Trace${truncated ? ' (truncated for display)' : ''}`} className="flex-1">
        <table className="w-full code">
          <thead><tr><th className="th w-12">#</th><th className="th">Action</th><th className="th">Lookahead</th><th className="th">Stack depth</th></tr></thead>
          <tbody>{steps.map((st, k) => (
            <tr key={k} onClick={() => setI(k)} className={`cursor-pointer hover:bg-ink-850 ${k === i ? 'bg-nova/15' : ''}`}>
              <td className="td text-fg-mute">{st.step}</td><td className="td">{st.action}</td><td className="td text-fg-dim">{st.lookahead}</td><td className="td text-fg-mute">{(st.stack.length + 1) / 2}</td>
            </tr>))}</tbody>
        </table>
      </Panel>
    </div>
  )
}

function TableTab({ g }: { g: GrammarInfo }) {
  const [sel, setSel] = useState(0)
  const [closure, setClosure] = useState<string[] | null>(null)
  useEffect(() => { setClosure(null); api.lrState(sel).then((r) => setClosure(r.closure)).catch(() => setClosure([])) }, [sel])
  const st = g.stateList[sel]
  const prodText = (a: string) => (a.startsWith('r') ? g.grammar.productions[+a.slice(1)]?.text : '')
  return (
    <div className="grid grid-cols-[170px_1fr] gap-3 h-full min-h-0">
      <Panel title={`States (${g.states})`}><div>{g.stateList.map((s) => <button key={s.id} onClick={() => setSel(s.id)} className={`w-full text-left px-3 py-1 code hover:bg-ink-800 ${sel === s.id ? 'bg-nova/15 text-nova-strong' : 'text-fg-dim'}`}>I{s.id}</button>)}</div></Panel>
      <div className="flex flex-col gap-3 min-h-0 overflow-auto">
        <Panel title={`I${sel} — kernel items`} bodyClass="p-2 code leading-6">{st.kernel.map((k, i) => <div key={i}>{k}</div>)}</Panel>
        <Panel title={`I${sel} — closure (${closure?.length ?? '…'} item cores)`} bodyClass="p-2 code leading-6 max-h-64">{(closure ?? ['computing…']).map((k, i) => <div key={i} className="text-fg-dim">{k}</div>)}</Panel>
        <div className="grid grid-cols-2 gap-3">
          <Panel title="ACTION row" bodyClass="p-2">{Object.entries(st.action).map(([t, a]) => <div key={t} className="code flex gap-2"><span className="w-24 text-fg-dim">{t}</span><Chip tone={a[0] === 's' ? 'ok' : a === 'acc' ? 'nova' : 'warn'}>{a}</Chip><span className="text-fg-mute truncate">{prodText(a)}</span></div>)}{Object.keys(st.action).length === 0 && <span className="text-fg-mute">none</span>}</Panel>
          <Panel title="GOTO row" bodyClass="p-2">{Object.entries(st.goto).map(([t, a]) => <div key={t} className="code flex gap-2"><span className="w-28 text-fg-dim">{t}</span><Chip>{a}</Chip></div>)}{Object.keys(st.goto).length === 0 && <span className="text-fg-mute">none</span>}</Panel>
        </div>
      </div>
    </div>
  )
}

function TreeNode({ n, depth, open }: { n: ParseNode; depth: number; open: boolean }) {
  const [o, setO] = useState(open && depth < 4)
  const has = n.children.length > 0
  return (
    <div>
      <div style={{ paddingLeft: depth * 14 + 6 }} onClick={() => has && setO(!o)} className={`code py-[1px] ${has ? 'cursor-pointer hover:bg-ink-800' : ''} whitespace-nowrap`}>
        <span className="text-fg-mute inline-block w-3">{has ? (o ? '▾' : '▸') : ''}</span>
        {n.isToken ? <span><span className="text-fg-dim">{n.symbol}</span> <span className="text-warn">“{n.lexeme}”</span></span> : <span className="text-nova-strong">{n.symbol}</span>}
        {!n.isToken && n.children.length === 0 && <span className="text-fg-mute"> ε</span>}
      </div>
      {o && n.children.map((c, i) => <TreeNode key={i} n={c} depth={depth + 1} open={open} />)}
    </div>
  )
}

export default function Syntax() {
  const { g, err } = useGrammar()
  const [tab, setTab] = useState<Tab>('trace')
  const { result } = useNova()
  const tabs: [Tab, string][] = [['trace', 'Parse trace'], ['grammar', 'Grammar'], ['sets', 'FIRST / FOLLOW'], ['table', 'LR(1) states & tables'], ['tree', 'Parse tree']]
  const gi = useMemo(() => g, [g])
  return (
    <div className="h-full flex flex-col">
      <PageHeader title="Syntax Analysis" subtitle={<>Canonical <b>LR(1)</b> parser: the compiler builds the full set of LR(1) item sets from the grammar, derives ACTION/GOTO tables (no conflicts), then drives a shift/reduce stack machine over the token stream.</>}
        right={gi && <><Chip tone="nova">{gi.states} states</Chip><Chip tone={gi.conflicts.length ? 'bad' : 'ok'}>{gi.conflicts.length} conflicts</Chip><Chip>built in {gi.buildMillis} ms</Chip></>} />
      <div className="flex gap-1 mb-3 border-b border-ink-700 shrink-0">
        {tabs.map(([id, label]) => <button key={id} onClick={() => setTab(id)} className={`px-3 h-8 -mb-px border-b-2 ${tab === id ? 'border-nova text-fg' : 'border-transparent text-fg-dim hover:text-fg'}`}>{label}</button>)}
      </div>
      <div className="flex-1 min-h-0">
        {tab === 'trace' && <Gate need={(r) => r.parser.trace.length > 0}>{(r) => <TraceTab steps={r.parser.trace} truncated={r.parser.traceTruncated} total={r.parser.totalSteps} />}</Gate>}
        {tab === 'tree' && <Gate need={(r) => r.parser.tree}>{(r) => <Panel title="Concrete parse tree (click to expand)"><TreeNode n={r.parser.tree!} depth={0} open /></Panel>}</Gate>}
        {err && <div className="text-bad">{err}</div>}
        {tab === 'grammar' && gi && (
          <Panel title={`Productions (${gi.grammar.productions.length - 1} + augmented start)`}><table className="w-full code"><tbody>{gi.grammar.productions.map((p) => <tr key={p.id} className="hover:bg-ink-850"><td className="td text-fg-mute w-10">{p.id}</td><td className="td"><span className="text-nova-strong">{p.lhs}</span> <span className="text-fg-mute">→</span> {p.rhs.length ? p.rhs.map((s, i) => <span key={i} className={gi.grammar.nonterminals.includes(s) ? 'text-nova-strong' : 'text-warn'}> {s}</span>) : <span className="text-fg-mute"> ε</span>}</td></tr>)}</tbody></table>
            <div className="p-3 text-fg-dim">Terminals: <span className="code text-warn">{gi.grammar.terminals.join(' ')}</span></div></Panel>
        )}
        {tab === 'sets' && gi && (
          <Panel title="FIRST and FOLLOW sets"><table className="w-full code"><thead><tr><th className="th">Nonterminal</th><th className="th">FIRST</th><th className="th">FOLLOW</th></tr></thead><tbody>{gi.grammar.nonterminals.map((n) => <tr key={n} className="hover:bg-ink-850"><td className="td text-nova-strong">{n}</td><td className="td">{(gi.grammar.first[n] ?? []).join(' ')}</td><td className="td text-fg-dim">{(gi.grammar.follow[n] ?? []).join(' ')}</td></tr>)}</tbody></table></Panel>
        )}
        {tab === 'table' && gi && <TableTab g={gi} />}
        {!gi && !err && tab !== 'trace' && tab !== 'tree' && <div className="text-fg-mute p-4">Loading grammar from the compiler…</div>}
        {!result && tab === 'trace' && null}
      </div>
    </div>
  )
}
