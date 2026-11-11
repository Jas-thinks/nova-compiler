import { diffLines } from './diff'

describe('diffLines', () => {
  it('marks removed and added lines and keeps common ones', () => {
    const d = diffLines(['a', 'b', 'c'], ['a', 'c', 'd'])
    expect(d).toEqual([
      { type: 'same', text: 'a' },
      { type: 'removed', text: 'b' },
      { type: 'same', text: 'c' },
      { type: 'added', text: 'd' },
    ])
  })
  it('handles empty sides', () => {
    expect(diffLines([], ['x'])).toEqual([{ type: 'added', text: 'x' }])
    expect(diffLines(['x'], [])).toEqual([{ type: 'removed', text: 'x' }])
    expect(diffLines([], [])).toEqual([])
  })
  it('reports identical input as all same', () => {
    expect(diffLines(['q', 'r'], ['q', 'r']).every((o) => o.type === 'same')).toBe(true)
  })
})
