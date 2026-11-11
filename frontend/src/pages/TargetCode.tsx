import { useState } from 'react'
import Gate from '../components/Gate'
import { Chip, PageHeader, Panel, SourceView } from '../components/ui'
import BytecodeListing from '../features/bytecode/BytecodeListing'
import { useNova } from '../state/NovaContext'

export default function TargetCode() {
  const { resultSource } = useNova()
  const [which, setWhich] = useState<'optimized' | 'baseline'>('optimized')
  const [addr, setAddr] = useState<number | undefined>()
  const [line, setLine] = useState<number | undefined>()
  return (
    <div className="h-full flex flex-col min-h-[520px]">
      <PageHeader title="Target Code — NOVA VM bytecode" subtitle="A stack-machine instruction set interpreted by the NOVA virtual machine. This is VM bytecode, not native machine code. Each function owns numbered local slots; arguments become the first slots of the new frame." />
      <Gate need={(r) => r.bytecode}>
        {(r) => {
          const bc = r.bytecode![which]
          const sel = addr !== undefined ? bc.instructions[addr] : undefined
          const firstMapped = line ? bc.instructions.find((i) => i.line === line)?.addr : undefined
          const counts = bc.instructions.reduce<Record<string, number>>((m, i) => ({ ...m, [i.op]: (m[i.op] ?? 0) + 1 }), {})
          return (
            <div className="flex-1 min-h-0 grid grid-cols-[minmax(0,0.8fr)_minmax(0,1.7fr)] gap-3">
              <div className="flex flex-col gap-3 min-h-0">
                <Panel title="Source ↔ bytecode mapping" className="flex-1">
                  {resultSource && <SourceView source={resultSource} activeLine={sel?.line ?? line} onLineClick={(l) => { setLine(l); setAddr(undefined) }} />}
                </Panel>
                <Panel title="Instruction mix" bodyClass="p-2 flex gap-1 flex-wrap max-h-40">{Object.entries(counts).sort((a, b) => b[1] - a[1]).map(([k, v]) => <Chip key={k}>{k} × {v}</Chip>)}</Panel>
              </div>
              <Panel title={`Bytecode · ${bc.count} instructions`} right={<span className="flex gap-1 items-center">
                {(['optimized', 'baseline'] as const).map((w) => <button key={w} onClick={() => setWhich(w)} className={`px-2 h-6 rounded ${which === w ? 'bg-ink-700 text-fg' : 'text-fg-dim hover:text-fg'}`}>{w}</button>)}</span>}>
                <BytecodeListing bytecode={bc} onSelect={(a) => { setAddr(a); setLine(undefined) }} selectedAddr={addr ?? firstMapped} />
              </Panel>
            </div>
          )
        }}
      </Gate>
    </div>
  )
}
