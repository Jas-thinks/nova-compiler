// Bundles Monaco locally (no CDN) and registers the NOVA language + dark theme.
import * as monaco from 'monaco-editor/esm/vs/editor/editor.api'
import 'monaco-editor/esm/vs/editor/editor.all'
import EditorWorker from 'monaco-editor/esm/vs/editor/editor.worker?worker'
import { loader } from '@monaco-editor/react'

;(self as unknown as { MonacoEnvironment: unknown }).MonacoEnvironment = { getWorker: () => new EditorWorker() }
loader.config({ monaco })

let registered = false
export function registerNova() {
  if (registered) return
  registered = true
  monaco.languages.register({ id: 'nova' })
  monaco.languages.setLanguageConfiguration('nova', {
    comments: { lineComment: '//', blockComment: ['/*', '*/'] },
    brackets: [['{', '}'], ['(', ')']],
    autoClosingPairs: [{ open: '{', close: '}' }, { open: '(', close: ')' }],
    surroundingPairs: [{ open: '{', close: '}' }, { open: '(', close: ')' }],
    indentationRules: { increaseIndentPattern: /\{[^}]*$/, decreaseIndentPattern: /^\s*\}/ },
  })
  monaco.languages.setMonarchTokensProvider('nova', {
    keywords: ['fn', 'let', 'if', 'else', 'while', 'return', 'print', 'new'],
    types: ['int', 'float', 'bool', 'string'],
    builtins: ['to_float', 'to_int', 'len'],
    tokenizer: {
      root: [
        [/\/\/.*$/, 'comment'],
        [/\/\*/, 'comment', '@comment'],
        [/[a-zA-Z_]\w*/, { cases: { '@keywords': 'keyword', '@types': 'type', '@builtins': 'support.function', 'true|false': 'constant', '@default': 'identifier' } }],
        [/"(?:[^"\\]|\\.)*"/, 'string'],
        [/\d+\.\d+/, 'number.float'],
        [/\d+/, 'number'],
        [/->|==|!=|<=|>=|&&|\|\|/, 'operator'],
        [/[+\-*/%<>=!]/, 'operator'],
        [/[{}()\[\]]/, 'delimiter.bracket'],
        [/[;,:]/, 'delimiter'],
      ],
      comment: [[/[^/*]+/, 'comment'], [/\*\//, 'comment', '@pop'], [/[/*]/, 'comment']],
    },
  } as monaco.languages.IMonarchLanguage)
  monaco.editor.defineTheme('nova-dark', {
    base: 'vs-dark',
    inherit: true,
    rules: [
      { token: 'keyword', foreground: 'c792ea' },
      { token: 'type', foreground: '58b8e6' },
      { token: 'support.function', foreground: '82aaff' },
      { token: 'constant', foreground: 'f78c6c' },
      { token: 'number', foreground: 'f78c6c' },
      { token: 'number.float', foreground: 'f78c6c' },
      { token: 'comment', foreground: '5c6a80', fontStyle: 'italic' },
      { token: 'operator', foreground: '89ddff' },
      { token: 'identifier', foreground: 'd5dbe6' },
    ],
    colors: {
      'editor.background': '#0f1217',
      'editor.foreground': '#d5dbe6',
      'editorLineNumber.foreground': '#46536a',
      'editorLineNumber.activeForeground': '#98a2b3',
      'editor.lineHighlightBackground': '#181d2500',
      'editor.lineHighlightBorder': '#222934',
      'editorGutter.background': '#0f1217',
      'editor.selectionBackground': '#7b93ff44',
      'editorCursor.foreground': '#9db0ff',
      'editorWidget.background': '#13171d',
      'editorIndentGuide.background1': '#1a2029',
    },
  })
}

export { monaco }
