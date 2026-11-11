import type { AstNode } from '../../types/api'

export interface Positioned {
  id: number
  node: AstNode
  x: number
  y: number
  parent?: number
  collapsed: boolean
  hasChildren: boolean
}

export const NODE_W = 150
export const NODE_H = 44
const GAP_X = 14
const GAP_Y = 56

/**
 * Tidy tree layout: every leaf gets its own column, parents are centred above their (visible) children.
 * `collapsed` holds ids whose subtrees are hidden.
 */
export function layoutTree(root: AstNode, collapsed: Set<number>): Positioned[] {
  const out: Positioned[] = []
  let nextLeaf = 0
  const place = (n: AstNode, depth: number, parent?: number): number => {
    const isCollapsed = collapsed.has(n.id)
    const kids = isCollapsed ? [] : n.children
    let x: number
    const idx = out.length
    out.push({ id: n.id, node: n, x: 0, y: depth * (NODE_H + GAP_Y), parent, collapsed: isCollapsed, hasChildren: n.children.length > 0 })
    if (kids.length === 0) {
      x = nextLeaf * (NODE_W + GAP_X)
      nextLeaf += 1
    } else {
      const xs = kids.map((k) => place(k, depth + 1, n.id))
      x = (xs[0] + xs[xs.length - 1]) / 2
    }
    out[idx].x = x
    return x
  }
  place(root, 0)
  return out
}

export function countNodes(n: AstNode): number {
  return 1 + n.children.reduce((s, c) => s + countNodes(c), 0)
}

export function findNode(n: AstNode, id: number): AstNode | undefined {
  if (n.id === id) return n
  for (const c of n.children) {
    const f = findNode(c, id)
    if (f) return f
  }
  return undefined
}

export function kindColor(kind: string): string {
  switch (kind) {
    case 'Program': case 'Function': return '#7b93ff'
    case 'Block': return '#667085'
    case 'VarDecl': case 'Assign': return '#3fb68b'
    case 'If': case 'While': case 'Return': return '#e5a93a'
    case 'Print': case 'ExprStmt': case 'Call': return '#d97ad9'
    case 'Binary': case 'Unary': return '#58b8e6'
    default: return '#98a2b3'
  }
}
