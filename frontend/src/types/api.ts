// Shapes of the JSON returned by the NOVA backend (which mirrors the C++ compiler's output).
export interface Loc { line: number; col: number; endLine: number; endCol: number }

export interface Diagnostic {
  severity: 'error' | 'warning' | 'info'
  phase: string
  category: string
  code: string
  message: string
  explanation: string
  suggestion: string
  loc: Loc
}

export interface Stage {
  id: string
  name: string
  status: 'ok' | 'error' | 'skipped'
  durationMs: number
  summary: Record<string, number>
}

export interface Token { category: string; lexeme: string; terminal: string; loc: Loc; offset: number }

export interface ParseStep {
  step: number
  stack: string[]
  lookahead: string
  input: string[]
  action: string
  prod: number
  detail: string
}
export interface ParseNode { symbol: string; isToken: boolean; lexeme?: string; prod?: number; loc?: Loc; children: ParseNode[] }
export interface ParserInfo {
  algorithm: string
  ok: boolean
  states: number
  totalSteps: number
  shifts: number
  reduces: number
  maxStackDepth: number
  traceTruncated: boolean
  trace: ParseStep[]
  tree?: ParseNode
}

export interface AstNode { id: number; kind: string; label: string; loc: Loc; type?: string; children: AstNode[] }

export interface SymbolInfo {
  name: string; uniqueName: string; type: string; kind: 'variable' | 'parameter'
  declLoc: Loc; reads: Loc[]; writes: Loc[]; readCount: number; writeCount: number
}
export interface ScopeInfo { id: number; parent: number; kind: string; owner: string; loc: Loc; symbols: SymbolInfo[]; children: number[] }
export interface FuncSig { name: string; params: { name: string; type: string }[]; returnType: string; builtin: boolean; calls: number; loc?: Loc }
export interface SemanticInfo { scopes: ScopeInfo[]; functions: FuncSig[] }

export interface Operand { kind: 'none' | 'const' | 'var' | 'temp'; text: string; type: string }
export interface IRInstr {
  index: number; text: string; explanation: string; kind: string; op?: string
  dst?: Operand; a?: Operand; b?: Operand; target?: string; callee?: string; line?: number; col?: number
}
export interface IRFunction { name: string; returnType: string; params: { name: string; type: string }[]; instructions: IRInstr[]; count: number }
export interface IRProgram { functions: IRFunction[]; instructionCount: number }

export interface OptRecord { pass: string; function: string; before: string; after: string; explanation: string; round: number; line?: number; col?: number }
export interface OptInfo {
  history: OptRecord[]
  perPass: Record<string, number>
  rounds: number
  instructionsBefore: number
  instructionsAfter: number
  eliminated: number
  config: Record<string, boolean>
  passes: { id: string; title: string; count: number }[]
}

export interface BcInstr { addr: number; op: string; text: string; comment: string; description: string; function: string; line?: number; col?: number }
export interface BcFunction { index: number; name: string; entry: number; end: number; params: number; returnsValue: boolean; slotNames: string[]; slotTypes: string[]; slotIsTemp: boolean[] }
export interface Bytecode { functions: BcFunction[]; instructions: BcInstr[]; count: number; mainIndex: number }

export interface RuntimeError { message: string; ip: number; line: number; col: number }
export interface VMRun {
  ok: boolean
  output: string[]
  steps: number
  maxStackDepth: number
  maxCallDepth: number
  error?: RuntimeError
  opCounts: Record<string, number>
}
export interface Execution {
  baseline: VMRun
  optimized: VMRun
  reference: { ok: boolean; output: string[]; error: string; inconclusive: boolean }
  optimizationPreservesBehavior: boolean
  matchesReference: boolean
}

export interface CompileResponse {
  success: boolean
  failedStage: string
  stages: Stage[]
  diagnostics: Diagnostic[]
  errorCount: number
  warningCount: number
  tokens: Token[]
  parser: ParserInfo
  ast?: AstNode
  semantic?: SemanticInfo
  ir?: { original: IRProgram; optimized: IRProgram }
  analysis?: AnalysisInfo
  optimization?: OptInfo
  bytecode?: { optimized: Bytecode; baseline: Bytecode }
  execution?: Execution
}

export interface ExecuteResponse {
  success: boolean
  failedStage: string
  diagnostics: Diagnostic[]
  stages: Stage[]
  execution: Execution | null
  output: string[]
  error: RuntimeError | null
}

export interface OptimizeResponse {
  success: boolean
  failedStage?: string
  diagnostics?: Diagnostic[]
  config: Record<string, boolean>
  ir: { original: IRProgram; optimized: IRProgram }
  optimization: OptInfo
  execution: Execution
  metrics: {
    ir_before: number; ir_after: number; ir_eliminated: number
    bytecode_before: number; bytecode_after: number
    vm_steps_before: number; vm_steps_after: number
    semantics_preserved: boolean; matches_reference_interpreter: boolean
  }
  bytecode: { optimized: Bytecode; baseline: Bytecode }
}

