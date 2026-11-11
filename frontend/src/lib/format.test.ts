import { diagnosticToMarker, ms, pct, sliceSource } from './format'
import type { Diagnostic } from '../types/api'

const diag: Diagnostic = {
  severity: 'error', phase: 'semantic', category: 'Undefined variable', code: 'E-SEM-001', message: "Variable 'x' is not declared",
  explanation: '', suggestion: "Did you mean 'y'?", loc: { line: 3, col: 5, endLine: 3, endCol: 6 },
}

describe('format helpers', () => {
  it('converts diagnostics to Monaco markers', () => {
    const m = diagnosticToMarker(diag)
    expect(m).toMatchObject({ severity: 8, startLineNumber: 3, startColumn: 5, endColumn: 6, code: 'E-SEM-001' })
    expect(m.message).toContain("Did you mean 'y'?")
    expect(diagnosticToMarker({ ...diag, severity: 'warning' }).severity).toBe(4)
  })
  it('widens zero-width ranges so squiggles are visible', () => {
    expect(diagnosticToMarker({ ...diag, loc: { line: 1, col: 4, endLine: 1, endCol: 4 } }).endColumn).toBe(5)
  })
  it('formats durations and percentages', () => {
    expect(ms(0.001)).toBe('<0.01 ms')
    expect(ms(1.234)).toBe('1.23 ms')
    expect(ms(45.67)).toBe('45.7 ms')
    expect(pct(50, 20)).toBe('60%')
    expect(pct(0, 0)).toBe('0%')
  })
  it('slices source by location', () => {
    expect(sliceSource('let abc = 1;', { line: 1, col: 5, endLine: 1, endCol: 8 })).toBe('abc')
  })
})
