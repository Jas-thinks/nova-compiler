import { useState } from 'react'
import { Panel, PageHeader, Chip, SourceView } from '../components/ui'
import Gate from '../components/Gate'
import { useNova } from '../state/NovaContext'
import TokenTable from '../features/lexer/TokenTable'

export default function Lexical() {
  const { resultSource } = useNova()
  const [sel, setSel] = useState<number | undefined>()
  return (
    <div className="h-full flex flex-col">
      <PageHeader title="Lexical Analysis" subtitle="The scanner reads the source one character at a time and groups characters into tokens (longest match), tracking line and column. Whitespace and comments are discarded." />
      <Gate need={(r) => r.tokens.length > 0}>
        {(r) => {
          const counts = r.tokens.reduce<Record<string, number>>((m, t) => ({ ...m, [t.category]: (m[t.category] ?? 0) + 1 }), {})
          const t = sel !== undefined ? r.tokens[sel] : undefined
          return (
            <div className="flex-1 min-h-0 flex flex-col gap-3">
              <div className="flex gap-2 flex-wrap">{Object.entries(counts).map(([k, v]) => <Chip key={k} tone="nova">{k}: {v}</Chip>)}{r.diagnostics.some((d) => d.phase === 'lexical') && <Chip tone="bad">lexical errors</Chip>}</div>
              {r.diagnostics.filter((d) => d.phase === 'lexical').map((d, i) => <div key={i} className="panel border-bad/40 p-3"><b className="text-bad">{d.category}</b> at {d.loc.line}:{d.loc.col} — {d.message}<div className="text-nova-strong mt-1">💡 {d.suggestion}</div></div>)}
              <div className="flex-1 min-h-0 grid grid-cols-2 gap-3">
                <Panel title="Source (selected token highlighted)"><SourceView source={resultSource ?? ''} highlights={t ? [t.loc] : []} /></Panel>
                <Panel title={`Token stream · ${r.tokens.length} tokens`} right={t && <span className="code text-fg-dim">offset {t.offset}</span>}><TokenTable tokens={r.tokens} selected={sel} onSelect={setSel} /></Panel>
              </div>
            </div>
          )
        }}
      </Gate>
    </div>
  )
}
