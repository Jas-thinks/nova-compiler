import { useEffect, useRef } from 'react'
import type { Bytecode } from '../../types/api'

export default function BytecodeListing({ bytecode, currentIp, onSelect, selectedAddr, showDesc = true, followCurrent }: {
  bytecode: Bytecode; currentIp?: number; onSelect?: (addr: number) => void; selectedAddr?: number; showDesc?: boolean; followCurrent?: boolean
}) {
  const cur = useRef<HTMLTableRowElement | null>(null)
  useEffect(() => { if (followCurrent) cur.current?.scrollIntoView({ block: 'nearest' }) }, [currentIp, followCurrent])
  const entryOf = new Map(bytecode.functions.map((f) => [f.entry, f.name]))
  return (
    <table className="w-full code">
      <thead><tr><th className="th w-12">Addr</th><th className="th">Instruction</th><th className="th">Operand</th><th className="th w-12">Line</th>{showDesc && <th className="th">Meaning</th>}</tr></thead>
      <tbody>
        {bytecode.instructions.map((i) => {
          const isCur = currentIp === i.addr
          return [
            entryOf.has(i.addr) && (
              <tr key={'f' + i.addr}><td colSpan={5} className="px-3 pt-2 pb-0.5 text-2xs uppercase tracking-wider text-nova border-t border-ink-700">fn {entryOf.get(i.addr)}</td></tr>
            ),
            <tr key={i.addr} ref={isCur ? cur : undefined} onClick={() => onSelect?.(i.addr)}
              className={`${onSelect ? 'cursor-pointer' : ''} hover:bg-ink-850 ${isCur ? 'bg-nova/20 outline outline-1 outline-nova/50' : selectedAddr === i.addr ? 'bg-warn/15' : ''}`}>
              <td className="td text-fg-mute">{isCur ? '▶ ' : ''}{String(i.addr).padStart(4, '0')}</td>
              <td className="td text-nova-strong">{i.text}</td>
              <td className="td text-fg-dim">{i.comment}</td>
              <td className="td text-fg-mute">{i.line ?? ''}</td>
              {showDesc && <td className="td font-sans text-fg-dim text-[12px]">{i.description}</td>}
            </tr>,
          ]
        })}
      </tbody>
    </table>
  )
}
