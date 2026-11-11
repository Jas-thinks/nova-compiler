import { useCallback, useEffect, useRef, useState, type ReactNode } from 'react'
import { BadgeCheck, Bot, Bug, CheckCircle2, Loader2, Sparkles, Wand2, XCircle } from 'lucide-react'
import { Chip, Empty, PageHeader, Panel } from '../components/ui'
import { api } from '../services/api'
import { useNova } from '../state/NovaContext'
import type { AiResponse, AutofixResponse, QuickFix, RuntimeExplanation, SuggestResponse } from '../types/api'

type Tab = 'fixes' | 'autofix' | 'runtime' | 'claude'

export function DiffView({ diff }: { diff: string }) {
  if (!diff.trim()) return <div className="text-fg-mute p-2">No changes.</div>
  return (
    <pre className="code text-[12px] leading-5 overflow-auto rounded-md bg-ink-950 border border-ink-700 p-2">
      {diff.split('\n').map((l, i) => {
        const cls = l.startsWith('+++') || l.startsWith('---') ? 'text-fg-mute' : l.startsWith('+') ? 'text-ok bg-ok/10' : l.startsWith('-') ? 'text-bad bg-bad/10' : l.startsWith('@@') ? 'text-nova' : 'text-fg-dim'
        return <div key={i} className={cls}>{l || ' '}</div>
      })}
    </pre>
  )
}

