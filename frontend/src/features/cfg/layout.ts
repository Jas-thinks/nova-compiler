import type { CfgBlock, CfgEdgeInfo } from '../../types/api'

export const CFG_W = 250
const LINE_H = 17
const HEAD_H = 30
const GAP_X = 40
const GAP_Y = 46

export interface CfgPos { id: number; x: number; y: number; w: number; h: number; rank: number }

export const blockHeight = (b: CfgBlock) => HEAD_H + 10 + Math.max(1, b.instructions.length) * LINE_H

/**
 * Layered layout. rank(b) = longest path from the entry over *forward* edges only (back edges are ignored, which
 * makes loops come out as a downward chain with a back edge returning upwards). Blocks of equal rank sit side by side.
 */
export function layoutCfg(blocks: CfgBlock[], edges: CfgEdgeInfo[]): CfgPos[] {
  const rank = new Map<number, number>()
  const fwd = edges.filter((e) => !e.back)
  const reachable = blocks.filter((b) => b.reachable)
  // iterative longest-path relaxation (graph without back edges is acyclic)
  reachable.forEach((b) => rank.set(b.id, 0))
  for (let pass = 0; pass < blocks.length; pass++) {
    let changed = false
    for (const e of fwd) {
      if (!rank.has(e.from) || !rank.has(e.to)) continue
      const r = rank.get(e.from)! + 1
      if (r > rank.get(e.to)!) { rank.set(e.to, r); changed = true }
    }
    if (!changed) break
  }
  // unreachable blocks go to a final row so they are still visible
  const maxRank = Math.max(0, ...rank.values())
  blocks.filter((b) => !b.reachable).forEach((b) => rank.set(b.id, maxRank + 1))
  const rows = new Map<number, CfgBlock[]>()
  blocks.forEach((b) => { const r = rank.get(b.id)!; rows.set(r, [...(rows.get(r) ?? []), b]) })
  const out: CfgPos[] = []
  let y = 0
  const widest = Math.max(...[...rows.values()].map((r) => r.length))
  for (const r of [...rows.keys()].sort((a, b) => a - b)) {
    const row = rows.get(r)!
    const total = row.length * CFG_W + (row.length - 1) * GAP_X
    const full = widest * CFG_W + (widest - 1) * GAP_X
    let x = (full - total) / 2
    for (const b of row) { out.push({ id: b.id, x, y, w: CFG_W, h: blockHeight(b), rank: r }); x += CFG_W + GAP_X }
    y += Math.max(...row.map(blockHeight)) + GAP_Y
  }
  return out
}
