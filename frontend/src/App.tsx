import { lazy, Suspense, useEffect, useState, type ReactNode } from 'react'
import {
  BookOpen, Boxes, BugPlay, Cpu, FlaskConical, GraduationCap, Layers, LayoutDashboard, ListTree, Loader2, Network, Play, ScanText,
  BarChart3, Bot, Database, Sigma, SquareTerminal, Menu, GitBranch, Telescope, TestTube2, TriangleAlert, Zap, X,
} from 'lucide-react'
import { NovaProvider, useNova } from './state/NovaContext'
import { useHashRoute } from './hooks/useHashRoute'
import { Chip } from './components/ui'

const Dashboard = lazy(() => import('./pages/Dashboard'))
const Playground = lazy(() => import('./pages/Playground'))
const Lexical = lazy(() => import('./pages/Lexical'))
const Syntax = lazy(() => import('./pages/Syntax'))
const AstExplorer = lazy(() => import('./pages/AstExplorer'))
const Semantic = lazy(() => import('./pages/Semantic'))
const IRPage = lazy(() => import('./pages/IRPage'))
const OptLab = lazy(() => import('./pages/OptLab'))
const TargetCode = lazy(() => import('./pages/TargetCode'))
const Debugger = lazy(() => import('./pages/Debugger'))
const RuntimeMemory = lazy(() => import('./pages/RuntimeMemory'))
const Tests = lazy(() => import('./pages/Tests'))
const Docs = lazy(() => import('./pages/Docs'))
const Learn = lazy(() => import('./pages/Learn'))
const Analysis = lazy(() => import('./pages/Analysis'))
const Explorer = lazy(() => import('./pages/Explorer'))
const SqlLab = lazy(() => import('./pages/SqlLab'))
const Assistant = lazy(() => import('./pages/Assistant'))
const Native = lazy(() => import('./pages/Native'))
const Visualisation = lazy(() => import('./pages/Visualisation'))

interface NavItem { id: string; label: string; icon: ReactNode; page: ReactNode; group: string; standalone?: boolean }   // standalone: page does not use the NOVA compile/run header
const I = 16
const NAV: NavItem[] = [
  { id: 'dashboard', label: 'Dashboard', icon: <LayoutDashboard size={I} />, page: <Dashboard />, group: 'Workspace' },
  { id: 'playground', label: 'Compiler Playground', icon: <SquareTerminal size={I} />, page: <Playground />, group: 'Workspace' },
  { id: 'learn', label: 'Learn Mode', icon: <GraduationCap size={I} />, page: <Learn />, group: 'Workspace' },
  { id: 'explorer', label: 'Compiler Explorer', icon: <Telescope size={I} />, page: <Explorer />, group: 'Workspace' },
  { id: 'visualisation', label: 'Visualisation', icon: <BarChart3 size={I} />, page: <Visualisation />, group: 'Workspace' },
  { id: 'assistant', label: 'AI Assistant', icon: <Bot size={I} />, page: <Assistant />, group: 'Workspace', standalone: true },
  { id: 'sql', label: 'SQL Compiler', icon: <Database size={I} />, page: <SqlLab />, group: 'Workspace', standalone: true },
  { id: 'lexical', label: 'Lexical Analysis', icon: <ScanText size={I} />, page: <Lexical />, group: 'Front end' },
  { id: 'syntax', label: 'Syntax Analysis', icon: <Network size={I} />, page: <Syntax />, group: 'Front end' },
  { id: 'ast', label: 'AST Explorer', icon: <ListTree size={I} />, page: <AstExplorer />, group: 'Front end' },
  { id: 'semantic', label: 'Semantic Analysis', icon: <Sigma size={I} />, page: <Semantic />, group: 'Front end' },
  { id: 'ir', label: 'Intermediate Repr.', icon: <Layers size={I} />, page: <IRPage />, group: 'Middle & back end' },
  { id: 'analysis', label: 'CFG & Data-flow', icon: <GitBranch size={I} />, page: <Analysis />, group: 'Middle & back end' },
  { id: 'optimizer', label: 'Optimization Lab', icon: <FlaskConical size={I} />, page: <OptLab />, group: 'Middle & back end' },
  { id: 'target', label: 'Target Code (VM)', icon: <Cpu size={I} />, page: <TargetCode />, group: 'Middle & back end' },
  { id: 'native', label: 'Native x86-64', icon: <Cpu size={I} />, page: <Native />, group: 'Middle & back end' },
  { id: 'debugger', label: 'Debugger', icon: <BugPlay size={I} />, page: <Debugger />, group: 'Runtime' },
  { id: 'memory', label: 'Runtime Memory', icon: <Boxes size={I} />, page: <RuntimeMemory />, group: 'Runtime' },
  { id: 'tests', label: 'Compiler Tests', icon: <TestTube2 size={I} />, page: <Tests />, group: 'Project' },
  { id: 'docs', label: 'Documentation', icon: <BookOpen size={I} />, page: <Docs />, group: 'Project' },
]

