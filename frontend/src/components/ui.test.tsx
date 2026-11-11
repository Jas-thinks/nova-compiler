import { render, screen } from '@testing-library/react'
import { DiagnosticList, Empty, SourceView } from './ui'
import { Watch } from '../pages/Debugger'
import type { Diagnostic, DebugFrame } from '../types/api'

const d: Diagnostic = {
  severity: 'error', phase: 'syntax', category: 'Missing semicolon', code: 'E-SYN-001', message: "Expected ';' but found 'print'",
  explanation: 'Every statement ends with a semicolon.', suggestion: "Insert ';'", loc: { line: 2, col: 19, endLine: 2, endCol: 20 },
}

describe('components', () => {
  it('renders diagnostics with location, category and suggestion', () => {
    render(<DiagnosticList items={[d]} />)
    expect(screen.getByText("Expected ';' but found 'print'")).toBeInTheDocument()
    expect(screen.getByText('2:19')).toBeInTheDocument()
    expect(screen.getByText('Missing semicolon')).toBeInTheDocument()
    expect(screen.getByText(/Insert ';'/)).toBeInTheDocument()
  })
  it('shows an empty diagnostics message', () => {
    render(<DiagnosticList items={[]} />)
    expect(screen.getByText('No diagnostics.')).toBeInTheDocument()
  })
  it('highlights the requested source range', () => {
    const { container } = render(<SourceView source={'let abc = 1;\nprint(abc);'} highlights={[{ line: 1, col: 5, endLine: 1, endCol: 8 }]} />)
    const marks = container.querySelectorAll('mark')
    expect(marks).toHaveLength(1)
    expect(marks[0].textContent).toBe('abc')
  })
  it('renders Empty states', () => {
    render(<Empty title="Nothing here" hint="Do something" />)
    expect(screen.getByText('Do something')).toBeInTheDocument()
  })
  it('debugger watch hides temporaries unless asked', () => {
    const frame: DebugFrame = { depth: 0, function: 'main', callLine: 0, stack: [], locals: [
      { slot: 0, name: 'x', type: 'int', value: '5', temp: false, param: false },
      { slot: 1, name: 't1', type: 'int', value: '7', temp: true, param: false },
    ] }
    const { rerender } = render(<Watch frame={frame} showTemps={false} />)
    expect(screen.getByText('5')).toBeInTheDocument()
    expect(screen.queryByText('7')).toBeNull()
    rerender(<Watch frame={frame} showTemps />)
    expect(screen.getByText('7')).toBeInTheDocument()
  })
})
