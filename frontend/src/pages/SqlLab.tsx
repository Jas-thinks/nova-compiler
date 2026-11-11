import { useCallback, useEffect, useMemo, useState } from 'react'
import { Loader2, Play } from 'lucide-react'
import { Chip, Empty, PageHeader, Panel, Stat } from '../components/ui'
import { api } from '../services/api'
import type { SqlPlanNode, SqlResponse, SqlStatement } from '../types/api'

const EXAMPLES: Record<string, string> = {
  'Join + GROUP BY + HAVING': `CREATE TABLE users (id INT PRIMARY KEY, name TEXT NOT NULL, age INT, city TEXT);
CREATE TABLE orders (oid INT PRIMARY KEY, uid INT, amount FLOAT);
INSERT INTO users VALUES (1,'ann',30,'NYC'),(2,'bob',25,'LA'),(3,'cy',NULL,'NYC'),(4,'dee',41,'SF');
INSERT INTO orders VALUES (10,1,9.5),(11,1,20),(12,2,5.25),(13,9,1);

SELECT u.name, SUM(o.amount) AS total, COUNT(*) AS n
FROM users u JOIN orders o ON u.id = o.uid
WHERE u.age > 20
GROUP BY u.name
HAVING SUM(o.amount) > 5
ORDER BY total DESC
LIMIT 5;`,
  'LEFT JOIN + NULLs': `CREATE TABLE users (id INT PRIMARY KEY, name TEXT NOT NULL);
CREATE TABLE orders (oid INT PRIMARY KEY, uid INT, amount FLOAT);
INSERT INTO users VALUES (1,'ann'),(2,'bob'),(3,'cy');
INSERT INTO orders VALUES (10,1,9.5),(11,2,NULL);
SELECT u.name, o.amount, o.amount IS NULL AS missing FROM users u LEFT JOIN orders o ON u.id = o.uid ORDER BY 1;`,
  'Optimizer: cross join → hash join': `CREATE TABLE a (id INT, v INT);
CREATE TABLE b (id INT, w INT);
INSERT INTO a VALUES (1,10),(2,20),(3,30);
INSERT INTO b VALUES (1,100),(3,300),(4,400);
SELECT a.v, b.w FROM a, b WHERE a.id = b.id AND a.v > 5 AND b.w < 1000 ORDER BY 1 LIMIT 10;`,
  'Type error with suggestion': `CREATE TABLE users (id INT PRIMARY KEY, name TEXT NOT NULL, age INT);
INSERT INTO users VALUES (1,'ann',30);
SELECT nme FROM users WHERE age = 'old';`,
}

const OP_TONE: Record<string, string> = {
  Scan: 'border-ok/50 text-ok bg-ok/10', Filter: 'border-warn/50 text-warn bg-warn/10', Join: 'border-nova/50 text-nova bg-nova/10',
  Aggregate: 'border-bad/40 text-bad bg-bad/10', Project: 'border-ink-500 text-fg-dim bg-ink-800', Sort: 'border-ink-500 text-fg-dim bg-ink-800',
  Limit: 'border-ink-500 text-fg-dim bg-ink-800', Distinct: 'border-ink-500 text-fg-dim bg-ink-800', OneRow: 'border-ink-500 text-fg-dim bg-ink-800',
}
const maxRows = (n: SqlPlanNode): number => Math.max(n.rows, ...n.children.map(maxRows))

function PlanTree({ n, max, root = true }: { n: SqlPlanNode; max: number; root?: boolean }) {
  return (
    <div className={root ? '' : 'ml-5 pl-3 border-l border-ink-600'}>
      <div className="flex items-center gap-2 my-1">
        <span className={`chip shrink-0 ${OP_TONE[n.op] ?? ''}`}>{n.op}</span>
        <span className="code text-[12px] text-fg-dim truncate" title={n.detail}>{n.detail.replace(/^\w+\s*/, '')}</span>
        {n.rows >= 0 && (
          <span className="ml-auto flex items-center gap-2 shrink-0" title="rows produced by this operator">
            <span className="w-24 h-1.5 rounded bg-ink-700 overflow-hidden"><span className="block h-full bg-nova" style={{ width: `${(100 * n.rows) / Math.max(1, max)}%` }} /></span>
            <span className="tabular-nums w-14 text-right">{n.rows} rows</span>
          </span>
        )}
      </div>
      {n.children.map((c, i) => <PlanTree key={i} n={c} max={max} root={false} />)}
    </div>
  )
}

