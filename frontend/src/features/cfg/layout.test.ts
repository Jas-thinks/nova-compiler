import { layoutCfg } from './layout'
import type { CfgBlock, CfgEdgeInfo } from '../../types/api'

const blk = (id: number, reachable = true): CfgBlock => ({ id, labels: [], instructions: [{ index: id, text: 'x', line: 1 }], succ: [], pred: [], idom: -1, reachable, loopHeader: false, firstLine: 1, lastLine: 1 })
const edge = (from: number, to: number, back = false): CfgEdgeInfo => ({ from, to, kind: 'fall', back })

describe('CFG layout', () => {
  it('ranks a diamond with branches side by side', () => {
    const p = layoutCfg([blk(0), blk(1), blk(2), blk(3)], [edge(0, 1), edge(0, 2), edge(1, 3), edge(2, 3)])
    const by = Object.fromEntries(p.map((x) => [x.id, x]))
    expect([by[0].rank, by[1].rank, by[2].rank, by[3].rank]).toEqual([0, 1, 1, 2])
    expect(by[1].y).toBe(by[2].y)
    expect(by[1].x).not.toBe(by[2].x)
    expect(by[3].y).toBeGreaterThan(by[1].y)
  })
  it('ignores back edges so loops do not create cycles in ranking', () => {
    const p = layoutCfg([blk(0), blk(1), blk(2), blk(3)], [edge(0, 1), edge(1, 2), edge(2, 1, true), edge(1, 3)])
    const by = Object.fromEntries(p.map((x) => [x.id, x]))
    expect(by[1].rank).toBe(1)
    expect(by[2].rank).toBe(2)
    expect(by[3].rank).toBe(2)
  })
  it('places unreachable blocks in a separate last row', () => {
    const p = layoutCfg([blk(0), blk(1, false)], [])
    expect(p.find((x) => x.id === 1)!.y).toBeGreaterThan(p.find((x) => x.id === 0)!.y)
  })
})
