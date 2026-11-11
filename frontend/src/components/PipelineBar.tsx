import { ChevronRight } from 'lucide-react'
import { ms, STAGE_ORDER, type StageId } from '../lib/format'
import type { Stage } from '../types/api'
import { StatusIcon } from './ui'

const SHORT: Record<string, string> = { lexer: 'Lexer', parser: 'Parser', ast: 'AST', semantic: 'Semantic', ir: 'IR', optimizer: 'Optimizer', codegen: 'Codegen' }

export default function PipelineBar({ stages, selected, onSelect, vertical }: { stages?: Stage[]; selected?: StageId; onSelect?: (s: StageId) => void; vertical?: boolean }) {
  return (
    <div className={`flex ${vertical ? 'flex-col gap-1.5' : 'items-stretch gap-1 flex-wrap'}`}>
      {STAGE_ORDER.map((id, idx) => {
        const st = stages?.find((s) => s.id === id)
        const status = st?.status ?? 'pending'
        const active = selected === id
        const clickable = Boolean(onSelect) && st && st.status !== 'skipped'
        return (
          <div key={id} className={`flex items-center ${vertical ? '' : 'gap-1'}`}>
            <button
              disabled={!clickable}
              onClick={() => onSelect?.(id)}
              className={`text-left rounded-md border px-2.5 py-1.5 min-w-[96px] w-full transition-colors ${
                active ? 'border-nova bg-nova/10' : status === 'error' ? 'border-bad/50 bg-bad/5' : 'border-ink-700 bg-ink-850'
              } ${clickable ? 'hover:border-ink-500 cursor-pointer' : 'opacity-60 cursor-default'}`}
            >
              <div className="flex items-center gap-1.5">
                <StatusIcon status={status} size={13} />
                <span className="font-medium text-[12.5px]">{SHORT[id]}</span>
              </div>
              <div className="text-2xs text-fg-mute code mt-0.5">{st ? (st.status === 'skipped' ? 'skipped' : ms(st.durationMs)) : '—'}</div>
            </button>
            {!vertical && idx < STAGE_ORDER.length - 1 && <ChevronRight size={14} className="text-ink-500 shrink-0" />}
          </div>
        )
      })}
    </div>
  )
}
