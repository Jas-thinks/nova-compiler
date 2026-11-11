import { memo, useMemo, useState } from 'react'
import { Background, Controls, Handle, Position, ReactFlow, type Edge, type Node, type NodeProps } from '@xyflow/react'
import '@xyflow/react/dist/style.css'
import { ChevronDown, ChevronRight } from 'lucide-react'
import type { AstNode } from '../../types/api'
import { kindColor, layoutTree, NODE_H, NODE_W } from './layout'

interface NodeData extends Record<string, unknown> {
  node: AstNode
  collapsed: boolean
  hasChildren: boolean
  selected: boolean
  onToggle: (id: number) => void
}

const AstNodeView = memo(function AstNodeView({ data }: NodeProps<Node<NodeData>>) {
  const { node, collapsed, hasChildren, selected, onToggle } = data
  const color = kindColor(node.kind)
  return (
    <div
      style={{ width: NODE_W, height: NODE_H, borderColor: selected ? '#9db0ff' : '#2d3644', boxShadow: selected ? '0 0 0 1px #9db0ff' : undefined }}
      className="rounded-md bg-ink-850 border px-2 py-1 flex items-center gap-1.5 cursor-pointer"
    >
      <Handle type="target" position={Position.Top} style={{ opacity: 0 }} />
      <span style={{ background: color }} className="w-1.5 self-stretch rounded-full shrink-0" />
      <div className="min-w-0 flex-1">
        <div className="truncate text-[12px] font-medium" title={node.label}>{node.label}</div>
        <div className="text-[10px] text-fg-mute code truncate">
          {node.type ? `${node.type} · ` : ''}{node.loc.line}:{node.loc.col}
        </div>
      </div>
      {hasChildren && (
        <button
          className="text-fg-dim hover:text-fg shrink-0 nodrag"
          title={collapsed ? 'Expand' : 'Collapse'}
          onClick={(e) => { e.stopPropagation(); onToggle(node.id) }}
        >
          {collapsed ? <ChevronRight size={14} /> : <ChevronDown size={14} />}
        </button>
      )}
      <Handle type="source" position={Position.Bottom} style={{ opacity: 0 }} />
    </div>
  )
})

const nodeTypes = { ast: AstNodeView }

export default function AstTree({ root, selectedId, onSelect }: { root: AstNode; selectedId?: number; onSelect: (n: AstNode) => void }) {
  const [collapsed, setCollapsed] = useState<Set<number>>(new Set())
  const toggle = (id: number) => setCollapsed((s) => { const n = new Set(s); if (n.has(id)) n.delete(id); else n.add(id); return n })

  const { nodes, edges } = useMemo(() => {
    const pos = layoutTree(root, collapsed)
    const nodes: Node<NodeData>[] = pos.map((p) => ({
      id: String(p.id), type: 'ast', position: { x: p.x, y: p.y }, draggable: false,
      data: { node: p.node, collapsed: p.collapsed, hasChildren: p.hasChildren, selected: p.id === selectedId, onToggle: toggle },
    }))
    const edges: Edge[] = pos.filter((p) => p.parent !== undefined).map((p) => ({
      id: `e${p.parent}-${p.id}`, source: String(p.parent), target: String(p.id), type: 'smoothstep', style: { stroke: '#46536a' },
    }))
    return { nodes, edges }
  }, [root, collapsed, selectedId])

  return (
    <ReactFlow
      key={collapsed.size + ':' + root.id}
      nodes={nodes} edges={edges} nodeTypes={nodeTypes}
      onNodeClick={(_, n) => onSelect((n.data as NodeData).node)}
      fitView fitViewOptions={{ padding: 0.1, maxZoom: 1, minZoom: 0.55 }} minZoom={0.1}
      nodesConnectable={false} proOptions={{ hideAttribution: true }}
    >
      <Background color="#1d232d" gap={22} />
      <Controls showInteractive={false} />
    </ReactFlow>
  )
}

export function AstOutline({ node, depth = 0, onSelect, selectedId }: { node: AstNode; depth?: number; onSelect: (n: AstNode) => void; selectedId?: number }) {
  return (
    <div>
      <div
        onClick={() => onSelect(node)}
        style={{ paddingLeft: depth * 14 + 8 }}
        className={`code py-[1px] cursor-pointer hover:bg-ink-800 whitespace-nowrap ${selectedId === node.id ? 'bg-nova/15' : ''}`}
      >
        <span style={{ color: kindColor(node.kind) }}>●</span> {node.label}
        {node.type && <span className="text-fg-mute"> : {node.type}</span>}
        <span className="text-ink-500"> {node.loc.line}:{node.loc.col}</span>
      </div>
      {node.children.map((c) => <AstOutline key={c.id} node={c} depth={depth + 1} onSelect={onSelect} selectedId={selectedId} />)}
    </div>
  )
}