type Tab = 'results' | 'plan' | 'optimizer' | 'bytecode' | 'ast'

function Cell({ v }: { v: string | number | null }) {
  if (v === null) return <span className="text-fg-mute italic">NULL</span>
  return <span className={typeof v === 'number' ? 'text-nova-strong' : ''}>{String(v)}</span>
}

function Result({ s }: { s: SqlStatement }) {
  if (s.kind !== 'SELECT') return <div className="p-3 text-fg-dim">{s.ok ? <>✓ {s.kind} — {s.rowsAffected} row(s) affected</> : 'failed'}</div>
  return (
    <div className="overflow-auto">
      <table className="w-full text-left">
        <thead className="sticky top-0 bg-ink-800"><tr>{s.columns.map((c, i) => <th key={i} className="px-3 py-1.5 font-medium border-b border-ink-600">{c}<span className="ml-1.5 text-2xs text-fg-mute">{s.types[i]}</span></th>)}</tr></thead>
        <tbody>{s.rows.map((r, i) => <tr key={i} className="border-b border-ink-700/60 hover:bg-ink-800">{r.map((v, j) => <td key={j} className="px-3 py-1 code"><Cell v={v} /></td>)}</tr>)}</tbody>
      </table>
      {s.rows.length === 0 && <Empty title="0 rows" />}
      {s.truncated && <div className="p-2 text-warn text-2xs">Showing the first {s.rows.length} of {s.rowsAffected} rows.</div>}
    </div>
  )
}

