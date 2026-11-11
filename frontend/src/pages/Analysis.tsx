import { useEffect, useState } from 'react'
import { ChevronLeft, ChevronRight } from 'lucide-react'
import Gate from '../components/Gate'
import { Chip, PageHeader, Panel, SourceView } from '../components/ui'
import CfgGraph from '../features/cfg/CfgGraph'
import { useNova } from '../state/NovaContext'
import type { CfgBlock, DataflowInfo, FunctionAnalysis } from '../types/api'

type Tab = 'cfg' | 'liveness' | 'reaching' | 'ssa'

const fmt = (s: string[]) => (s.length ? s.join(', ') : '∅')
const same = (a: string[], b?: string[]) => !!b && a.length === b.length && a.every((x, i) => x === b[i])

export function Dataflow({ fn, kind }: { fn: FunctionAnalysis; kind: 'liveness' | 'reaching' }) {
  const d: DataflowInfo = kind === 'liveness' ? fn.liveness : fn.reachingDefinitions
  const [it, setIt] = useState(d.iterations)
  useEffect(() => setIt(d.iterations), [d])
  const h = d.history[it - 1]
  const prev = d.history[it - 2]
  const live = kind === 'liveness'
  const a = live ? d.use! : d.gen!, b = live ? d.def! : d.kill!
  return (
    <div className="grid gap-3">
      <Panel bodyClass="p-3 text-fg-dim space-y-1">
        {live ? (<>
          <div><b className="text-fg">Live-variable analysis</b> — backward, may-analysis. A variable is <i>live</i> at a point if its current value may be read later.</div>
          <div className="code text-fg">IN[B] = USE[B] ∪ (OUT[B] − DEF[B]) &nbsp;&nbsp; OUT[B] = ⋃ IN[S] for S ∈ succ(B)</div>
          <div>USE = read before being written in the block; DEF = written in the block. Dead-code elimination removes writes whose variable is not live afterwards.</div>
        </>) : (<>
          <div><b className="text-fg">Reaching definitions</b> — forward, may-analysis. A definition <i>reaches</i> a point if some path from it to the point does not redefine the variable.</div>
          <div className="code text-fg">OUT[B] = GEN[B] ∪ (IN[B] − KILL[B]) &nbsp;&nbsp; IN[B] = ⋃ OUT[P] for P ∈ pred(B)</div>
          <div>Definition ids: <code className="code">dN</code> = IR instruction N, <code className="code">p:x</code> = parameter x at function entry.</div>
        </>)}
        <div className="flex gap-2 items-center pt-1">
          <Chip tone={d.converged ? 'ok' : 'bad'}>{d.converged ? `converged after ${d.iterations} iteration(s)` : 'did not converge'}</Chip>
          <Chip>{d.direction}</Chip>
        </div>
      </Panel>
      <div className="flex items-center gap-2">
        <button className="btn-icon" disabled={it <= 1} onClick={() => setIt(it - 1)}><ChevronLeft size={15} /></button>
        <input type="range" min={1} max={d.iterations} value={it} onChange={(e) => setIt(+e.target.value)} className="w-56 accent-[#7b93ff]" />
        <button className="btn-icon" disabled={it >= d.iterations} onClick={() => setIt(it + 1)}><ChevronRight size={15} /></button>
        <span className="code text-fg-dim">iteration {it} / {d.iterations}</span>
        {it === d.iterations && d.converged && <Chip tone="ok">fixed point: nothing changed in this pass</Chip>}
        <span className="text-fg-mute text-[12px]">highlighted = changed since previous iteration</span>
      </div>
      <Panel title="Per-block sets">
        <table className="w-full code">
          <thead><tr><th className="th w-14">Block</th><th className="th">{live ? 'USE' : 'GEN'}</th><th className="th">{live ? 'DEF' : 'KILL'}</th><th className="th">IN</th><th className="th">OUT</th></tr></thead>
          <tbody>{fn.blocks.map((blk) => (
            <tr key={blk.id} className={`hover:bg-ink-850 ${blk.reachable ? '' : 'opacity-50'}`}>
              <td className="td text-nova-strong">B{blk.id}{blk.loopHeader && <span className="text-warn"> ⟲</span>}</td>
              <td className="td text-fg-dim">{fmt(a[blk.id])}</td><td className="td text-fg-dim">{fmt(b[blk.id])}</td>
              <td className={`td ${h && !same(h.in[blk.id], prev?.in[blk.id]) ? 'bg-warn/15' : ''}`}>{fmt(h?.in[blk.id] ?? [])}</td>
              <td className={`td ${h && !same(h.out[blk.id], prev?.out[blk.id]) ? 'bg-warn/15' : ''}`}>{fmt(h?.out[blk.id] ?? [])}</td>
            </tr>))}</tbody>
        </table>
      </Panel>
      {!live && d.definitions && (
        <Panel title="Definitions"><table className="w-full code"><tbody>{Object.entries(d.definitions).map(([k, v]) => <tr key={k}><td className="td text-warn w-16">{k}</td><td className="td">{v}</td></tr>)}</tbody></table></Panel>
      )}
    </div>
  )
}

