import { render, screen, waitFor } from '@testing-library/react'
import { vi } from 'vitest'
import Visualisation, { HBars, Intervals, LineChart, StackedBars } from './Visualisation'
import { api } from '../services/api'

const trace = [1, 2, 3, 2, 1].map((n, i) => ({ step: i + 1, stack: Array(2 * n - 1).fill('x'), lookahead: 'a', input: [], action: 'shift 1', prod: -1, detail: '' }))
const result = {
  success: true,
  parser: { totalSteps: 5, shifts: 3, reduces: 2, maxStackDepth: 3, traceTruncated: false, trace },
  optimization: { rounds: 2, instructionsBefore: 10, instructionsAfter: 4, eliminated: 6, history: [
    { pass: 'constant_folding', function: 'main', before: '', after: '', explanation: '', round: 1 },
    { pass: 'dce', function: 'main', before: '', after: '', explanation: '', round: 1 },
    { pass: 'dce', function: 'main', before: '', after: '', explanation: '', round: 2 }] },
  analysis: { original: [{ name: 'main', registerAllocation: { k: 4, colorsUsed: 2, spills: 0, minimalK: 2, nodes: [
    { name: 'a', color: 0, spilled: false, first: 0, last: 3, uses: 2 }, { name: 'b', color: 1, spilled: false, first: 2, last: 5, uses: 1 }] } }] },
  execution: { optimized: { opCounts: { LOAD: 60, STORE: 30, PUSH: 5, ADD: 5 } } },
}
vi.mock('../state/NovaContext', () => ({ useNova: () => ({ result, exec: null, compile: vi.fn(), run: vi.fn(), compiling: false, running: false }) }))

describe('Visualisation page', () => {
  it('renders every chart from compiler data', async () => {
    vi.spyOn(api, 'sql').mockResolvedValue({ success: true, tokenCount: 0, errorCount: 0, elapsedMs: 0, diagnostics: [], catalog: [], statements: [{ kind: 'SELECT', index: 0, text: '', ast: '', ok: true, columns: [], types: [], rows: [], rowCount: 0, rowsAffected: 0, bytecode: [], stats: {}, elapsedMs: 0, diagnostics: [],
      plan: { original: '', optimized: '', optimizedWithStats: '', history: [], tree: { op: 'Limit', detail: '', rows: 2, width: 1, children: [{ op: 'Scan', detail: '', rows: 4, width: 1, children: [] }] } } }] })
    render(<Visualisation />)
    expect(screen.getByText('LR(1) parser stack depth')).toBeInTheDocument()
    expect(screen.getByText(/of the 100 executed instructions are LOAD/)).toBeInTheDocument()     // 95% data movement
    expect(screen.getByText('round 2')).toBeInTheDocument()
    expect(screen.getByText('−6')).toBeInTheDocument()          // IR shrank by 6 instructions
    expect(screen.getByText('K = 4')).toBeInTheDocument()
    await waitFor(() => expect(screen.getByText('Limit')).toBeInTheDocument())
    expect(screen.getByText('Scan')).toBeInTheDocument()
  })
})

describe('chart components', () => {
  it('LineChart draws a path and labelled axes', () => {
    const { container } = render(<LineChart points={[[0, 0], [5, 3], [10, 1]]} xLabel="step" yLabel="depth" />)
    expect(container.querySelectorAll('path').length).toBe(2)
    expect(screen.getByText('step')).toBeInTheDocument()
  })
  it('HBars scales bars relative to the maximum', () => {
    const { container } = render(<HBars rows={[{ label: 'A', value: 100 }, { label: 'B', value: 50 }]} />)
    const bars = Array.from(container.querySelectorAll('[style*="width"]')) as HTMLElement[]
    expect(bars.map((b) => b.style.width)).toEqual(['100%', '50%'])
  })
  it('StackedBars shows totals per round and a legend', () => {
    render(<StackedBars rounds={[1, 2]} series={[{ name: 'constant_folding', color: '#fff', values: [3, 1] }]} />)
    expect(screen.getByText('constant folding')).toBeInTheDocument()
    expect(screen.getByText('3')).toBeInTheDocument()
  })
  it('Intervals marks spilled variables', () => {
    render(<Intervals nodes={[{ name: 'x', color: 0, spilled: true, first: 0, last: 2 }]} />)
    expect(screen.getByText('spilled')).toBeInTheDocument()
  })
})
