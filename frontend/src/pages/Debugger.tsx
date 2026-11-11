import { lazy, Suspense, useEffect, useMemo, useState } from 'react'
import { ArrowDownToLine, ArrowRightToLine, ChevronsRight, Loader2, Play, RotateCcw, StepBack, StepForward, SkipForward } from 'lucide-react'
import { Chip, Empty, PageHeader, Panel } from '../components/ui'
import BytecodeListing from '../features/bytecode/BytecodeListing'
import { useNova } from '../state/NovaContext'
import type { DebugFrame } from '../types/api'

const CodeEditor = lazy(() => import('../components/CodeEditor'))

export function Watch({ frame, showTemps }: { frame: DebugFrame; showTemps: boolean }) {
  const rows = frame.locals.filter((l) => showTemps || !l.temp)
  if (rows.length === 0) return <div className="p-3 text-fg-mute">No variables in this frame.</div>
  return (
    <table className="w-full code">
      <thead><tr><th className="th">Name</th><th className="th">Type</th><th className="th">Value</th></tr></thead>
      <tbody>{rows.map((l) => (
        <tr key={l.slot} className="hover:bg-ink-850"><td className="td">{l.name}{l.param && <span className="text-nova text-2xs"> param</span>}{l.temp && <span className="text-fg-mute text-2xs"> temp</span>}</td><td className="td text-fg-dim">{l.type}</td><td className="td text-warn">{l.value}</td></tr>
      ))}</tbody>
    </table>
  )
}