function Ssa({ fn }: { fn: FunctionAnalysis }) {
  return (
    <div className="grid gap-3">
      <Panel bodyClass="p-3 text-fg-dim space-y-1.5">
        <div><b className="text-fg">Static single assignment (pruned SSA).</b> Every variable is renamed to versions <code className="code">x#0, x#1, …</code> so each name is assigned exactly once. Where control flow merges and several versions could reach, a <b className="text-fg">φ (phi) node</b> picks the version according to the edge taken: <code className="code">i#2 = φ(i#0 [B0], i#3 [B3])</code> means “i#0 if we came from B0, i#3 if from B3”.</div>
        <div>Algorithm: dominators (Cooper–Harvey–Kennedy) → dominance frontiers → φ placement on the iterated frontier of each variable's definitions, <i>pruned</i> by liveness (no φ where the variable is dead) → renaming by a pre-order walk of the dominator tree with version stacks. This SSA form is for analysis/visualisation only; the VM code is generated from the ordinary IR.</div>
        <div className="flex gap-2"><Chip tone="nova">{fn.ssa.phiCount} φ node(s)</Chip><Chip>{fn.ssa.definitions} SSA definitions</Chip></div>
      </Panel>
      <Panel title="Original IR  ⇄  SSA IR, per basic block">
        <table className="w-full code">
          <thead><tr><th className="th w-14">Block</th><th className="th w-1/2">Original IR</th><th className="th">SSA IR</th></tr></thead>
          <tbody>{fn.blocks.map((b) => (
            <tr key={b.id} className={b.reachable ? '' : 'opacity-50'}>
              <td className="td text-nova-strong">B{b.id}{b.labels.length > 0 && <div className="text-fg-mute text-[11px]">{b.labels.join(', ')}</div>}</td>
              <td className="td">{b.instructions.map((i) => <div key={i.index} className="text-fg-dim">{i.text}</div>)}</td>
              <td className="td">{fn.ssa.blocks[b.id].phis.map((p, k) => <div key={'p' + k} className="text-warn">{p.text}</div>)}{fn.ssa.blocks[b.id].code.map((c, k) => <div key={k}>{c}</div>)}</td>
            </tr>))}</tbody>
        </table>
      </Panel>
    </div>
  )
}

