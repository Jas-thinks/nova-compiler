import { useEffect, useMemo, useState, type ReactNode } from 'react'
import { ArrowRight, BarChart3, Loader2 } from 'lucide-react'
import { Chip, Empty, PageHeader, Panel, Stat } from '../components/ui'
import { api } from '../services/api'
import { useNova } from '../state/NovaContext'
import type { SqlPlanNode } from '../types/api'

const PALETTE = ['#7b93ff', '#3fb68b', '#e5a93a', '#f2555a', '#b48cff', '#5cc8e6']
const PASS_COLOR: Record<string, string> = { inlining: '#5cc8e6', constant_propagation: '#7b93ff', constant_folding: '#3fb68b', algebraic: '#e5a93a', cse: '#f2555a', unreachable: '#98a2b3', dce: '#b48cff' }
const SQL_SAMPLE = `CREATE TABLE users (id INT PRIMARY KEY, name TEXT NOT NULL, age INT);
CREATE TABLE orders (oid INT PRIMARY KEY, uid INT, amount FLOAT);
INSERT INTO users VALUES (1,'ann',30),(2,'bob',25),(3,'cy',NULL),(4,'dee',41);
INSERT INTO orders VALUES (10,1,9.5),(11,1,20),(12,2,5.25),(13,9,1);
SELECT u.name, SUM(o.amount) AS total FROM users u, orders o WHERE u.id = o.uid AND u.age > 20 GROUP BY u.name HAVING SUM(o.amount) > 5 ORDER BY total DESC LIMIT 5;`

// ---------------------------------------------------------------------------------------------- small SVG charts
export function LineChart({ points, xLabel, yLabel }: { points: [number, number][]; xLabel: string; yLabel: string }) {
  const W = 640, H = 190, L = 44, B = 28, T = 8, R = 10
  const maxX = Math.max(1, ...points.map((p) => p[0])), maxY = Math.max(1, ...points.map((p) => p[1]))
  const x = (v: number) => L + ((W - L - R) * v) / maxX, y = (v: number) => H - B - ((H - B - T) * v) / maxY
  const d = points.map((p, i) => `${i ? 'L' : 'M'}${x(p[0]).toFixed(1)},${y(p[1]).toFixed(1)}`).join(' ')
  return (
    <svg viewBox={`0 0 ${W} ${H}`} className="w-full h-auto" role="img" aria-label={`${yLabel} over ${xLabel}`}>
      {[0, 0.5, 1].map((f) => <g key={f}><line x1={L} x2={W - R} y1={y(maxY * f)} y2={y(maxY * f)} stroke="#2d3644" strokeDasharray="3 3" /><text x={L - 6} y={y(maxY * f) + 4} textAnchor="end" fontSize="10" fill="#98a2b3">{Math.round(maxY * f)}</text></g>)}
      <path d={`${d} L${x(maxX)},${y(0)} L${x(0)},${y(0)} Z`} fill="#7b93ff" opacity="0.15" />
      <path d={d} fill="none" stroke="#7b93ff" strokeWidth="1.6" />
      <text x={(W + L) / 2} y={H - 6} textAnchor="middle" fontSize="10" fill="#98a2b3">{xLabel}</text>
      <text x={10} y={(H - B) / 2} fontSize="10" fill="#98a2b3" transform={`rotate(-90 10 ${(H - B) / 2})`} textAnchor="middle">{yLabel}</text>
    </svg>
  )
}

export function HBars({ rows, color = '#7b93ff' }: { rows: { label: string; value: number; indent?: number; tone?: string }[]; color?: string }) {
  const max = Math.max(1, ...rows.map((r) => r.value))
  return (
    <div className="space-y-1" role="list">
      {rows.map((r, i) => (
        <div key={i} className="flex items-center gap-2" role="listitem">
          <div className="w-28 shrink-0 code text-[12px] text-fg-dim truncate" style={{ paddingLeft: (r.indent ?? 0) * 10 }} title={r.label}>{r.label}</div>
          <div className="flex-1 h-4 bg-ink-800 rounded overflow-hidden"><div className="h-full rounded" style={{ width: `${(100 * r.value) / max}%`, background: r.tone ?? color }} /></div>
          <div className="w-14 text-right tabular-nums">{r.value.toLocaleString()}</div>
        </div>
      ))}
    </div>
  )
}

