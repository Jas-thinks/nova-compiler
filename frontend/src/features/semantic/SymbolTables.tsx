import type { ScopeInfo } from '../../types/api'
import { Chip } from '../../components/ui'

export function ScopeTree({ scopes, id = 0, depth = 0, selected, onSelect }: { scopes: ScopeInfo[]; id?: number; depth?: number; selected?: number; onSelect: (s: ScopeInfo) => void }) {
  const s = scopes[id]
  return (
    <div>
      <div onClick={() => onSelect(s)} style={{ paddingLeft: depth * 16 + 8 }} className={`py-1 pr-2 cursor-pointer hover:bg-ink-800 flex items-center gap-2 ${selected === s.id ? 'bg-nova/15' : ''}`}>
        <span className="text-fg-mute">{depth ? '└' : '◉'}</span>
        <span className="font-medium">{s.kind === 'global' ? 'global' : s.kind === 'function' ? `fn ${s.owner}` : 'block'}</span>
        <span className="code text-fg-mute">#{s.id}{s.kind !== 'global' ? ` · line ${s.loc.line}` : ''}</span>
        <Chip>{s.symbols.length} sym</Chip>
      </div>
      {s.children.map((c) => <ScopeTree key={c} scopes={scopes} id={c} depth={depth + 1} selected={selected} onSelect={onSelect} />)}
    </div>
  )
}

export function SymbolTable({ scope }: { scope: ScopeInfo }) {
  if (scope.symbols.length === 0) return <div className="p-3 text-fg-mute">No symbols are declared directly in this scope.</div>
  return (
    <table className="w-full code">
      <thead><tr><th className="th">Name</th><th className="th">IR name</th><th className="th">Type</th><th className="th">Kind</th><th className="th">Declared</th><th className="th">Reads</th><th className="th">Writes</th></tr></thead>
      <tbody>
        {scope.symbols.map((s) => (
          <tr key={s.uniqueName} className="hover:bg-ink-850">
            <td className="td font-medium">{s.name}</td>
            <td className="td text-fg-dim">{s.uniqueName}</td>
            <td className="td"><Chip tone="nova">{s.type}</Chip></td>
            <td className="td text-fg-dim">{s.kind}</td>
            <td className="td text-fg-dim">{s.declLoc.line}:{s.declLoc.col}</td>
            <td className="td" title={s.reads.map((r) => `${r.line}:${r.col}`).join(', ')}>
              {s.readCount === 0 && s.kind === 'variable' ? <Chip tone="warn">never read</Chip> : s.readCount}
            </td>
            <td className="td">{s.writeCount}</td>
          </tr>
        ))}
      </tbody>
    </table>
  )
}
