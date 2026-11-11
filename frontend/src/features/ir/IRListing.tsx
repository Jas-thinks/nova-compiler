import { useState } from 'react'
import type { IRFunction, IRInstr, IRProgram } from '../../types/api'

function colorize(i: IRInstr) {
  const t = i.text
  if (i.kind === 'label') return <span className="text-warn">{t}</span>
  if (i.kind.startsWith('jump')) return <span className="text-nova-strong">{t}</span>
  if (i.kind === 'return' || i.kind === 'print' || i.kind === 'call') return <span className="text-[#d97ad9]">{t}</span>
  return <span>{t}</span>
}

export function IRFunctionTable({ fn, showExplain = true, onSelectLine, activeLine }: { fn: IRFunction; showExplain?: boolean; onSelectLine?: (line: number) => void; activeLine?: number }) {
  return (
    <table className="w-full code">
      <thead><tr><th className="th w-10">#</th><th className="th">Instruction</th><th className="th w-14">Src</th>{showExplain && <th className="th">What it does</th>}</tr></thead>
      <tbody>
        {fn.instructions.map((i) => (
          <tr key={i.index} onClick={() => i.line && onSelectLine?.(i.line)} className={`hover:bg-ink-850 ${i.line && i.line === activeLine ? 'bg-nova/15' : ''} ${onSelectLine ? 'cursor-pointer' : ''}`}>
            <td className="td text-fg-mute">{i.index}</td>
            <td className={`td ${i.kind === 'label' ? '' : 'pl-6'}`}>{colorize(i)}</td>
            <td className="td text-fg-mute">{i.line ?? ''}</td>
            {showExplain && <td className="td font-sans text-fg-dim text-[12px]">{i.explanation}</td>}
          </tr>
        ))}
      </tbody>
    </table>
  )
}

export default function IRListing({ program, ...rest }: { program: IRProgram; showExplain?: boolean; onSelectLine?: (l: number) => void; activeLine?: number }) {
  const [active, setActive] = useState(0)
  const fn = program.functions[Math.min(active, program.functions.length - 1)]
  return (
    <div>
      <div className="flex gap-1 px-2 pt-2 flex-wrap">
        {program.functions.map((f, i) => (
          <button key={f.name} onClick={() => setActive(i)} className={`px-2 h-6 rounded text-2xs code border ${i === active ? 'border-nova text-nova bg-nova/10' : 'border-ink-600 text-fg-dim hover:bg-ink-800'}`}>
            {f.name} <span className="text-fg-mute">({f.count})</span>
          </button>
        ))}
      </div>
      {fn && <IRFunctionTable fn={fn} {...rest} />}
    </div>
  )
}