export default function Debugger() {
  const { source, result, debug, debugStart, debugStep, debugContinue, debugReset, debugToggleBreakpoint, debugSetTarget, compile } = useNova()
  const [showTemps, setShowTemps] = useState(false)
  const st = debug.state
  const stale = st !== null && debug.source !== source
  const busy = debug.loading
  const cur = st?.frames[st.frames.length - 1]

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (!st || busy || (e.target as HTMLElement)?.closest('.monaco-editor, input, textarea, select')) return
      if (e.key === 'F10') { e.preventDefault(); void debugStep('forward', 'over') }
      if (e.key === 'F11') { e.preventDefault(); void debugStep('forward', e.shiftKey ? 'instruction' : 'line') }
      if (e.key === 'F5') { e.preventDefault(); void debugContinue() }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [st, busy, debugStep, debugContinue])

  const highlights = useMemo(() => (st && st.instruction.line ? [{ line: st.instruction.line, className: 'monaco-cur-line' }] : []), [st])
  const finishedMsg = st?.finished ? (st.error ? `Stopped: runtime error — ${st.error.message}` : 'Program finished') : null

  return (
    <div className="h-full flex flex-col min-h-[640px]">
      <PageHeader title="Debugger" subtitle="Controls the real NOVA VM. The VM runs your bytecode once while recording its complete state before every instruction; stepping (also backwards) moves through those authentic snapshots. Click the gutter to toggle breakpoints." />
      <div className="panel p-2 mb-3 flex items-center gap-2 flex-wrap shrink-0">
        <button className="btn btn-primary" onClick={() => void debugStart()} disabled={busy}>{busy && !st ? <Loader2 size={14} className="animate-spin" /> : <Play size={14} />}{st ? 'Restart' : 'Start debugging'}</button>
        <div className="w-px h-6 bg-ink-700 mx-1" />
        <button className="btn" disabled={!st || busy || st.cursor === 0} onClick={() => void debugStep('back')} title="Step back one instruction"><StepBack size={14} /> Back</button>
        <button className="btn" disabled={!st || busy || st.finished} onClick={() => void debugStep('forward')} title="Step one bytecode instruction (Shift+F11)"><StepForward size={14} /> Instr</button>
        <button className="btn" disabled={!st || busy || st.finished} onClick={() => void debugStep('forward', 'line')} title="Step into next source line (F11)"><ArrowDownToLine size={14} /> Line</button>
        <button className="btn" disabled={!st || busy || st.finished} onClick={() => void debugStep('forward', 'over')} title="Step over calls (F10)"><SkipForward size={14} /> Over</button>
        <button className="btn" disabled={!st || busy || st.finished} onClick={() => void debugContinue()} title="Continue to next breakpoint (F5)"><ChevronsRight size={14} /> Continue</button>
        <button className="btn" disabled={!st || busy} onClick={() => void debugReset()}><RotateCcw size={14} /> Reset</button>
        <div className="w-px h-6 bg-ink-700 mx-1" />
        <select className="bg-ink-800 border border-ink-600 rounded h-8 px-2" value={debug.target} onChange={(e) => debugSetTarget(e.target.value as 'optimized' | 'baseline')}>
          <option value="optimized">debug optimized bytecode</option><option value="baseline">debug unoptimized bytecode</option>
        </select>
        <label className="flex items-center gap-1.5 text-fg-dim ml-1"><input type="checkbox" className="accent-[#7b93ff]" checked={showTemps} onChange={(e) => setShowTemps(e.target.checked)} /> show temporaries</label>
        {st && <span className="ml-auto flex gap-2 items-center"><Chip>step {st.step} / {st.trace_length - 1}</Chip>{st.reason === 'breakpoint' && <Chip tone="bad">breakpoint hit</Chip>}{finishedMsg && <Chip tone={st.error ? 'bad' : 'ok'}>{finishedMsg}</Chip>}{st.trace_truncated && <Chip tone="warn">trace truncated</Chip>}</span>}
      </div>
      {debug.error && <div className="mb-3 text-bad">{debug.error}</div>}
      {stale && <div className="mb-3 text-warn">The source has changed since this session started — press Restart to debug the new program.</div>}
      {!st ? (
        <Panel className="flex-1"><Empty title="No active debug session" hint="Start debugging to run the program on the VM with state recording. Set breakpoints by clicking the editor gutter before or after starting." icon={<ArrowRightToLine size={26} />} />
          {!result && <div className="text-center"><button className="btn" onClick={() => void compile()}>Compile first</button></div>}</Panel>
      ) : (
        <div className="flex-1 min-h-0 grid grid-cols-[minmax(0,1.1fr)_minmax(0,1fr)_minmax(0,0.9fr)] gap-3">
          <Panel title="Source" bodyClass="overflow-hidden">
            <Suspense fallback={null}><CodeEditor value={debug.source ?? source} readOnly highlights={highlights} breakpoints={debug.breakpoints} onToggleBreakpoint={(l) => void debugToggleBreakpoint(l)} revealLine={st.instruction.line} glyphMargin /></Suspense>
          </Panel>
          <div className="flex flex-col gap-3 min-h-0">
            <Panel title={`Bytecode · ip ${st.ip}`} className="flex-[3]">{debug.bytecode && <BytecodeListing bytecode={debug.bytecode} currentIp={st.ip} followCurrent showDesc={false} />}</Panel>
            <Panel title="Current instruction" bodyClass="p-3 space-y-1">
              <div className="code text-nova-strong text-[14px]">{st.instruction.text} <span className="text-fg-mute">{st.instruction.comment}</span></div>
              <div className="text-fg-dim">{st.instruction.description}</div>
              <div className="text-fg-mute">in <b>{st.instruction.function}</b>{st.instruction.line ? `, line ${st.instruction.line}` : ''} · last effect: <span className="code">{st.event}</span></div>
            </Panel>
            <Panel title={`Instruction history (${st.history.length})`} className="flex-[2]">
              <table className="w-full code"><tbody>{[...st.history].reverse().map((h) => <tr key={h.step} className="hover:bg-ink-850"><td className="td text-fg-mute w-10">{h.step}</td><td className="td text-fg-dim w-12">{String(h.addr).padStart(4, '0')}</td><td className="td">{h.text}</td><td className="td text-fg-mute">{h.event}</td></tr>)}</tbody></table>
            </Panel>
          </div>
          <div className="flex flex-col gap-3 min-h-0">
            <Panel title={`Variable watch · ${cur?.function ?? '—'}`} className="flex-[3]">{cur ? <Watch frame={cur} showTemps={showTemps} /> : <div className="p-3 text-fg-mute">No active frame (program not started or finished).</div>}</Panel>
            <Panel title="Call stack" className="flex-[2]">
              {st.frames.length === 0 ? <div className="p-3 text-fg-mute">empty</div> : [...st.frames].reverse().map((f) => (
                <div key={f.depth} className={`px-3 py-1.5 border-b border-ink-800 code ${f.depth === st.frames.length - 1 ? 'bg-nova/10' : ''}`}>
                  <span className="text-fg-mute">#{f.depth}</span> {f.function}({f.locals.filter((l) => l.param).map((l) => `${l.name}=${l.value}`).join(', ')}) {f.callLine > 0 && <span className="text-fg-mute">called from line {f.callLine}</span>}
                </div>))}
            </Panel>
            <Panel title="Operand stack" className="flex-[2]" bodyClass="p-2">
              {cur && cur.stack.length ? <div className="flex flex-col-reverse gap-1">{cur.stack.map((v, i) => <div key={i} className={`code px-2 py-1 rounded border ${i === cur.stack.length - 1 ? 'border-nova bg-nova/10' : 'border-ink-600 bg-ink-850'}`}>{v}{i === cur.stack.length - 1 && <span className="text-fg-mute"> ← top</span>}</div>)}</div> : <span className="text-fg-mute">empty</span>}
            </Panel>
            <Panel title="Program output" className="flex-[2]" bodyClass="p-2 code">{st.output.map((l, i) => <div key={i}>{l}</div>)}{st.output.length === 0 && <span className="text-fg-mute font-sans">(nothing printed yet)</span>}{st.finished && st.error && <div className="text-bad">⚠ {st.error.message}</div>}</Panel>
          </div>
        </div>
      )}
    </div>
  )
}
