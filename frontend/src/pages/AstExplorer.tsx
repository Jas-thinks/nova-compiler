import { useState } from 'react'
import Gate from '../components/Gate'
import { Chip, PageHeader, Panel, SourceView } from '../components/ui'
import AstTree, { AstOutline } from '../features/ast/AstTree'
import { countNodes } from '../features/ast/layout'
import { useNova } from '../state/NovaContext'
import { sliceSource } from '../lib/format'
import type { AstNode } from '../types/api'

export default function AstExplorer() {
  const { resultSource } = useNova()
  const [sel, setSel] = useState<AstNode | null>(null)
  const [view, setView] = useState<'graph' | 'outline'>('graph')
  return (
    <div className="h-full flex flex-col min-h-[560px]">
      <PageHeader title="AST Explorer" subtitle="The parse tree is converted into a typed abstract syntax tree: syntactic noise (parentheses, separators, unit productions) disappears and every node keeps its source location. Click a node to inspect it; use the chevron to collapse a subtree." />
      <Gate need={(r) => r.ast}>
        {(r) => (
          <div className="flex-1 min-h-0 grid grid-cols-[1fr_320px] gap-3">
            <Panel title={`AST · ${countNodes(r.ast!)} nodes`} right={<span className="flex gap-1">{(['graph', 'outline'] as const).map((v) => <button key={v} onClick={() => setView(v)} className={`px-2 h-6 rounded ${view === v ? 'bg-ink-700 text-fg' : 'text-fg-dim hover:text-fg'}`}>{v}</button>)}</span>} bodyClass="overflow-hidden">
              {view === 'graph' ? <AstTree root={r.ast!} selectedId={sel?.id} onSelect={setSel} /> : <div className="h-full overflow-auto py-1"><AstOutline node={r.ast!} onSelect={setSel} selectedId={sel?.id} /></div>}
            </Panel>
            <div className="flex flex-col gap-3 min-h-0">
              <Panel title="Selected node" bodyClass="p-3 space-y-2">
                {sel ? (<>
                  <div className="font-medium text-[14px]">{sel.label}</div>
                  <div className="flex gap-1.5 flex-wrap"><Chip tone="nova">{sel.kind}</Chip>{sel.type && <Chip tone="ok">type {sel.type}</Chip>}<Chip>{sel.children.length} children</Chip></div>
                  <div className="code text-fg-dim">{sel.loc.line}:{sel.loc.col} → {sel.loc.endLine}:{sel.loc.endCol}</div>
                  <pre className="code bg-ink-850 border border-ink-700 rounded p-2 whitespace-pre-wrap max-h-40 overflow-auto">{resultSource ? sliceSource(resultSource, sel.loc) : ''}</pre>
                </>) : <span className="text-fg-mute">Click a node in the tree.</span>}
              </Panel>
              <Panel title="Source" className="flex-1">{resultSource && <SourceView source={resultSource} highlights={sel ? [sel.loc] : []} />}</Panel>
            </div>
          </div>
        )}
      </Gate>
    </div>
  )
}
