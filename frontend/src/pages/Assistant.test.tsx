import { fireEvent, render, screen, waitFor } from '@testing-library/react'
import { vi } from 'vitest'
import Assistant, { DiffView } from './Assistant'
import { api } from '../services/api'
import type { SuggestResponse } from '../types/api'

const setSource = vi.fn()
vi.mock('../state/NovaContext', () => ({
  useNova: () => ({ source: 'fn main() { let x: int = 1.5; }', setSource, examples: [], loadExample: vi.fn() }),
}))

const sug: SuggestResponse = {
  errors: 1, compiles: false,
  diagnostics: [{ severity: 'error', phase: 'semantic', category: 'Type mismatch', code: 'E-SEM-004', message: 'Cannot initialise x', explanation: '', suggestion: '', loc: { line: 1, col: 25, endLine: 1, endCol: 28 } }],
  fixes: [{ title: 'Convert with to_int(…) (truncates)', code: 'E-SEM-004', line: 1, kind: 'quickfix', severity: 'error', diagnostic: 'Cannot initialise x', verified: true, errorsBefore: 1, errorsAfter: 0, compilesAfter: true,
    diff: '--- original.nova\n+++ fixed.nova\n@@ -1 +1 @@\n-fn main() { let x: int = 1.5; }\n+fn main() { let x: int = to_int(1.5); }\n', patched: 'fn main() { let x: int = to_int(1.5); }' }],
}

describe('Assistant', () => {
  beforeEach(() => { setSource.mockClear() })

  it('shows a compiler-verified fix with its diff and applies it', async () => {
    vi.spyOn(api, 'suggest').mockResolvedValue(sug)
    render(<Assistant />)
    await waitFor(() => expect(screen.getByText('verified by compiler')).toBeInTheDocument())
    expect(screen.getByText('Convert with to_int(…) (truncates)')).toBeInTheDocument()
    expect(screen.getByText((_, el) => el?.textContent === '1 → 0 errors' && el.children.length === 0)).toBeInTheDocument()
    fireEvent.click(screen.getByRole('button', { name: /Apply fix/ }))
    expect(setSource).toHaveBeenCalledWith('fn main() { let x: int = to_int(1.5); }')
  })

  it('explains runtime failures from the rule-based engine', async () => {
    vi.spyOn(api, 'suggest').mockResolvedValue({ errors: 0, compiles: true, diagnostics: [], fixes: [] })
    vi.spyOn(api, 'explainRuntime').mockResolvedValue({ status: 'runtime_error', mode: 'rules', error: { message: 'division by zero', line: 8, col: 11 }, function: 'main', sourceLine: 'print(a / zero);',
      variables: { a: '10', zero: '0' }, analysis: ['The divisor `zero` is 0 when line 8 runs.'], suggestions: ['Guard the operation.'], evidence: [] })
    render(<Assistant />)
    fireEvent.click(screen.getByRole('button', { name: /Runtime analysis/ }))
    fireEvent.click(screen.getByRole('button', { name: /Run and explain/ }))
    await waitFor(() => expect(screen.getByText('division by zero')).toBeInTheDocument())
    expect(screen.getByText('zero')).toBeInTheDocument()      // rendered as inline code, not literal backticks
    expect(screen.getByText('zero = 0')).toBeInTheDocument()
  })

  it('says plainly when Claude is not configured', async () => {
    vi.spyOn(api, 'suggest').mockResolvedValue({ errors: 0, compiles: true, diagnostics: [], fixes: [] })
    vi.spyOn(api, 'aiFix').mockResolvedValue({ available: false, note: 'Set ANTHROPIC_API_KEY to enable AI assistance.' })
    render(<Assistant />)
    fireEvent.click(screen.getByRole('button', { name: /Ask Claude/ }))
    fireEvent.click(screen.getByRole('button', { name: /Ask Claude to fix compile errors/ }))
    await waitFor(() => expect(screen.getByText('Claude is not configured on this server.')).toBeInTheDocument())
  })
})

describe('DiffView', () => {
  it('colours added and removed lines', () => {
    const { container } = render(<DiffView diff={'-old\n+new\n same'} />)
    expect(container.querySelector('.text-ok')?.textContent).toBe('+new')
    expect(container.querySelector('.text-bad')?.textContent).toBe('-old')
  })
})
