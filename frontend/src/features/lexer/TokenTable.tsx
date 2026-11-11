import type { Token } from '../../types/api'
import { Chip } from '../../components/ui'

const tone = (c: string) => (c === 'KEYWORD' ? 'nova' : c === 'IDENTIFIER' ? 'neutral' : c.endsWith('LITERAL') ? 'warn' : c === 'EOF' ? 'neutral' : 'ok') as 'nova' | 'neutral' | 'warn' | 'ok'

export default function TokenTable({ tokens, selected, onSelect, limit }: { tokens: Token[]; selected?: number; onSelect?: (i: number) => void; limit?: number }) {
  const shown = limit ? tokens.slice(0, limit) : tokens
  return (
    <table className="w-full code">
      <thead><tr><th className="th w-10">#</th><th className="th">Type</th><th className="th">Lexeme</th><th className="th">Terminal</th><th className="th w-20">Line:Col</th></tr></thead>
      <tbody>
        {shown.map((t, i) => (
          <tr key={i} onClick={() => onSelect?.(i)} className={`cursor-pointer hover:bg-ink-850 ${selected === i ? 'bg-nova/15' : ''}`}>
            <td className="td text-fg-mute">{i}</td>
            <td className="td"><Chip tone={tone(t.category)}>{t.category}</Chip></td>
            <td className="td">{t.category === 'EOF' ? <span className="text-fg-mute">⟨eof⟩</span> : t.lexeme}</td>
            <td className="td text-fg-dim">{t.terminal}</td>
            <td className="td text-fg-dim">{t.loc.line}:{t.loc.col}</td>
          </tr>
        ))}
        {limit && tokens.length > limit && <tr><td colSpan={5} className="td text-fg-mute">… {tokens.length - limit} more</td></tr>}
      </tbody>
    </table>
  )
}
