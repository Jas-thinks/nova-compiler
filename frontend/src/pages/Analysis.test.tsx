import { fireEvent, render, screen } from '@testing-library/react'
import { Dataflow } from './Analysis'
import type { DataflowInfo, FunctionAnalysis } from '../types/api'

const blk = (id: number) => ({ id, labels: [], instructions: [], succ: [], pred: [], idom: -1, reachable: true, loopHeader: id === 1, firstLine: 0, lastLine: 0 })
const live: DataflowInfo = {
  name: 'liveness', direction: 'backward', converged: true, iterations: 2,
  use: [[], ['i']], def: [['i'], []],
  in: [[], ['i']], out: [['i'], []],
  history: [
    { iteration: 1, in: [[], ['i']], out: [['i'], []] },
    { iteration: 2, in: [[], ['i']], out: [['i'], []] },
  ],
}
const fn = { name: 'main', blocks: [blk(0), blk(1)], edges: [], rpo: [0, 1], liveness: live, reachingDefinitions: live, ssa: { blocks: [], phiCount: 0, definitions: 0 },
  summary: { blocks: 2, edges: 0, loops: 1, phis: 0, livenessIterations: 2, reachingDefsIterations: 2 } } as unknown as FunctionAnalysis

describe('Dataflow view', () => {
  it('shows USE/DEF/IN/OUT sets and convergence', () => {
    render(<Dataflow fn={fn} kind="liveness" />)
    expect(screen.getByText('USE')).toBeInTheDocument()
    expect(screen.getByText('DEF')).toBeInTheDocument()
    expect(screen.getByText(/converged after 2 iteration/)).toBeInTheDocument()
    expect(screen.getByText(/fixed point/)).toBeInTheDocument()
    expect(screen.getAllByText('∅').length).toBeGreaterThan(0)
  })
  it('steps through iteration history', () => {
    render(<Dataflow fn={fn} kind="liveness" />)
    expect(screen.getByText('iteration 2 / 2')).toBeInTheDocument()
    fireEvent.change(screen.getByRole('slider'), { target: { value: '1' } })
    expect(screen.getByText('iteration 1 / 2')).toBeInTheDocument()
  })
})