export interface Example { id: string; title: string; description: string; category: string; source: string }

export interface HealthInfo {
  status: string
  compiler_found: boolean
  llm_available: boolean
  parser?: { algorithm: string; states: number; conflicts: number }
  limits: { max_source_bytes: number; compile_timeout_s: number; max_vm_steps: number }
  sandbox?: { process_timeout: boolean; output_cap_bytes: number; rlimits_available: boolean; isolation: string }
}

export interface GrammarProduction { id: number; lhs: string; rhs: string[]; text: string }
export interface GrammarInfo {
  algorithm: string
  states: number
  buildMillis: number
  conflicts: string[]
  shiftEntries: number; reduceEntries: number; gotoEntries: number
  grammar: {
    productions: GrammarProduction[]; terminals: string[]; nonterminals: string[]
    first: Record<string, string[]>; follow: Record<string, string[]>
  }
  stateList: { id: number; kernel: string[]; action: Record<string, string>; goto: Record<string, string> }[]
}

// ---- debugger ----
export interface DebugLocal { slot: number; name: string; type: string; value: string; temp: boolean; param: boolean }
export interface DebugFrame { depth: number; function: string; callLine: number; locals: DebugLocal[]; stack: string[] }
export interface DebugState {
  session_id: string
  cursor: number
  total_steps: number
  trace_length: number
  trace_truncated: boolean
  step: number
  ip: number
  instruction: { addr: number; text: string; comment: string; line: number; col: number; description: string; function: string }
  event: string
  frames: DebugFrame[]
  output: string[]
  finished: boolean
  error: RuntimeError | null
  history: { step: number; addr: number; text: string; line: number; event: string }[]
  reason: string
  breakpoints: number[]
  breakpoint_addrs: number[]
  target: string
}
export interface DebugStartResponse {
  success: boolean
  failedStage?: string
  diagnostics: Diagnostic[]
  stages?: Stage[]
  bytecode?: Bytecode
  state?: DebugState
}

export interface UnitTestReport { passed: number; failed: number; total: number; tests: { suite: string; name: string; passed: boolean; message: string }[] }
export interface FuzzReport {
  requested: number; seed: number; compiled: number; passed: number; failed: number; inconclusive: number; generatorFailures: number
  irInstructionsBefore: number; irInstructionsAfter: number; vmStepsBefore: number; vmStepsAfter: number
  failures: { seed: number; kind: string; detail: string; source: string }[]
}
export interface TestRunResponse { unit?: UnitTestReport; differential?: FuzzReport }

// ---- advanced analysis ----
export interface CfgInstr { index: number; text: string; line: number }
export interface CfgBlock {
  id: number; labels: string[]; instructions: CfgInstr[]; succ: number[]; pred: number[]
  idom: number; reachable: boolean; loopHeader: boolean; firstLine: number; lastLine: number
}
export interface CfgEdgeInfo { from: number; to: number; kind: 'fall' | 'jump' | 'true' | 'false'; back: boolean }
export interface DataflowIter { iteration: number; in: string[][]; out: string[][] }
export interface DataflowInfo {
  name: string; direction: string; converged: boolean; iterations: number
  use?: string[][]; def?: string[][]; gen?: string[][]; kill?: string[][]
  in: string[][]; out: string[][]; history: DataflowIter[]; definitions?: Record<string, string>
}
export interface SsaPhiInfo { var: string; dst: string; text: string; args: { block: number; name: string }[] }
export interface SsaBlockInfo { id: number; phis: SsaPhiInfo[]; code: string[] }
export interface FunctionAnalysis {
  name: string; blocks: CfgBlock[]; edges: CfgEdgeInfo[]; rpo: number[]
  liveness: DataflowInfo; reachingDefinitions: DataflowInfo
  ssa: { blocks: SsaBlockInfo[]; phiCount: number; definitions: number }
  summary: { blocks: number; edges: number; loops: number; phis: number; livenessIterations: number; reachingDefsIterations: number }
}
export interface AnalysisInfo { original: FunctionAnalysis[]; optimized: FunctionAnalysis[] }

export interface ExplorerLanguage {
  id: string; name: string; status: 'full' | 'available' | 'unavailable' | 'not_implemented'
  description: string; outputs: string[]; toolchain?: string | null; note?: string | null; next_step?: string | null
}
export interface ExplorerCResult { tool: string; tool_path: string; opt_level: number; ok: boolean; outputs: Record<string, string>; diagnostics: string; note: string }
export interface Capabilities {
  nova: {
    pipeline_stages: string[]; stages_ok: boolean
    parser: { algorithm: string; states: number; conflicts: number; productions: number }
    optimization_passes: string[]; analyses: Record<string, boolean>; bytecode_instructions: number
    execution_verified_against_reference: boolean
  }
  explorer: ExplorerLanguage[]
  toolchains: Record<string, string | null>
  llm_available: boolean
}
export interface LastTests { ran: boolean; at?: number; result?: TestRunResponse }