function Shell() {
  const { compile, run, compiling, running, result, stale, error, clearError, backendError, health } = useNova()
  const [route, go] = useHashRoute('playground')
  const [navOpen, setNavOpen] = useState(false)
  const current = NAV.find((n) => n.id === route) ?? NAV[1]

  // compile once on load so every view is populated with real compiler output
  useEffect(() => { void compile() }, []) // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') { e.preventDefault(); void (e.shiftKey ? run() : compile()) }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [compile, run])

  const status = !result ? null : !result.success ? 'error' : stale ? 'stale' : 'ok'
  const groups = Array.from(new Set(NAV.map((n) => n.group)))

  return (
    <div className="h-full flex">
      <nav className={`w-[212px] shrink-0 border-r border-ink-700 bg-ink-900 flex-col max-md:fixed max-md:inset-y-0 max-md:left-0 max-md:z-40 max-md:shadow-2xl ${navOpen ? 'flex' : 'hidden md:flex'}`}>
        <div className="h-12 px-4 flex items-center gap-2.5 border-b border-ink-700">
          <svg width="22" height="22" viewBox="0 0 32 32"><rect width="32" height="32" rx="7" fill="#181d25" /><path d="M8 24V8l16 16V8" fill="none" stroke="#7b93ff" strokeWidth="3" strokeLinecap="round" strokeLinejoin="round" /></svg>
          <div className="leading-tight">
            <div className="font-semibold tracking-wide">NOVA</div>
            <div className="text-[10px] text-fg-mute -mt-0.5">Compiler Engineering</div>
          </div>
        </div>
        <div className="flex-1 overflow-y-auto py-2">
          {groups.map((g) => (
            <div key={g} className="mb-2">
              <div className="px-4 py-1 text-[10px] uppercase tracking-widest text-ink-500">{g}</div>
              {NAV.filter((n) => n.group === g).map((n) => (
                <button key={n.id} onClick={() => { go(n.id); setNavOpen(false) }}
                  className={`w-full flex items-center gap-2.5 px-4 h-8 text-left transition-colors border-l-2 ${current.id === n.id ? 'border-nova bg-nova/10 text-fg' : 'border-transparent text-fg-dim hover:text-fg hover:bg-ink-800'}`}>
                  {n.icon}<span className="truncate">{n.label}</span>
                </button>
              ))}
            </div>
          ))}
        </div>
        <div className="p-3 border-t border-ink-700 text-2xs text-fg-mute space-y-1">
          <div className="flex items-center gap-1.5"><span className={`w-1.5 h-1.5 rounded-full ${backendError ? 'bg-bad' : health ? 'bg-ok' : 'bg-warn'}`} />{backendError ? 'Backend offline' : health ? 'Backend connected' : 'Connecting…'}</div>
          {health?.parser && <div>{health.parser.algorithm} · {health.parser.states} states</div>}
        </div>
      </nav>

      <div className="flex-1 min-w-0 flex flex-col">
        <header className="h-12 shrink-0 border-b border-ink-700 bg-ink-900 px-4 flex items-center gap-3">
          <button className="btn-icon md:hidden" aria-label="Menu" onClick={() => setNavOpen((o) => !o)}><Menu size={16} /></button>
          <h2 className="font-medium truncate">{current.label}</h2>
          {!current.standalone && status === 'ok' && <Chip tone="ok">compiled</Chip>}
          {!current.standalone && status === 'stale' && <Chip tone="warn" title="Source changed since last compile">source changed — recompile</Chip>}
          {!current.standalone && status === 'error' && <Chip tone="bad">{result!.errorCount} error{result!.errorCount === 1 ? '' : 's'}</Chip>}
          {!current.standalone && result && result.warningCount > 0 && <Chip tone="warn">{result.warningCount} warning{result.warningCount === 1 ? '' : 's'}</Chip>}
          {!current.standalone && <div className="ml-auto flex items-center gap-2">
            <button className="btn" onClick={() => void compile()} disabled={compiling || running} title="Ctrl+Enter">
              {compiling ? <Loader2 size={14} className="animate-spin" /> : <Zap size={14} />} Compile
            </button>
            <button className="btn btn-primary" onClick={() => void run()} disabled={compiling || running} title="Ctrl+Shift+Enter">
              {running ? <Loader2 size={14} className="animate-spin" /> : <Play size={14} />} Run
            </button>
          </div>}
        </header>
        {(backendError || error) && (
          <div className="px-4 py-2 bg-bad/10 border-b border-bad/30 text-bad flex items-center gap-2">
            <TriangleAlert size={15} /><span className="flex-1">{backendError ?? error}</span>
            {error && <button onClick={clearError}><X size={14} /></button>}
          </div>
        )}
        <main className="flex-1 min-h-0 overflow-auto p-4">
          <Suspense fallback={<div className="text-fg-mute p-6 flex items-center gap-2"><Loader2 className="animate-spin" size={16} /> Loading…</div>}>
            {current.page}
          </Suspense>
        </main>
      </div>
    </div>
  )
}

export default function App() {
  return (
    <NovaProvider>
      <Shell />
    </NovaProvider>
  )
}