export default function Analysis() {
  const { resultSource } = useNova()
  const [tab, setTab] = useState<Tab>('cfg')
  const [which, setWhich] = useState<'original' | 'optimized'>('original')
  const [fnIdx, setFnIdx] = useState(0)
  const [sel, setSel] = useState<CfgBlock | null>(null)
  const tabs: [Tab, string][] = [['cfg', 'Control-flow graph'], ['liveness', 'Liveness'], ['reaching', 'Reaching definitions'], ['ssa', 'SSA & φ nodes']]
  return (
    <div className="h-full flex flex-col min-h-[600px]">
      <PageHeader title="CFG & Data-flow Analysis" subtitle="Basic blocks, control-flow edges, loops (back edges via dominators), iterative data-flow analyses with their full iteration history, and SSA — all computed by the C++ compiler from the IR." />
      <Gate need={(r) => r.analysis}>
        {(r) => {
          const list = r.analysis![which]
          const fn = list[Math.min(fnIdx, list.length - 1)]
          const sm = fn.summary
          return (
            <div className="flex-1 min-h-0 flex flex-col gap-3">
              <div className="flex flex-wrap items-center gap-2">
                <div className="flex gap-1">{list.map((f, i) => <button key={f.name} onClick={() => { setFnIdx(i); setSel(null) }} className={`px-2 h-7 rounded code border ${i === fnIdx ? 'border-nova text-nova bg-nova/10' : 'border-ink-600 text-fg-dim hover:bg-ink-800'}`}>{f.name}</button>)}</div>
                <div className="flex gap-1 ml-2">{(['original', 'optimized'] as const).map((w) => <button key={w} onClick={() => setWhich(w)} className={`px-2 h-7 rounded ${which === w ? 'bg-ink-700 text-fg' : 'text-fg-dim hover:text-fg'}`}>{w} IR</button>)}</div>
                <div className="ml-auto flex gap-2 flex-wrap"><Chip>{sm.blocks} blocks</Chip><Chip>{sm.edges} edges</Chip><Chip tone={sm.loops ? 'warn' : 'neutral'}>{sm.loops} loop(s)</Chip><Chip tone="nova">{sm.phis} φ</Chip><Chip>liveness: {sm.livenessIterations} it</Chip><Chip>reach-defs: {sm.reachingDefsIterations} it</Chip></div>
              </div>
              <div className="flex gap-1 border-b border-ink-700 shrink-0">{tabs.map(([id, label]) => <button key={id} onClick={() => setTab(id)} className={`px-3 h-8 -mb-px border-b-2 ${tab === id ? 'border-nova text-fg' : 'border-transparent text-fg-dim hover:text-fg'}`}>{label}</button>)}</div>
              <div className="flex-1 min-h-0 overflow-auto">
                {tab === 'cfg' && (
                  <div className="h-full min-h-[480px] grid grid-cols-[1fr_300px] gap-3">
                    <Panel title="Basic blocks and edges" bodyClass="overflow-hidden"
                      right={<span className="flex gap-2 text-2xs"><span className="text-ok">● true</span><span className="text-bad">● false</span><span className="text-nova">● jump</span><span className="text-fg-mute">● fall-through</span><span className="text-warn">┅ back edge</span></span>}>
                      <CfgGraph fn={fn} selected={sel?.id} onSelect={setSel} />
                    </Panel>
                    <div className="flex flex-col gap-3 min-h-0">
                      <Panel title={sel ? `Block B${sel.id}` : 'Select a block'} bodyClass="p-3 space-y-1.5">
                        {sel ? (<>
                          <div className="flex gap-1.5 flex-wrap">{sel.loopHeader && <Chip tone="warn">loop header</Chip>}{!sel.reachable && <Chip tone="bad">unreachable</Chip>}<Chip>{sel.instructions.length} instr</Chip></div>
                          <div className="text-fg-dim">predecessors: <span className="code text-fg">{sel.pred.map((p) => 'B' + p).join(', ') || '—'}</span></div>
                          <div className="text-fg-dim">successors: <span className="code text-fg">{sel.succ.map((p) => 'B' + p).join(', ') || '—'}</span></div>
                          <div className="text-fg-dim">immediate dominator: <span className="code text-fg">{sel.idom >= 0 ? 'B' + sel.idom : '—'}</span></div>
                          <div className="text-fg-dim">source lines: <span className="code text-fg">{sel.firstLine ? `${sel.firstLine}–${sel.lastLine}` : '—'}</span></div>
                        </>) : <span className="text-fg-mute">Click a node. Leaders are the first instruction, every label, and every instruction after a jump/return.</span>}
                      </Panel>
                      <Panel title="Source" className="flex-1">{resultSource && <SourceView source={resultSource} highlights={sel && sel.firstLine ? Array.from({ length: sel.lastLine - sel.firstLine + 1 }, (_, i) => ({ line: sel.firstLine + i, col: 1, endLine: sel.firstLine + i, endCol: 200 })) : []} />}</Panel>
                    </div>
                  </div>
                )}
                {tab === 'liveness' && <Dataflow fn={fn} kind="liveness" />}
                {tab === 'reaching' && <Dataflow fn={fn} kind="reaching" />}
                {tab === 'ssa' && <Ssa fn={fn} />}
              </div>
            </div>
          )
        }}
      </Gate>
    </div>
  )
}
