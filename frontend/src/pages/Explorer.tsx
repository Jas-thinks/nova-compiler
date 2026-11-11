import { useEffect, useState } from 'react'
import { Loader2, Play, Wrench } from 'lucide-react'
import { Chip, Empty, PageHeader, Panel } from '../components/ui'
import { api } from '../services/api'
import type { ExplorerCResult, ExplorerLanguage } from '../types/api'

const SAMPLE = `int sum_squares(int n) {
    int s = 0;
    for (int i = 0; i < n; i++)
        s += i * i;
    return s;
}
`
const STATUS: Record<string, { tone: 'ok' | 'warn' | 'bad' | 'neutral' | 'nova'; label: string }> = {
  full: { tone: 'ok', label: 'full pipeline' }, available: { tone: 'nova', label: 'viewer available' },
  unavailable: { tone: 'warn', label: 'toolchain missing' }, not_implemented: { tone: 'neutral', label: 'not implemented' },
}

export default function Explorer() {
  const [langs, setLangs] = useState<ExplorerLanguage[]>([])
  const [src, setSrc] = useState(SAMPLE)
  const [opt, setOpt] = useState(1)
  const [res, setRes] = useState<ExplorerCResult | null>(null)
  const [tab, setTab] = useState('')
  const [busy, setBusy] = useState(false)
  const [err, setErr] = useState<string | null>(null)
  useEffect(() => { api.explorerLanguages().then(setLangs).catch((e: Error) => setErr(e.message)) }, [])
  const c = langs.find((l) => l.id === 'c')

  const run = async () => {
    setBusy(true); setErr(null)
    try { const r = await api.explorerC(src, opt); setRes(r); setTab(Object.keys(r.outputs)[0] ?? '') } catch (e) { setErr(e instanceof Error ? e.message : String(e)); setRes(null) } finally { setBusy(false) }
  }
  return (
    <div className="max-w-6xl">
      <PageHeader title="Compiler Explorer" subtitle="A language-adapter foundation. NOVA has the full pipeline (use the other pages). Other languages are thin viewers over real installed toolchains — nothing here is simulated, and unsupported languages are listed as such." />
      <div className="grid grid-cols-[repeat(auto-fill,minmax(230px,1fr))] gap-3 mb-4">
        {langs.map((l) => {
          const st = STATUS[l.status]
          return (
            <div key={l.id} className="panel p-3">
              <div className="flex items-center gap-2 mb-1"><b>{l.name}</b><Chip tone={st.tone}>{st.label}</Chip></div>
              <p className="text-fg-dim text-[12px] leading-snug">{l.description}</p>
              {l.outputs.length > 0 && <div className="flex gap-1 flex-wrap mt-2">{l.outputs.map((o) => <Chip key={o}>{o}</Chip>)}</div>}
              {l.toolchain && <div className="text-2xs text-fg-mute mt-2">toolchain: <span className="code">{l.toolchain}</span></div>}
              {l.note && <div className="text-2xs text-warn mt-2">{l.note}</div>}
              {l.next_step && <div className="text-2xs text-fg-mute mt-1">{l.next_step}</div>}
            </div>)
        })}
      </div>
      <Panel title={`C viewer${c?.toolchain ? ' · ' + c.toolchain : ''}`} right={<span className="flex items-center gap-2">
        <label className="text-fg-dim">opt <select value={opt} onChange={(e) => setOpt(+e.target.value)} className="bg-ink-800 border border-ink-600 rounded h-6 px-1">{[0, 1, 2, 3].map((o) => <option key={o} value={o}>-O{o}</option>)}</select></label>
        <button className="btn btn-primary h-7" disabled={busy || c?.status !== 'available'} onClick={() => void run()}>{busy ? <Loader2 size={13} className="animate-spin" /> : <Play size={13} />} Compile</button></span>}>
        {c?.status !== 'available' ? <Empty icon={<Wrench size={24} />} title="No C toolchain on the server" hint={c?.note ?? ''} /> : (
          <div className="grid md:grid-cols-2 min-h-[320px] divide-x divide-ink-700">
            <textarea value={src} onChange={(e) => setSrc(e.target.value)} spellCheck={false} className="bg-transparent code p-3 outline-none resize-none min-h-[320px]" />
            <div className="min-w-0 flex flex-col">
              {err && <div className="p-3 text-bad whitespace-pre-wrap">{err}</div>}
              {res ? (<>
                <div className="flex gap-1 p-2 border-b border-ink-700">{Object.keys(res.outputs).map((k) => <button key={k} onClick={() => setTab(k)} className={`px-2 h-6 rounded ${tab === k ? 'bg-ink-700 text-fg' : 'text-fg-dim hover:text-fg'}`}>{k}</button>)}<Chip tone={res.ok ? 'ok' : 'bad'}>{res.ok ? 'compiled' : 'failed'}</Chip></div>
                <pre className="code p-3 overflow-auto flex-1 text-[11.5px] leading-[1.45]">{res.outputs[tab] || '(empty)'}</pre>
                {res.diagnostics && <pre className={`code p-3 border-t border-ink-700 max-h-40 overflow-auto text-[11.5px] ${res.ok ? 'text-warn' : 'text-bad'}`}>{res.diagnostics}</pre>}
                <div className="px-3 py-1.5 border-t border-ink-700 text-2xs text-fg-mute">{res.note}</div>
              </>) : !err && <div className="p-6 text-fg-mute">Press Compile to see real toolchain output.</div>}
            </div>
          </div>)}
      </Panel>
      <p className="text-fg-mute mt-3 text-[12px]">Limits: compile-only (never executed), 16 KiB source, header allow-list, no full C/C++ project support. Adding another language means implementing one adapter in <code className="code">backend/app/explorer.py</code> that wraps a real tool.</p>
    </div>
  )
}
