import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from 'react'
import { api, ApiError } from '../services/api'
import { PASS_IDS, type StageId } from '../lib/format'
import type { Bytecode, CompileResponse, DebugState, ExecuteResponse, Example, HealthInfo } from '../types/api'

const DEFAULT_SOURCE = `// NOVA — edit me, then press Compile (Ctrl+Enter)
fn fact(n: int) -> int {
    if (n <= 1) {
        return 1;
    }
    return n * fact(n - 1);
}

fn main() {
    let age: int = 22;
    let marks: float = 91.5;
    let passed: bool = true;

    let result: int = 10 + 20 * 2;

    if (result > 30) {
        print(result);
    } else {
        print(0);
    }
    print(fact(5));
}
`

function loadSaved(): string {
  try { return localStorage.getItem('nova.source') ?? DEFAULT_SOURCE } catch { return DEFAULT_SOURCE }
}

export interface DebugInfo {
  state: DebugState | null
  bytecode: Bytecode | null
  breakpoints: number[]
  target: 'optimized' | 'baseline'
  loading: boolean
  error: string | null
  source: string | null // the source the session was started with
}

interface Store {
  source: string
  setSource: (s: string) => void
  loadExample: (e: Example) => void
  examples: Example[]
  health: HealthInfo | null
  backendError: string | null

  passes: Record<string, boolean>
  setPass: (id: string, on: boolean) => void
  passesParam: string[] | null

  result: CompileResponse | null
  resultSource: string | null
  stale: boolean
  compiling: boolean
  compile: () => Promise<CompileResponse | null>
  exec: ExecuteResponse | null
  running: boolean
  run: () => Promise<void>
  error: string | null
  clearError: () => void

  stage: StageId
  setStage: (s: StageId) => void

  debug: DebugInfo
  debugStart: () => Promise<void>
  debugStep: (dir: 'forward' | 'back', mode?: 'instruction' | 'line' | 'over') => Promise<void>
  debugContinue: () => Promise<void>
  debugReset: () => Promise<void>
  debugSeek: (cursor: number) => Promise<void>
  debugToggleBreakpoint: (line: number) => Promise<void>
  debugSetTarget: (t: 'optimized' | 'baseline') => void
}

const Ctx = createContext<Store | null>(null)
export const useNova = () => {
  const s = useContext(Ctx)
  if (!s) throw new Error('useNova outside provider')
  return s
}

const initialDebug: DebugInfo = { state: null, bytecode: null, breakpoints: [], target: 'optimized', loading: false, error: null, source: null }