export default function SqlLab() {
  const [name, setName] = useState(Object.keys(EXAMPLES)[0])
  const [script, setScript] = useState(EXAMPLES[Object.keys(EXAMPLES)[0]])
  const [res, setRes] = useState<SqlResponse | null>(null)
  const [busy, setBusy] = useState(false)
  const [err, setErr] = useState<string | null>(null)
  const [sel, setSel] = useState(0)
  const [tab, setTab] = useState<Tab>('results')

  const run = useCallback(async () => {
    setBusy(true); setErr(null)
    try {
      const r = await api.sql(script)
      setRes(r)
      const lastSelect = [...r.statements].reverse().find((s) => s.kind === 'SELECT')
      setSel(lastSelect ? lastSelect.index : Math.max(0, r.statements.length - 1))
    } catch (e) { setErr(e instanceof Error ? e.message : String(e)) } finally { setBusy(false) }
  }, [script])

  useEffect(() => { void run() }, []) // eslint-disable-line react-hooks/exhaustive-deps
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => { if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') { e.preventDefault(); void run() } }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [run])

  const stmt = res?.statements[sel]
  const stats = useMemo(() => (stmt ? Object.entries(stmt.stats) : []), [stmt])

  return (
    <div className="h-full flex flex-col">
      <PageHeader title="SQL Compiler" subtitle="NOVA-SQL is a second compiler in this project: lexer → recursive-descent parser → binder/type checker (3-valued NULL logic) → logical plan → rule-based optimizer → expression bytecode VM → executor. Verified against SQLite with a differential fuzzer."
        right={<div className="flex items-center gap-2">
          <select className="btn" value={name} onChange={(e) => { setName(e.target.value); setScript(EXAMPLES[e.target.value]) }}>{Object.keys(EXAMPLES).map((k) => <option key={k}>{k}</option>)}</select>
          <button className="btn btn-primary" onClick={() => void run()} disabled={busy} title="Ctrl+Enter">{busy ? <Loader2 size={14} className="animate-spin" /> : <Play size={14} />} Run</button>
        </div>} />
      {err && <div className="panel border-bad/40 p-3 text-bad mb-3">{err}</div>}
      <div className="flex-1 min-h-0 grid grid-cols-1 lg:grid-cols-2 gap-3">
        <Panel title="SQL script" right={res && <><Chip tone={res.success ? 'ok' : 'bad'}>{res.success ? 'ok' : `${res.errorCount} error(s)`}</Chip><Chip>{res.tokenCount} tokens</Chip><Chip>{res.elapsedMs.toFixed(1)} ms</Chip></>}>
          <textarea className="w-full h-full min-h-[240px] resize-none bg-transparent code p-3 outline-none" spellCheck={false} value={script} onChange={(e) => setScript(e.target.value)} />
        </Panel>
        <div className="min-h-0 flex flex-col gap-3">
          {res && res.diagnostics.length > 0 && (
            <Panel title="Diagnostics" bodyClass="max-h-44">
              {res.diagnostics.map((d, i) => (
                <div key={i} className="px-3 py-2 border-b border-ink-700/60">
                  <b className={d.severity === 'error' ? 'text-bad' : 'text-warn'}>{d.code}</b> {d.category} at {d.loc.line}:{d.loc.col} — {d.message}
                  {d.suggestion && <div className="text-nova-strong mt-0.5">💡 {d.suggestion}</div>}
                </div>
              ))}
            </Panel>
          )}
          {res && res.catalog.length > 0 && (
            <div className="flex gap-2 flex-wrap">{res.catalog.map((t) => <Chip key={t.name} tone="nova" title={t.columns.map((c) => `${c.name} ${c.type}${c.primaryKey ? ' PK' : ''}${c.notNull ? ' NOT NULL' : ''}`).join(', ')}>{t.name} · {t.rows} rows</Chip>)}</div>
          )}
          {res && res.statements.length > 0 && (
            <Panel title="Statements" bodyClass="max-h-40">
              {res.statements.map((s) => (
                <button key={s.index} onClick={() => setSel(s.index)} className={`w-full text-left px-3 py-1 flex gap-2 items-center border-l-2 ${sel === s.index ? 'border-nova bg-nova/10' : 'border-transparent hover:bg-ink-800'}`}>
                  <span className="shrink-0 whitespace-nowrap"><Chip tone={s.skipped ? 'neutral' : s.ok ? 'ok' : 'bad'}>{s.kind}</Chip></span><span className="truncate code text-fg-dim">{s.text}</span>
                </button>
              ))}
            </Panel>
          )}
          {stmt && (
            <Panel title={<span className="flex gap-1">{(['results', 'plan', 'optimizer', 'bytecode', 'ast'] as Tab[]).map((t) => <button key={t} onClick={() => setTab(t)} className={`px-2 py-0.5 rounded ${tab === t ? 'bg-nova/20 text-fg' : 'text-fg-dim hover:text-fg'}`}>{t}</button>)}</span>}
              right={<span className="code text-fg-mute">{stmt.elapsedMs.toFixed(2)} ms</span>} className="flex-1">
              {tab === 'results' && <Result s={stmt} />}
              {tab === 'plan' && (stmt.plan ? <div className="p-3 space-y-3"><div><div className="text-2xs uppercase text-fg-mute mb-1">Optimized plan — data flows bottom-up; bars show measured rows per operator</div><PlanTree n={stmt.plan.tree} max={maxRows(stmt.plan.tree)} /></div><details className="text-fg-mute"><summary className="cursor-pointer">Plan as written (before optimization)</summary><pre className="code mt-1 whitespace-pre-wrap">{stmt.plan.original}</pre></details><div className="flex gap-2 flex-wrap">{stats.map(([k, v]) => <Stat key={k} label={k.replace(/([A-Z])/g, ' $1').toLowerCase()} value={v} />)}</div></div> : <Empty title="No plan" hint="Only SELECT statements have a query plan." />)}
              {tab === 'optimizer' && (stmt.plan ? (stmt.plan.history.length === 0 ? <Empty title="No rewrite applied" hint="The plan was already optimal for the available rules." /> : <div className="p-3 space-y-4">{stmt.plan.history.map((h, i) => (
                <div key={i}><div className="flex items-center gap-2"><Chip tone="nova">{h.rule}</Chip></div><div className="text-fg-dim my-1">{h.description}</div>
                  <div className="grid grid-cols-2 gap-2"><pre className="code panel p-2 whitespace-pre-wrap">{h.before}</pre><pre className="code panel p-2 whitespace-pre-wrap">{h.after}</pre></div></div>))}</div>) : <Empty title="No plan" />)}
              {tab === 'bytecode' && (stmt.bytecode.length === 0 ? <Empty title="No expressions" /> : <div className="p-3 grid gap-3">{stmt.bytecode.map((b, i) => <div key={i}><div className="code text-nova-strong">{b.source}</div><pre className="code text-fg-dim">{b.code.join('\n')}</pre></div>)}</div>)}
              {tab === 'ast' && <pre className="code p-3 whitespace-pre-wrap">{stmt.ast}</pre>}
            </Panel>
          )}
        </div>
      </div>
    </div>
  )
}
