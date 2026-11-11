import type {
  CompileResponse, DebugStartResponse, DebugState, ExecuteResponse, Example, GrammarInfo, HealthInfo,
  OptimizeResponse, SqlResponse, TestRunResponse, Capabilities, ExplorerCResult, ExplorerLanguage, LastTests,
} from '../types/api'

export class ApiError extends Error {
  constructor(message: string, public status: number, public detail?: unknown) {
    super(message)
    this.name = 'ApiError'
  }
}

type Json = Record<string, unknown>

function describeDetail(detail: unknown): string {
  if (typeof detail === 'string') return detail
  if (Array.isArray(detail)) return detail.map((d) => (typeof d === 'object' && d ? `${(d as Json).field ?? ''}: ${(d as Json).message ?? ''}` : String(d))).join('; ')
  return JSON.stringify(detail)
}

export async function request<T>(path: string, init?: RequestInit): Promise<T> {
  let res: Response
  try {
    res = await fetch(path, init)
  } catch {
    throw new ApiError('Cannot reach the NOVA backend. Is it running? (./scripts/dev.sh)', 0)
  }
  const text = await res.text()
  let body: unknown = null
  if (text) {
    try { body = JSON.parse(text) } catch { /* non-JSON error body */ }
  }
  if (!res.ok) {
    const detail = body && typeof body === 'object' ? (body as Json).detail : text
    throw new ApiError(`${res.status}: ${describeDetail(detail) || res.statusText}`, res.status, detail)
  }
  return body as T
}

const post = <T,>(path: string, body: Json) =>
  request<T>(path, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(body) })

export const api = {
  health: () => request<HealthInfo>('/api/health'),
  examples: () => request<{ examples: Example[] }>('/api/examples').then((r) => r.examples),
  grammar: () => request<GrammarInfo>('/api/grammar'),
  lrState: (n: number) => request<{ id: number; kernel: string[]; closure: string[] }>(`/api/grammar/state/${n}`),
  compile: (source: string, passes: string[] | null) => post<CompileResponse>('/api/compile', { source, passes, run: false }),
  execute: (source: string, passes: string[] | null) => post<ExecuteResponse>('/api/execute', { source, passes }),
  optimize: (source: string, passes: string[] | null) => post<OptimizeResponse>('/api/optimize', { source, passes }),
  debugStart: (source: string, passes: string[] | null, target: 'optimized' | 'baseline', breakpoints: number[]) =>
    post<DebugStartResponse>('/api/debug/start', { source, passes, target, breakpoints }),
  debugStep: (session_id: string, direction: 'forward' | 'back', mode: 'instruction' | 'line' | 'over') =>
    post<DebugState>('/api/debug/step', { session_id, direction, mode }),
  debugContinue: (session_id: string, breakpoints: number[]) => post<DebugState>('/api/debug/continue', { session_id, breakpoints }),
  debugBreakpoints: (session_id: string, breakpoints: number[]) => post<DebugState>('/api/debug/breakpoints', { session_id, breakpoints }),
  debugSeek: (session_id: string, cursor: number) => post<DebugState>('/api/debug/seek', { session_id, cursor }),
  sql: (script: string) => post<SqlResponse>('/api/sql', { script }),
  isa: () => request<{ instructions: { op: string; description: string }[] }>('/api/isa').then((r) => r.instructions),
  debugReset: (session_id: string) => post<DebugState>('/api/debug/reset', { session_id }),
  runTests: (fuzz_count: number, seed: number, unit_tests: boolean) => post<TestRunResponse>('/api/tests/run', { fuzz_count, seed, unit_tests }),
  capabilities: () => request<Capabilities>('/api/capabilities'),
  lastTests: () => request<LastTests>('/api/tests/last'),
  explorerLanguages: () => request<{ languages: ExplorerLanguage[] }>('/api/explorer/languages').then((r) => r.languages),
  explorerC: (source: string, opt_level: number) => post<ExplorerCResult>('/api/explorer/c', { source, opt_level }),
  explain: (source: string, diagnostic: unknown) => post<{ available: boolean; explanation: string | null; note?: string }>('/api/explain', { source, diagnostic }),
}