export function NovaProvider({ children }: { children: ReactNode }) {
  const [source, setSourceRaw] = useState(loadSaved)
  const [examples, setExamples] = useState<Example[]>([])
  const [health, setHealth] = useState<HealthInfo | null>(null)
  const [backendError, setBackendError] = useState<string | null>(null)
  const [passes, setPasses] = useState<Record<string, boolean>>(() => Object.fromEntries(PASS_IDS.map((p) => [p, true])))
  const [result, setResult] = useState<CompileResponse | null>(null)
  const [resultSource, setResultSource] = useState<string | null>(null)
  const [compiling, setCompiling] = useState(false)
  const [exec, setExec] = useState<ExecuteResponse | null>(null)
  const [running, setRunning] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [stage, setStage] = useState<StageId>('lexer')
  const [debug, setDebug] = useState<DebugInfo>(initialDebug)
  const reqId = useRef(0)

  useEffect(() => {
    api.health().then((h) => { setHealth(h); setBackendError(h.compiler_found ? null : 'Backend is running but the C++ compiler binary was not found. Run ./scripts/build.sh') })
      .catch((e: ApiError) => setBackendError(e.message))
    api.examples().then(setExamples).catch(() => undefined)
  }, [])

  const setSource = useCallback((s: string) => {
    setSourceRaw(s)
    try { localStorage.setItem('nova.source', s) } catch { /* private mode */ }
  }, [])

  const passesParam = useMemo(() => {
    const on = PASS_IDS.filter((p) => passes[p])
    return on.length === PASS_IDS.length ? null : on
  }, [passes])

  const fail = (e: unknown) => setError(e instanceof Error ? e.message : String(e))

  const compile = useCallback(async () => {
    const id = ++reqId.current
    setCompiling(true); setError(null)
    try {
      const r = await api.compile(source, passesParam)
      if (id !== reqId.current) return null
      setResult(r); setResultSource(source); setExec(null)
      setBackendError(null)
      // jump to the failing stage so the error is visible, otherwise keep the user's choice
      if (!r.success) setStage((r.failedStage || 'lexer') as StageId)
      return r
    } catch (e) { fail(e); return null } finally { if (id === reqId.current) setCompiling(false) }
  }, [source, passesParam])

  const run = useCallback(async () => {
    setRunning(true); setError(null)
    try {
      const r = await api.execute(source, passesParam)
      const c = await api.compile(source, passesParam)
      setExec(r); setResult(c); setResultSource(source)
      if (!c.success) setStage((c.failedStage || 'lexer') as StageId)
    } catch (e) { fail(e) } finally { setRunning(false) }
  }, [source, passesParam])

  const loadExample = useCallback((e: Example) => {
    setSource(e.source); setResult(null); setExec(null); setResultSource(null); setDebug(initialDebug)
  }, [setSource])

  const debugStart = useCallback(async () => {
    setDebug((d) => ({ ...d, loading: true, error: null }))
    try {
      const target = debug.target
      const r = await api.debugStart(source, passesParam, target, debug.breakpoints)
      if (!r.success || !r.state || !r.bytecode) {
        setDebug((d) => ({ ...d, loading: false, state: null, bytecode: null, error: 'Program does not compile — fix the errors first (' + (r.failedStage ?? 'compile') + ').' }))
        return
      }
      setDebug((d) => ({ ...d, loading: false, state: r.state!, bytecode: r.bytecode!, source }))
    } catch (e) { setDebug((d) => ({ ...d, loading: false, error: e instanceof Error ? e.message : String(e) })) }
  }, [source, passesParam, debug.target, debug.breakpoints])

  const withSession = useCallback(async (fn: (id: string) => Promise<DebugState>) => {
    const id = debug.state?.session_id
    if (!id) return
    setDebug((d) => ({ ...d, loading: true, error: null }))
    try {
      const st = await fn(id)
      setDebug((d) => ({ ...d, loading: false, state: st }))
    } catch (e) { setDebug((d) => ({ ...d, loading: false, error: e instanceof Error ? e.message : String(e) })) }
  }, [debug.state?.session_id])

  const debugStep = useCallback((dir: 'forward' | 'back', mode: 'instruction' | 'line' | 'over' = 'instruction') => withSession((id) => api.debugStep(id, dir, mode)), [withSession])
  const debugContinue = useCallback(() => withSession((id) => api.debugContinue(id, debug.breakpoints)), [withSession, debug.breakpoints])
  const debugSeek = useCallback((c: number) => withSession((id) => api.debugSeek(id, c)), [withSession])
  const debugReset = useCallback(() => withSession((id) => api.debugReset(id)), [withSession])
  const debugToggleBreakpoint = useCallback(async (line: number) => {
    const next = debug.breakpoints.includes(line) ? debug.breakpoints.filter((l) => l !== line) : [...debug.breakpoints, line].sort((a, b) => a - b)
    setDebug((d) => ({ ...d, breakpoints: next }))
    const id = debug.state?.session_id
    if (id) { try { await api.debugBreakpoints(id, next) } catch { /* session expired; surfaced on next action */ } }
  }, [debug.breakpoints, debug.state?.session_id])
  const debugSetTarget = useCallback((t: 'optimized' | 'baseline') => setDebug({ ...initialDebug, target: t, breakpoints: debug.breakpoints }), [debug.breakpoints])

  const value: Store = {
    source, setSource, loadExample, examples, health, backendError,
    passes, setPass: (id, on) => setPasses((p) => ({ ...p, [id]: on })), passesParam,
    result, resultSource, stale: result !== null && resultSource !== source, compiling, compile, exec, running, run,
    error, clearError: () => setError(null),
    stage, setStage,
    debug, debugStart, debugStep, debugContinue, debugReset, debugSeek, debugToggleBreakpoint, debugSetTarget,
  }
  return <Ctx.Provider value={value}>{children}</Ctx.Provider>
}
