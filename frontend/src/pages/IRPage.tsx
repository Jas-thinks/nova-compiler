import { useState } from 'react'
import Gate from '../components/Gate'
import { Chip, PageHeader, Panel, SourceView } from '../components/ui'
import IRListing from '../features/ir/IRListing'
import { useNova } from '../state/NovaContext'

export default function IRPage() {
  const { resultSource } = useNova()
  const [which, setWhich] = useState<'original' | 'optimized'>('original')
  const [line, setLine] = useState<number | undefined>()
  return (
    <div className="h-full flex flex-col min-h-[520px]">
      <PageHeader title="Intermediate Representation" subtitle="Three-address code: every instruction has at most one operator and one destination; compiler temporaries (t1, t2, …) hold intermediate results; control flow uses labels and jumps. Short-circuit && / || are lowered to jumps." />
      <Gate need={(r) => r.ir}>
        {(r) => (
          <div className="flex-1 min-h-0 grid grid-cols-[minmax(0,0.8fr)_minmax(0,1.6fr)] gap-3">
            <Panel title="Source (click an IR row to map it back)">{resultSource && <SourceView source={resultSource} activeLine={line} onLineClick={setLine} />}</Panel>
            <Panel title="IR" right={<span className="flex gap-1 items-center">
              {(['original', 'optimized'] as const).map((w) => <button key={w} onClick={() => setWhich(w)} className={`px-2 h-6 rounded ${which === w ? 'bg-ink-700 text-fg' : 'text-fg-dim hover:text-fg'}`}>{w}</button>)}
              <Chip tone="nova">{r.ir![which].instructionCount} instrs</Chip></span>}>
              <IRListing program={r.ir![which]} onSelectLine={setLine} activeLine={line} />
            </Panel>
          </div>
        )}
      </Gate>
    </div>
  )
}