export function StackedBars({ rounds, series }: { rounds: number[]; series: { name: string; color: string; values: number[] }[] }) {
  const totals = rounds.map((_, i) => series.reduce((a, s) => a + s.values[i], 0)), max = Math.max(1, ...totals)
  return (
    <div>
      <div className="flex items-end gap-3 h-40 border-b border-ink-600 px-2">
        {rounds.map((r, i) => (
          <div key={r} className="flex-1 flex flex-col-reverse items-stretch justify-start h-full" title={`round ${r}: ${totals[i]} rewrites`}>
            <div className="text-center text-2xs text-fg-mute order-last">{totals[i]}</div>
            {series.map((s) => <div key={s.name} style={{ height: `${(100 * s.values[i]) / max * 0.86}%`, background: s.color }} />)}
          </div>
        ))}
      </div>
      <div className="flex gap-3 px-2 mt-1 text-2xs text-fg-mute">{rounds.map((r) => <div key={r} className="flex-1 text-center">{r === 0 ? 'inlining' : `round ${r}`}</div>)}</div>
      <div className="flex gap-3 flex-wrap mt-3">{series.map((s) => <span key={s.name} className="flex items-center gap-1.5 text-fg-dim"><span className="w-2.5 h-2.5 rounded-sm" style={{ background: s.color }} />{s.name.replace(/_/g, ' ')}</span>)}</div>
    </div>
  )
}

export function Intervals({ nodes }: { nodes: { name: string; color: number; spilled: boolean; first: number; last: number }[] }) {
  const sorted = [...nodes].sort((a, b) => a.first - b.first), max = Math.max(1, ...nodes.map((n) => n.last)) + 1
  return (
    <div className="space-y-1">
      {sorted.map((n) => (
        <div key={n.name} className="flex items-center gap-2">
          <div className="w-16 code text-[12px] text-fg-dim text-right truncate">{n.name}</div>
          <div className="flex-1 h-4 relative bg-ink-800 rounded">
            <div className="absolute top-0 h-full rounded" style={{ left: `${(100 * n.first) / max}%`, width: `${(100 * (n.last - n.first + 1)) / max}%`, background: n.spilled ? '#111' : PALETTE[n.color % PALETTE.length], border: n.spilled ? '1px solid #f2555a' : undefined }} />
          </div>
          <div className="w-16 text-2xs text-fg-mute">{n.spilled ? 'spilled' : `colour ${n.color}`}</div>
        </div>
      ))}
      <div className="text-2xs text-fg-mute pl-[72px]">IR instruction index → ; bars of the same colour never overlap (they can share a register)</div>
    </div>
  )
}

const flatten = (n: SqlPlanNode, d = 0): { label: string; value: number; indent: number }[] => [{ label: n.op, value: Math.max(0, n.rows), indent: d }, ...n.children.flatMap((c) => flatten(c, d + 1))]

function Card({ title, why, children, right }: { title: string; why: ReactNode; children: ReactNode; right?: ReactNode }) {
  return <Panel title={title} right={right} bodyClass="p-3 space-y-2"><p className="text-fg-dim">{why}</p>{children}</Panel>
}