function Md({ text }: { text: string }) {   // minimal inline-code renderer for `backticks`
  return <>{text.split(/(`[^`]+`)/g).map((p, i) => p.startsWith('`') ? <code key={i} className="code px-1 rounded bg-ink-800 text-nova-strong">{p.slice(1, -1)}</code> : <span key={i}>{p}</span>)}</>
}

function Card({ children, tone }: { children: ReactNode; tone?: 'ok' | 'bad' }) {
  return <div className={`panel p-3 space-y-2 ${tone === 'ok' ? 'border-ok/40' : tone === 'bad' ? 'border-bad/40' : ''}`}>{children}</div>
}

export default function Assistant() {
  const { source, setSource, examples, loadExample } = useNova()
  const [tab, setTab] = useState<Tab>('fixes')
  const [sug, setSug] = useState<SuggestResponse | null>(null)
  const [auto, setAuto] = useState<AutofixResponse | null>(null)
  const [rt, setRt] = useState<RuntimeExplanation | null>(null)
  const [ai, setAi] = useState<{ kind: 'fix' | 'debug'; r: AiResponse } | null>(null)
  const [busy, setBusy] = useState<string | null>(null)
  const [err, setErr] = useState<string | null>(null)
  const timer = useRef<number | undefined>(undefined)

  const wrap = useCallback(async <T,>(name: string, fn: () => Promise<T>, set: (v: T) => void) => {
    setBusy(name); setErr(null)
    try { set(await fn()) } catch (e) { setErr(e instanceof Error ? e.message : String(e)) } finally { setBusy(null) }
  }, [])

  // live, debounced suggestions while the user edits
  useEffect(() => {
    window.clearTimeout(timer.current)
    timer.current = window.setTimeout(() => { void wrap('suggest', () => api.suggest(source), setSug); setAuto(null) }, 500)
    return () => window.clearTimeout(timer.current)
  }, [source, wrap])

  const apply = (s: string) => { setSource(s); setTab('fixes') }
  const verified = sug?.fixes.filter((f) => f.verified) ?? []
  const errors = sug?.diagnostics.filter((d) => d.severity === 'error') ?? []

  const fixCard = (f: QuickFix, i: number) => (
    <Card key={i} tone={f.verified ? 'ok' : undefined}>
      <div className="flex items-start gap-2">
        <div className="flex-1 min-w-0">
          <div className="font-medium">{f.title}</div>
          <div className="text-fg-mute text-[12px]">line {f.line} · {f.code} — {f.diagnostic}</div>
        </div>
        {f.verified ? <Chip tone="ok"><BadgeCheck size={11} /> verified by compiler</Chip> : <Chip tone="warn">not verified</Chip>}
      </div>
      <div className="flex gap-1.5 flex-wrap text-2xs">
        <Chip>{f.errorsBefore} → {f.errorsAfter} error{f.errorsAfter === 1 ? '' : 's'}</Chip>
        {f.compilesAfter && <Chip tone="ok">program compiles after fix</Chip>}
      </div>
      <DiffView diff={f.diff} />
      <button className="btn btn-primary" onClick={() => apply(f.patched)}><Wand2 size={14} /> Apply fix</button>
    </Card>
  )

  return (
    <div className="h-full flex flex-col">
      <PageHeader title="AI Assistant"
        subtitle="Debugging help in two layers. The rule-based engines work offline: every quick fix is applied to your code and re-compiled, and only fixes the real compiler accepts are marked verified. The optional Claude layer proposes fixes that are re-verified the same way — the compiler, never the model, decides what is correct."
        right={<select className="btn" value="" onChange={(e) => { const ex = examples.find((x) => x.id === e.target.value); if (ex) loadExample(ex) }}>
          <option value="">Load a broken example…</option>
          {examples.filter((x) => x.category === 'errors' || x.category === 'runtime').map((x) => <option key={x.id} value={x.id}>{x.title}</option>)}
        </select>} />
      {err && <div className="panel border-bad/40 p-3 text-bad mb-3">{err}</div>}
      <div className="flex-1 min-h-0 grid grid-cols-1 xl:grid-cols-[minmax(320px,5fr)_7fr] gap-3">
        <Panel title="Program (edit freely — suggestions update live)"
          right={sug && <>{sug.compiles ? <Chip tone="ok">compiles</Chip> : <Chip tone="bad">{sug.errors} error{sug.errors === 1 ? '' : 's'}</Chip>}{busy === 'suggest' && <Loader2 size={12} className="animate-spin" />}</>}>
          <div className="h-full flex flex-col">
            <textarea className="flex-1 min-h-[260px] resize-none bg-transparent code p-3 outline-none leading-5" spellCheck={false} value={source} onChange={(e) => setSource(e.target.value)} />
            {errors.length > 0 && (
              <div className="border-t border-ink-700 max-h-48 overflow-auto">
                {errors.map((d, i) => (
                  <div key={i} className="px-3 py-1.5 border-b border-ink-800 flex gap-2"><XCircle size={14} className="text-bad mt-0.5 shrink-0" />
                    <span><b className="text-bad">{d.code}</b> <span className="text-fg-mute">{d.loc.line}:{d.loc.col}</span> {d.message}</span></div>
                ))}
              </div>
            )}
          </div>
        </Panel>

        <div className="min-h-0 flex flex-col gap-3">
          <div className="flex gap-1 flex-wrap">
            {([['fixes', 'Quick fixes', <Sparkles size={13} key="a" />], ['autofix', 'Auto-fix all', <Wand2 size={13} key="b" />], ['runtime', 'Runtime analysis', <Bug size={13} key="c" />], ['claude', 'Ask Claude', <Bot size={13} key="d" />]] as [Tab, string, ReactNode][]).map(([id, label, icon]) => (
              <button key={id} onClick={() => setTab(id)} className={`btn ${tab === id ? 'btn-primary' : ''}`}>{icon}{label}{id === 'fixes' && verified.length > 0 && <span className="ml-1 text-2xs opacity-80">{verified.length}</span>}</button>
            ))}
          </div>

          <div className="flex-1 min-h-0 overflow-auto space-y-3 pr-1">
            {tab === 'fixes' && (!sug ? <Empty title="Analyzing…" /> : sug.fixes.length === 0
              ? <Empty icon={sug.compiles ? <CheckCircle2 className="text-ok" /> : undefined} title={sug.compiles ? 'Nothing to fix — the program compiles cleanly' : 'No automatic fix available'} hint={sug.compiles ? undefined : 'These errors need a human decision (for example a wrong argument count). Try “Ask Claude”.'} />
              : sug.fixes.map(fixCard))}

            {tab === 'autofix' && (
              <>
                <button className="btn btn-primary" disabled={busy === 'auto'} onClick={() => void wrap('auto', () => api.autofix(source), setAuto)}>
                  {busy === 'auto' ? <Loader2 size={14} className="animate-spin" /> : <Wand2 size={14} />} Fix everything automatically
                </button>
                <p className="text-fg-mute">Applies the first verified fix to the first error, re-compiles, and repeats until the program compiles or no automatic fix remains.</p>
                {auto && (
                  <Card tone={auto.status === 'fixed' ? 'ok' : auto.status === 'partial' ? undefined : 'bad'}>
                    <div className="flex items-center gap-2">
                      <Chip tone={auto.status === 'fixed' ? 'ok' : auto.status === 'partial' ? 'warn' : 'bad'}>{auto.status === 'fixed' ? 'fully fixed' : auto.status === 'partial' ? `partially fixed — ${auto.remainingErrors} error(s) need you` : 'no automatic fix'}</Chip>
                    </div>
                    <ol className="list-decimal pl-5 space-y-1">{auto.steps.map((s, i) => <li key={i}>{s.title} <span className="text-fg-mute">(line {s.line}, {s.code}: {s.errorsBefore} → {s.errorsAfter} errors)</span></li>)}</ol>
                    {auto.fixedSource && <><DiffView diff={auto.diff} /><button className="btn btn-primary" onClick={() => apply(auto.fixedSource!)}><Wand2 size={14} /> Apply all changes</button></>}
                  </Card>
                )}
              </>
            )}

            {tab === 'runtime' && (
              <>
                <button className="btn btn-primary" disabled={busy === 'rt'} onClick={() => void wrap('rt', () => api.explainRuntime(source), setRt)}>
                  {busy === 'rt' ? <Loader2 size={14} className="animate-spin" /> : <Bug size={14} />} Run and explain the failure
                </button>
                <p className="text-fg-mute">Runs the program on the VM with tracing and derives the root cause from the recorded failing state (variables, source line, last steps).</p>
                {rt?.status === 'no_runtime_error' && <Card tone="ok"><b>No runtime error.</b> Output: <span className="code">{rt.output?.join(', ') || '(none)'}</span></Card>}
                {rt?.status === 'compile_error' && <Card tone="bad">The program does not compile yet — use the quick fixes first.</Card>}
                {rt?.status === 'runtime_error' && (
                  <Card tone="bad">
                    <div className="flex items-center gap-2"><Chip tone="bad">{rt.error?.message}</Chip><span className="text-fg-mute">line {rt.error?.line} in {rt.function}()</span></div>
                    <pre className="code panel p-2 text-warn">{rt.sourceLine}</pre>
                    <div><div className="text-2xs uppercase tracking-wider text-fg-mute mb-1">Root cause</div><ul className="space-y-1">{rt.analysis?.map((a, i) => <li key={i}>• <Md text={a} /></li>)}</ul></div>
                    <div><div className="text-2xs uppercase tracking-wider text-fg-mute mb-1">Suggested fix</div><ul className="space-y-1 text-ok">{rt.suggestions?.map((a, i) => <li key={i}>→ <Md text={a} /></li>)}</ul></div>
                    {rt.variables && Object.keys(rt.variables).length > 0 && (
                      <div><div className="text-2xs uppercase tracking-wider text-fg-mute mb-1">Variables at the moment of failure</div>
                        <div className="flex gap-1.5 flex-wrap">{Object.entries(rt.variables).map(([k, v]) => <Chip key={k} tone={v === '0' ? 'warn' : 'neutral'}>{k} = {v}</Chip>)}</div></div>
                    )}
                    <details className="text-fg-mute"><summary className="cursor-pointer">Last VM steps</summary><pre className="code mt-1">{rt.evidence?.join('\n')}</pre></details>
                  </Card>
                )}
              </>
            )}

            {tab === 'claude' && (
              <>
                <div className="flex gap-2 flex-wrap">
                  <button className="btn btn-primary" disabled={!!busy} onClick={() => void wrap('aifix', () => api.aiFix(source), (r) => setAi({ kind: 'fix', r }))}>{busy === 'aifix' ? <Loader2 size={14} className="animate-spin" /> : <Bot size={14} />} Ask Claude to fix compile errors</button>
                  <button className="btn" disabled={!!busy} onClick={() => void wrap('aidbg', () => api.aiDebug(source), (r) => setAi({ kind: 'debug', r }))}>{busy === 'aidbg' ? <Loader2 size={14} className="animate-spin" /> : <Bug size={14} />} Ask Claude why it crashed</button>
                </div>
                <p className="text-fg-mute">Needs <span className="code">ANTHROPIC_API_KEY</span> on the server. A proposed fix is only shown if the NOVA compiler accepts it (up to 3 attempts, each retry fed the new diagnostics).</p>
                {ai && !ai.r.available && <Card><b>Claude is not configured on this server.</b><div className="text-fg-dim">{ai.r.note}</div><div className="text-fg-mute">Everything else on this page works without it.</div></Card>}
                {ai?.r.available && ai.r.status === 'ai_error' && <Card tone="bad">{ai.r.note}</Card>}
                {ai?.r.available && ai.kind === 'fix' && ai.r.status === 'fixed' && ai.r.fixedSource && <Card tone="ok"><Chip tone="ok"><BadgeCheck size={11} /> verified by compiler</Chip><DiffView diff={ai.r.diff ?? ''} /><button className="btn btn-primary" onClick={() => apply(ai.r.fixedSource!)}>Apply</button></Card>}
                {ai?.r.available && ai.kind === 'fix' && ai.r.status && ai.r.status !== 'fixed' && ai.r.status !== 'ai_error' && <Card>{ai.r.status === 'nothing_to_fix' ? 'The program already compiles.' : ai.r.note ?? ai.r.status}</Card>}
                {ai?.r.available && ai.kind === 'debug' && ai.r.analysis && <Card><div className="whitespace-pre-wrap">{ai.r.analysis}</div></Card>}
              </>
            )}
          </div>
        </div>
      </div>
    </div>
  )
}
