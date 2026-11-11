import { memo, useMemo } from 'react'
import { Background, Controls, Handle, MarkerType, Position, ReactFlow, type Edge, type Node, type NodeProps } from '@xyflow/react'
import '@xyflow/react/dist/style.css'
import { Repeat } from 'lucide-react'
import type { FunctionAnalysis, CfgBlock } from '../../types/api'
import { layoutCfg } from './layout'

interface BlockData extends Record<string, unknown> { block: CfgBlock; selected: boolean; w: number; h: number }

const BlockNode = memo(function BlockNode({ data }: NodeProps<Node<BlockData>>) {
  const { block: b, selected, w, h } = data
  const border = !b.reachable ? '#f2555a' : b.loopHeader ? '#e5a93a' : selected ? '#9db0ff' : '#2d3644'
  return (
    <div style={{ width: w, height: h, borderColor: border, opacity: b.reachable ? 1 : 0.65 }} className={`rounded-md bg-ink-850 border ${selected ? 'ring-1 ring-nova-strong' : ''} overflow-hidden cursor-pointer`}>
      <Handle type="target" position={Position.Top} style={{ opacity: 0 }} />
      <div className="px-2 h-[30px] flex items-center gap-2 border-b border-ink-700 bg-ink-800 text-[11.5px]">
        <b className="text-nova-strong">B{b.id}</b>
        <span className="text-fg-mute truncate code">{b.labels.join(', ')}</span>
        {b.loopHeader && <span className="ml-auto chip border-warn/40 text-warn bg-warn/10"><Repeat size={10} />loop</span>}
        {!b.reachable && <span className="ml-auto chip border-bad/40 text-bad bg-bad/10">unreachable</span>}
        {b.id === 0 && <span className="ml-auto chip border-ok/40 text-ok bg-ok/10">entry</span>}
      </div>
      <div className="px-2 py-[5px] code text-[11px] leading-[17px]">
        {b.instructions.length === 0 ? <span className="text-fg-mute">(empty)</span> : b.instructions.map((i) => (
          <div key={i.index} className="whitespace-nowrap overflow-hidden text-ellipsis" title={`line ${i.line}`}>
            <span className="text-ink-500 mr-1.5 select-none">{i.line || '·'}</span>{i.text}
          </div>))}
      </div>
      <Handle type="source" position={Position.Bottom} style={{ opacity: 0 }} />
    </div>
  )
})
const nodeTypes = { block: BlockNode }
const EDGE_COLOR: Record<string, string> = { true: '#3fb68b', false: '#f2555a', jump: '#7b93ff', fall: '#667085' }

export default function CfgGraph({ fn, selected, onSelect }: { fn: FunctionAnalysis; selected?: number; onSelect: (b: CfgBlock) => void }) {
  const { nodes, edges } = useMemo(() => {
    const pos = layoutCfg(fn.blocks, fn.edges)
    const nodes: Node<BlockData>[] = pos.map((p) => ({
      id: String(p.id), type: 'block', position: { x: p.x, y: p.y }, draggable: false,
      data: { block: fn.blocks[p.id], selected: p.id === selected, w: p.w, h: p.h },
    }))
    const edges: Edge[] = fn.edges.map((e, i) => ({
      id: `e${i}`, source: String(e.from), target: String(e.to), type: 'smoothstep',
      label: e.kind === 'fall' || e.kind === 'jump' ? (e.back ? 'back edge' : undefined) : e.kind + (e.back ? ' · back' : ''),
      labelStyle: { fill: '#98a2b3', fontSize: 10 }, labelBgStyle: { fill: '#0f1217' },
      style: { stroke: e.back ? '#e5a93a' : EDGE_COLOR[e.kind], strokeWidth: e.back ? 2 : 1.4, strokeDasharray: e.back ? '6 4' : undefined },
      markerEnd: { type: MarkerType.ArrowClosed, color: e.back ? '#e5a93a' : EDGE_COLOR[e.kind] }, animated: e.back,
    }))
    return { nodes, edges }
  }, [fn, selected])
  return (
    <ReactFlow key={fn.name + fn.blocks.length} nodes={nodes} edges={edges} nodeTypes={nodeTypes} onNodeClick={(_, n) => onSelect((n.data as BlockData).block)}
      fitView fitViewOptions={{ padding: 0.12, maxZoom: 1, minZoom: 0.5 }} minZoom={0.15} nodesConnectable={false} proOptions={{ hideAttribution: true }}>
      <Background color="#1d232d" gap={22} /><Controls showInteractive={false} />
    </ReactFlow>
  )
}
