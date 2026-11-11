import { countNodes, findNode, layoutTree, NODE_H } from './layout'
import type { AstNode } from '../../types/api'

const loc = { line: 1, col: 1, endLine: 1, endCol: 2 }
const n = (id: number, children: AstNode[] = []): AstNode => ({ id, kind: 'Binary', label: 'n' + id, loc, children })
const tree = n(0, [n(1, [n(3), n(4)]), n(2)])

describe('AST layout', () => {
  it('gives each leaf its own column and centres parents', () => {
    const p = layoutTree(tree, new Set())
    const by = Object.fromEntries(p.map((x) => [x.id, x]))
    expect(new Set([by[3].x, by[4].x, by[2].x]).size).toBe(3)
    expect(by[1].x).toBeCloseTo((by[3].x + by[4].x) / 2)
    expect(by[0].x).toBeCloseTo((by[1].x + by[2].x) / 2)
    expect(by[3].y).toBe(2 * by[0].y + 2 * (NODE_H + 56))
  })
  it('hides children of collapsed nodes', () => {
    const p = layoutTree(tree, new Set([1]))
    expect(p.map((x) => x.id)).toEqual([0, 1, 2])
    expect(p.find((x) => x.id === 1)?.collapsed).toBe(true)
  })
  it('counts and finds nodes', () => {
    expect(countNodes(tree)).toBe(5)
    expect(findNode(tree, 4)?.label).toBe('n4')
    expect(findNode(tree, 99)).toBeUndefined()
  })
})