export default function Visualisation() {
  const { result, exec, compile, run, compiling, running } = useNova()
  const [fn, setFn] = useState(0)
  const [plan, setPlan] = useState<SqlPlanNode | null>(null)
  const [planErr, setPlanErr] = useState<string | null>(null)
  const go = (r: string) => { window.location.hash = '/' + r }

  useEffect(() => { api.sql(SQL_SAMPLE).then((r) => setPlan(r.statements.find((s) => s.kind === 'SELECT')?.plan?.tree ?? null)).catch((e) => setPlanErr(String(e))) }, [])

  const depth = useMemo<[number, number][]>(() => (result?.parser.trace ?? []).map((s) => [s.step, Math.ceil(s.stack.length / 2)]), [result])
  const ops = (exec?.execution ?? result?.execution)?.optimized.opCounts
  const opRows = useMemo(() => Object.entries(ops ?? {}).sort((a, b) => b[1] - a[1]).slice(0, 10).map(([label, value]) => ({ label, value })), [ops])
  const opTotal = Object.values(ops ?? {}).reduce((a, b) => a + b, 0)
  const moveShare = ops ? Math.round((100 * ((ops.LOAD ?? 0) + (ops.STORE ?? 0) + (ops.PUSH ?? 0))) / Math.max(1, opTotal)) : 0
  const hist = result?.optimization?.history ?? []
  const rounds = useMemo(() => Array.from(new Set(hist.map((h) => h.round))).sort((a, b) => a - b), [hist])
  const series = useMemo(() => Array.from(new Set(hist.map((h) => h.pass))).map((p) => ({ name: p, color: PASS_COLOR[p] ?? '#98a2b3', values: rounds.map((r) => hist.filter((h) => h.pass === p && h.round === r).length) })), [hist, rounds])
  const fns = result?.analysis?.original ?? []
  const ra = fns[Math.min(fn, Math.max(0, fns.length - 1))]?.registerAllocation

  return (
    <div className="max-w-6xl">
      <PageHeader title="Visualisation"
        subtitle="Data-driven views of the compiler's work on the program in the Playground: how the parser stack grows, what the VM really executes, how the optimizer converges, which variables share registers, and where a SQL query's rows go. Every chart is computed from real compiler output — nothing is mocked."
        right={<div className="flex gap-2"><button className="btn" onClick={() => void compile()} disabled={compiling}>{compiling ? <Loader2 size={14} className="animate-spin" /> : null} Recompile</button><button className="btn btn-primary" onClick={() => void run()} disabled={running}>{running ? <Loader2 size={14} className="animate-spin" /> : <BarChart3 size={14} />} Run to collect VM stats</button></div>} />
      {!result ? <Empty title="Nothing compiled yet" hint="Press Recompile to analyse the current program." /> : (
        <>
          <div className="grid grid-cols-[repeat(auto-fill,minmax(150px,1fr))] gap-3 mb-4">
            <Stat label="Parser steps" value={result.parser.totalSteps} sub={`${result.parser.shifts} shift · ${result.parser.reduces} reduce`} />
            <Stat label="Max stack depth" value={result.parser.maxStackDepth} sub="deepest nesting" />
            <Stat label="Optimizer rewrites" value={hist.length} sub={`${result.optimization?.rounds ?? 0} rounds to fixed point`} />
            <Stat label="IR size change" value={`${(result.optimization?.eliminated ?? 0) > 0 ? '−' : '+'}${Math.abs(result.optimization?.eliminated ?? 0)}`} tone={(result.optimization?.eliminated ?? 0) > 0 ? 'ok' : undefined} sub={`${result.optimization?.instructionsBefore} → ${result.optimization?.instructionsAfter}${(result.optimization?.eliminated ?? 0) < 0 ? ' (inlining grows code first)' : ''}`} />
          </div>
          <div className="grid lg:grid-cols-2 gap-4 mb-4">
            <Card title="LR(1) parser stack depth" why="The stack deepens as the parser enters nested constructs (function, block, expression) and unwinds as it reduces them."
              right={<button className="underline text-fg-dim" onClick={() => go('syntax')}>parse trace <ArrowRight size={11} className="inline" /></button>}>
              {depth.length ? <LineChart points={depth} xLabel="parse step" yLabel="stack depth" /> : <Empty title="No parse trace" />}
              {result.parser.traceTruncated && <Chip tone="warn">trace truncated</Chip>}
            </Card>
            <Card title="What the VM executes" why={ops ? <>{moveShare}% of the {opTotal.toLocaleString()} executed instructions are LOAD / STORE / PUSH — a stack machine mostly moves values. That is the case for register allocation in the native back end.</> : 'Press “Run to collect VM stats” to execute the program and count instructions.'}
              right={<button className="underline text-fg-dim" onClick={() => go('native')}>native backend <ArrowRight size={11} className="inline" /></button>}>
              {opRows.length ? <HBars rows={opRows} /> : <Empty title="No execution data yet" />}
            </Card>
            <Card title="Optimizer rewrites per round" why="Round 1 does the most varied work; later rounds clean up what it exposed until no pass changes anything (the fixed point)."
              right={<button className="underline text-fg-dim" onClick={() => go('optimizer')}>optimization lab <ArrowRight size={11} className="inline" /></button>}>
              {rounds.length ? <StackedBars rounds={rounds} series={series} /> : <Empty title="No rewrites" hint="The program was already optimal, or optimization is switched off." />}
            </Card>
            <Card title="Live ranges and register colours" why="Each bar is a variable's live range; the graph-colouring allocator gives overlapping variables different colours."
              right={<div className="flex gap-1">{fns.map((f, i) => <button key={f.name} onClick={() => setFn(i)} className={`btn h-6 px-2 ${i === fn ? 'btn-primary' : ''}`}>{f.name}</button>)}</div>}>
              {ra ? <><div className="flex gap-2 flex-wrap"><Chip tone="nova">K = {ra.k}</Chip><Chip>{ra.colorsUsed} colours</Chip><Chip tone={ra.spills ? 'warn' : 'ok'}>{ra.spills} spills</Chip><Chip>min K without spills: {ra.minimalK}</Chip></div><Intervals nodes={ra.nodes} /></> : <Empty title="No register data" />}
            </Card>
          </div>
        </>
      )}
      <Card title="Where a SQL query's rows go" why="Measured rows per operator of an optimized query plan (data flows bottom-up). Predicate pushdown and the hash join shrink the join input — the funnel shape of a good plan."
        right={<button className="underline text-fg-dim" onClick={() => go('sql')}>SQL compiler <ArrowRight size={11} className="inline" /></button>}>
        {plan ? <HBars rows={flatten(plan)} color="#3fb68b" /> : planErr ? <div className="text-bad">{planErr}</div> : <Empty title="Loading…" />}
      </Card>
      <div className="grid md:grid-cols-3 gap-3 mt-4">
        {([['syntax', 'Parse trace', 'Replay every shift/reduce with the stack and the reason.'], ['analysis', 'CFG & data-flow', 'Blocks, edges, dominators, liveness iterations, SSA.'], ['debugger', 'Debugger', 'Step the VM forward and backward; frames, locals, stack.']] as const).map(([to, t, d]) => (
          <button key={to} onClick={() => go(to)} className="panel p-3 text-left hover:border-nova/50 transition-colors"><div className="font-semibold">{t} <ArrowRight size={12} className="inline" /></div><div className="text-fg-dim">{d}</div></button>
        ))}
      </div>
    </div>
  )
}
