import { useEffect, useRef } from 'react'
import Editor, { type OnMount } from '@monaco-editor/react'
import '../lib/monaco'
import { monaco, registerNova } from '../lib/monaco'
import type { Diagnostic } from '../types/api'
import { diagnosticToMarker } from '../lib/format'

export interface Highlight { line: number; col?: number; endLine?: number; endCol?: number; className: string }

interface Props {
  value: string
  onChange?: (v: string) => void
  readOnly?: boolean
  diagnostics?: Diagnostic[]
  highlights?: Highlight[]
  breakpoints?: number[]
  onToggleBreakpoint?: (line: number) => void
  revealLine?: number
  fontSize?: number
  glyphMargin?: boolean
}

export default function CodeEditor({ value, onChange, readOnly, diagnostics, highlights, breakpoints, onToggleBreakpoint, revealLine, fontSize = 13, glyphMargin }: Props) {
  const ed = useRef<monaco.editor.IStandaloneCodeEditor | null>(null)
  const decos = useRef<monaco.editor.IEditorDecorationsCollection | null>(null)
  const toggle = useRef(onToggleBreakpoint)
  toggle.current = onToggleBreakpoint

  const onMount: OnMount = (editor) => {
    ed.current = editor
    decos.current = editor.createDecorationsCollection([])
    editor.onMouseDown((e) => {
      const t = e.target.type
      if (toggle.current && e.target.position && (t === monaco.editor.MouseTargetType.GUTTER_GLYPH_MARGIN || t === monaco.editor.MouseTargetType.GUTTER_LINE_NUMBERS)) {
        toggle.current(e.target.position.lineNumber)
      }
    })
    applyAll()
  }

  const applyAll = () => {
    const editor = ed.current
    const model = editor?.getModel()
    if (!editor || !model) return
    monaco.editor.setModelMarkers(model, 'nova', (diagnostics ?? []).map(diagnosticToMarker))
    const d: monaco.editor.IModelDeltaDecoration[] = []
    for (const h of highlights ?? []) {
      if (h.line < 1 || h.line > model.getLineCount()) continue
      const whole = h.col === undefined
      d.push({
        range: new monaco.Range(h.line, whole ? 1 : h.col!, h.endLine ?? h.line, whole ? model.getLineMaxColumn(h.line) : h.endCol ?? h.col! + 1),
        options: { isWholeLine: whole, className: h.className },
      })
    }
    for (const b of breakpoints ?? []) {
      if (b >= 1 && b <= model.getLineCount()) d.push({ range: new monaco.Range(b, 1, b, 1), options: { glyphMarginClassName: 'monaco-bp' } })
    }
    decos.current?.set(d)
  }

  useEffect(applyAll, [diagnostics, highlights, breakpoints, value]) // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    if (revealLine && ed.current) ed.current.revealLineInCenterIfOutsideViewport(revealLine)
  }, [revealLine])

  return (
    <Editor
      value={value}
      language="nova"
      theme="nova-dark"
      beforeMount={registerNova}
      onMount={onMount}
      onChange={(v) => onChange?.(v ?? '')}
      options={{
        readOnly,
        fontSize,
        fontFamily: '"JetBrains Mono", "Fira Code", ui-monospace, Menlo, Consolas, monospace',
        fontLigatures: false,
        minimap: { enabled: false },
        scrollBeyondLastLine: false,
        renderLineHighlight: 'line',
        glyphMargin: glyphMargin ?? Boolean(onToggleBreakpoint),
        lineNumbersMinChars: 3,
        padding: { top: 10, bottom: 10 },
        automaticLayout: true,
        tabSize: 4,
        smoothScrolling: true,
        contextmenu: false,
        overviewRulerLanes: 0,
        scrollbar: { verticalScrollbarSize: 10, horizontalScrollbarSize: 10 },
      }}
      loading={<div className="p-4 text-fg-mute">Loading editor…</div>}
    />
  )
}
