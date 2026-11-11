import type { ReactNode } from 'react'
import { AlertTriangle, CheckCircle2, CircleSlash, Info, XCircle } from 'lucide-react'
import type { Diagnostic } from '../types/api'

export function Panel({ title, icon, right, children, className = '', bodyClass = '' }: { title?: ReactNode; icon?: ReactNode; right?: ReactNode; children: ReactNode; className?: string; bodyClass?: string }) {
  return (
    <section className={`panel flex flex-col min-h-0 ${className}`}>
      {title !== undefined && (
        <header className="panel-head">
          {icon}
          <span>{title}</span>
          <span className="ml-auto flex items-center gap-2 normal-case tracking-normal">{right}</span>
        </header>
      )}
      <div className={`flex-1 min-h-0 overflow-auto ${bodyClass}`}>{children}</div>
    </section>
  )
}

export function Chip({ children, tone = 'neutral', title }: { children: ReactNode; tone?: 'neutral' | 'ok' | 'bad' | 'warn' | 'nova'; title?: string }) {
  const tones = {
    neutral: 'border-ink-600 text-fg-dim bg-ink-800',
    ok: 'border-ok/40 text-ok bg-ok/10',
    bad: 'border-bad/40 text-bad bg-bad/10',
    warn: 'border-warn/40 text-warn bg-warn/10',
    nova: 'border-nova/40 text-nova bg-nova/10',
  }
  return <span title={title} className={`chip ${tones[tone]}`}>{children}</span>
}

export function StatusIcon({ status, size = 14 }: { status: string; size?: number }) {
  if (status === 'ok') return <CheckCircle2 size={size} className="text-ok" />
  if (status === 'error') return <XCircle size={size} className="text-bad" />
  return <CircleSlash size={size} className="text-fg-mute" />
}

export function Empty({ title, hint, icon }: { title: string; hint?: string; icon?: ReactNode }) {
  return (
    <div className="h-full min-h-[120px] flex flex-col items-center justify-center text-center gap-1.5 p-6 text-fg-mute">
      {icon}
      <div className="text-fg-dim font-medium">{title}</div>
      {hint && <div className="max-w-md">{hint}</div>}
    </div>
  )
}

export function Stat({ label, value, sub, tone }: { label: string; value: ReactNode; sub?: ReactNode; tone?: 'ok' | 'bad' }) {
  return (
    <div className="panel px-3 py-2.5 min-w-[110px]">
      <div className="text-2xs uppercase tracking-wider text-fg-mute">{label}</div>
      <div className={`text-lg font-semibold tabular-nums leading-tight mt-0.5 ${tone === 'ok' ? 'text-ok' : tone === 'bad' ? 'text-bad' : ''}`}>{value}</div>
      {sub && <div className="text-2xs text-fg-mute mt-0.5">{sub}</div>}
    </div>
  )
}

export function PageHeader({ title, subtitle, right }: { title: string; subtitle?: ReactNode; right?: ReactNode }) {
  return (
    <div className="flex items-start gap-4 mb-3 shrink-0">
      <div className="min-w-0">
        <h1 className="text-[17px] font-semibold tracking-tight">{title}</h1>
        {subtitle && <p className="text-fg-dim mt-0.5 max-w-3xl">{subtitle}</p>}
      </div>
      <div className="ml-auto flex items-center gap-2 shrink-0">{right}</div>
    </div>
  )
}

export function DiagnosticList({ items, onSelect, compact }: { items: Diagnostic[]; onSelect?: (d: Diagnostic) => void; compact?: boolean }) {
  if (items.length === 0) return <div className="p-3 text-fg-mute">No diagnostics.</div>
  return (
    <ul className="divide-y divide-ink-800">
      {items.map((d, i) => {
        const Icon = d.severity === 'error' ? XCircle : d.severity === 'warning' ? AlertTriangle : Info
        const color = d.severity === 'error' ? 'text-bad' : d.severity === 'warning' ? 'text-warn' : 'text-nova'
        return (
          <li key={i} className="px-3 py-2 hover:bg-ink-850 cursor-pointer" onClick={() => onSelect?.(d)}>
            <div className="flex items-start gap-2">
              <Icon size={15} className={`${color} mt-0.5 shrink-0`} />
              <div className="min-w-0">
                <div className="flex items-center gap-2 flex-wrap">
                  <span className="font-medium">{d.message}</span>
                  <span className="code text-fg-mute">{d.loc.line}:{d.loc.col}</span>
                </div>
                <div className="flex gap-1.5 mt-0.5">
                  <Chip tone={d.severity === 'error' ? 'bad' : 'warn'}>{d.category}</Chip>
                  <Chip>{d.phase}</Chip>
                  <Chip>{d.code}</Chip>
                </div>
                {!compact && <p className="text-fg-dim mt-1">{d.explanation}</p>}
                {d.suggestion && <p className="mt-1 text-nova-strong">💡 {d.suggestion}</p>}
              </div>
            </div>
          </li>
        )
      })}
    </ul>
  )
}

/** Read-only source with line numbers and range highlighting (cheap alternative to a Monaco instance). */
export function SourceView({ source, highlights = [], className = '', onLineClick, activeLine }: {
  source: string
  highlights?: { line: number; col: number; endLine: number; endCol: number }[]
  className?: string
  onLineClick?: (line: number) => void
  activeLine?: number
}) {
  const lines = source.split('\n')
  return (
    <pre className={`code leading-[1.55] py-2 ${className}`}>
      {lines.map((text, i) => {
        const ln = i + 1
        const hs = highlights.filter((h) => h.line <= ln && h.endLine >= ln)
        let content: ReactNode = text || ' '
        if (hs.length) {
          const marks = new Array(text.length).fill(false)
          for (const h of hs) {
            const from = h.line === ln ? h.col - 1 : 0
            const to = h.endLine === ln ? h.endCol - 1 : text.length
            for (let k = Math.max(0, from); k < Math.min(text.length, Math.max(to, from + 1)); k++) marks[k] = true
          }
          const parts: ReactNode[] = []
          let k = 0
          while (k < text.length) {
            let j = k
            while (j < text.length && marks[j] === marks[k]) j++
            const seg = text.slice(k, j)
            parts.push(marks[k] ? <mark key={k} className="bg-warn/30 text-fg rounded-sm px-[1px] border-b border-warn">{seg}</mark> : <span key={k}>{seg}</span>)
            k = j
          }
          content = parts
        }
        return (
          <div key={i} onClick={() => onLineClick?.(ln)} className={`flex ${onLineClick ? 'cursor-pointer' : ''} ${activeLine === ln ? 'bg-nova/15' : 'hover:bg-ink-850'}`}>
            <span className="w-10 pr-3 text-right text-ink-500 select-none shrink-0">{ln}</span>
            <span className="whitespace-pre">{content}</span>
          </div>
        )
      })}
    </pre>
  )
}
