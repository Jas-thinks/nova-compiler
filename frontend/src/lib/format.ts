import type { Diagnostic, Loc } from '../types/api'

export const PASS_TITLES: Record<string, string> = {
  inlining: 'Function inlining',
  constant_propagation: 'Constant & copy propagation',
  constant_folding: 'Constant folding',
  algebraic: 'Algebraic simplification',
  cse: 'Common subexpression elimination',
  unreachable: 'Unreachable code elimination',
  dce: 'Dead code elimination',
}
export const PASS_IDS = Object.keys(PASS_TITLES)

export const STAGE_ORDER = ['lexer', 'parser', 'ast', 'semantic', 'ir', 'optimizer', 'codegen'] as const
export type StageId = (typeof STAGE_ORDER)[number]

export const locText = (l?: Loc) => (l ? `${l.line}:${l.col}` : '—')

export function ms(v: number): string {
  if (v < 0.01) return '<0.01 ms'
  if (v < 10) return `${v.toFixed(2)} ms`
  return `${v.toFixed(1)} ms`
}

/** Monaco marker severity numbers: Hint=1, Info=2, Warning=4, Error=8. */
export function diagnosticToMarker(d: Diagnostic) {
  const endCol = d.loc.endLine === d.loc.line ? Math.max(d.loc.endCol, d.loc.col + 1) : d.loc.col + 1
  return {
    severity: d.severity === 'error' ? 8 : d.severity === 'warning' ? 4 : 2,
    message: d.message + (d.suggestion ? `\n💡 ${d.suggestion}` : ''),
    startLineNumber: d.loc.line,
    startColumn: d.loc.col,
    endLineNumber: d.loc.endLine,
    endColumn: endCol,
    code: d.code,
    source: 'nova',
  }
}

/** Extract the text covered by a location (single-line locations only; multi-line are cut at end of the first line). */
export function sliceSource(source: string, loc: Loc): string {
  const lines = source.split('\n')
  const line = lines[loc.line - 1] ?? ''
  const end = loc.endLine === loc.line ? loc.endCol - 1 : line.length
  return line.slice(loc.col - 1, end)
}

export function pct(before: number, after: number): string {
  if (before <= 0) return '0%'
  return `${Math.round(((before - after) / before) * 100)}%`
}
