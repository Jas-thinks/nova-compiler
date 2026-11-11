import { fireEvent, render, screen, waitFor } from '@testing-library/react'
import { vi } from 'vitest'
import SqlLab from './SqlLab'
import { api } from '../services/api'
import type { SqlResponse } from '../types/api'

const response: SqlResponse = {
  success: true, tokenCount: 12, errorCount: 0, elapsedMs: 1.5, diagnostics: [],
  catalog: [{ name: 'users', rows: 2, columns: [{ name: 'id', type: 'INT', notNull: true, primaryKey: true }] }],
  statements: [{
    index: 0, kind: 'SELECT', text: 'SELECT id, name FROM users', ast: '(select (items id name))', ok: true,
    columns: ['id', 'name'], types: ['INT', 'TEXT'], rows: [[1, 'ann'], [2, null]], rowCount: 2, rowsAffected: 2,
    plan: { original: 'Scan users', optimized: 'Scan users', optimizedWithStats: 'Scan users [rows=2]', tree: { op: 'Scan', detail: 'Scan users', rows: 2, width: 2, children: [] },
      history: [{ rule: 'predicate_pushdown', description: 'moves filters below joins', before: 'Filter\n  Join', after: 'Join\n  Filter' }] },
    bytecode: [{ source: 'users.id', code: ['000  LOAD slot0'] }], stats: { rowsScanned: 2 }, elapsedMs: 0.2, diagnostics: [],
  }],
}

describe('SqlLab', () => {
  it('runs the script on load and renders rows, NULLs and column types', async () => {
    const spy = vi.spyOn(api, 'sql').mockResolvedValue(response)
    render(<SqlLab />)
    await waitFor(() => expect(screen.getByText('ann')).toBeInTheDocument())
    expect(screen.getByText('NULL')).toBeInTheDocument()
    expect(screen.getByText('TEXT')).toBeInTheDocument()
    expect(spy).toHaveBeenCalledTimes(1)
  })
  it('shows the optimizer rewrite history on its tab', async () => {
    vi.spyOn(api, 'sql').mockResolvedValue(response)
    render(<SqlLab />)
    await waitFor(() => screen.getByText('ann'))
    fireEvent.click(screen.getByRole('button', { name: 'optimizer' }))
    expect(screen.getByText('predicate_pushdown')).toBeInTheDocument()
    expect(screen.getByText('moves filters below joins')).toBeInTheDocument()
  })
  it('reports API failures instead of crashing', async () => {
    vi.spyOn(api, 'sql').mockRejectedValue(new Error('backend down'))
    render(<SqlLab />)
    await waitFor(() => expect(screen.getByText('backend down')).toBeInTheDocument())
  })
})
