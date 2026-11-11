import { Bug, Layers3 } from 'lucide-react'
import { Chip, Empty, PageHeader, Panel } from '../components/ui'
import { useNova } from '../state/NovaContext'

export default function RuntimeMemory() {
  const { debug, debugStart, debugStep, debugContinue, debugSeek } = useNova()
  const st = debug.state
  const go = (r: string) => { window.location.hash = '/' + r }
  return (
    <div>
      <PageHeader title="Runtime Memory" subtitle="What the NOVA VM really holds at the current instruction: the call stack of frames, each frame's local slots, and its operand stack. The NOVA runtime has no heap, so none is shown; there are no real memory addresses to display — slots are frame-relative indices." />
      {!st ? (
        <Panel><Empty icon={<Layers3 size={28} />} title="No running VM session" hint="Start a debug session; this view then follows the debugger." />
          <div className="text-center pb-4 flex justify-center gap-2"><button className="btn btn-primary" onClick={() => void debugStart()}><Bug size={14} /> Start session</button><button className="btn" onClick={() => go('debugger')}>Open debugger</button></div></Panel>
      ) : (
        <div className="grid grid-cols-[1fr_300px] gap-3">
          <div className="flex flex-col gap-3 min-w-0">
            <div className="panel p-3 flex items-center gap-3 flex-wrap">
              <button className="btn" disabled={st.cursor === 0} onClick={() => void debugStep('back')}>◀ Back</button>
              <button className="btn" disabled={st.finished} onClick={() => void debugStep('forward')}>Step ▶</button>
              <button className="btn" disabled={st.finished} onClick={() => void debugContinue()}>Continue ▶▶</button>
              <input type="range" className="flex-1 min-w-[160px] accent-[#7b93ff]" min={0} max={st.trace_length - 1} value={st.cursor} onChange={(e) => void debugSeek(+e.target.value)} />
              <span className="code text-fg-dim">step {st.step}/{st.trace_length - 1}</span>
            </div>
            <div className="panel p-3">
              <div className="text-2xs uppercase tracking-wider text-fg-mute mb-1">Instruction pointer</div>
              <div className="code text-[14px]"><span className="text-warn">ip = {st.ip}</span> → <span className="text-nova-strong">{st.instruction.text}</span> <span className="text-fg-mute">{st.instruction.comment}</span></div>
              <div className="text-fg-dim mt-1">Previous instruction: <span className="code">{st.event}</span></div>
            </div>
            <Panel title={`Call stack · ${st.frames.length} active frame${st.frames.length === 1 ? '' : 's'} (top first)`} bodyClass="p-3 space-y-3">
              {st.frames.length === 0 && <span className="text-fg-mute">No frames: {st.finished ? 'all frames have been destroyed — the program finished.' : 'the VM has not entered main yet. Step once to create the first frame.'}</span>}
              {[...st.frames].reverse().map((f) => {
                const top = f.depth === st.frames.length - 1
                return (
                  <div key={f.depth} className={`rounded-lg border ${top ? 'border-nova bg-nova/5' : 'border-ink-600 bg-ink-850'}`}>
                    <div className="px-3 py-1.5 border-b border-ink-700 flex items-center gap-2"><b className="code">{f.function}</b><Chip>frame #{f.depth}</Chip>{top && <Chip tone="nova">executing</Chip>}{f.callLine > 0 && <span className="text-fg-mute text-[12px]">called at line {f.callLine}</span>}</div>
                    <div className="p-3 grid grid-cols-[1fr_150px] gap-4">
                      <div>
                        <div className="text-2xs uppercase tracking-wider text-fg-mute mb-1">Local slots</div>
                        <div className="grid grid-cols-[repeat(auto-fill,minmax(112px,1fr))] gap-1.5">
                          {f.locals.map((l) => (
                            <div key={l.slot} className={`rounded border px-2 py-1 ${l.temp ? 'border-ink-700 opacity-70' : 'border-ink-600'} bg-ink-900`}>
                              <div className="text-2xs text-fg-mute code flex justify-between"><span>[{l.slot}] {l.name}</span><span>{l.type}</span></div>
                              <div className="code text-warn text-[13px]">{l.value}</div>
                            </div>))}
                        </div>
                      </div>
                      <div>
                        <div className="text-2xs uppercase tracking-wider text-fg-mute mb-1">Operand stack</div>
                        <div className="flex flex-col-reverse gap-1 min-h-[28px]">{f.stack.length ? f.stack.map((v, i) => <div key={i} className={`code px-2 py-0.5 rounded border text-center ${i === f.stack.length - 1 ? 'border-nova bg-nova/10' : 'border-ink-600'}`}>{v}</div>) : <span className="text-fg-mute">empty</span>}</div>
                      </div>
                    </div>
                  </div>)
              })}
            </Panel>
          </div>
          <div className="flex flex-col gap-3">
            <Panel title="Frame lifecycle events" bodyClass="max-h-72">
              {st.history.filter((h) => h.event.startsWith('call') || h.event.startsWith('return')).slice(-30).reverse().map((h) => (
                <div key={h.step} className="px-3 py-1 border-b border-ink-800 code"><span className="text-fg-mute">#{h.step}</span> <span className={h.event.startsWith('call') ? 'text-ok' : 'text-warn'}>{h.event}</span></div>))}
              {!st.history.some((h) => h.event.startsWith('call') || h.event.startsWith('return')) && <div className="p-3 text-fg-mute">No call/return yet in the recent history.</div>}
            </Panel>
            <Panel title="Program output" bodyClass="p-2 code">{st.output.map((l, i) => <div key={i}>{l}</div>)}{st.output.length === 0 && <span className="text-fg-mute font-sans">(nothing yet)</span>}</Panel>
          </div>
        </div>
      )}
    </div>
  )
}
