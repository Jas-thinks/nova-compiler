import { useState } from 'react'
import Gate from '../components/Gate'
import { Chip, DiagnosticList, PageHeader, Panel } from '../components/ui'
import { ScopeTree, SymbolTable } from '../features/semantic/SymbolTables'
import type { ScopeInfo } from '../types/api'

export default function Semantic() {
  const [sel, setSel] = useState<ScopeInfo | null>(null)
  return (
    <div>
      <PageHeader title="Semantic Analysis" subtitle="Name resolution and static type checking over the AST. Scopes form a tree; each declaration is recorded in the symbol table of its scope, and every read and write is tracked." />
      <Gate need={(r) => r.semantic}>
        {(r) => {
          const sem = r.semantic!
          const scope = sel ?? sem.scopes[Math.min(1, sem.scopes.length - 1)]
          const semDiag = r.diagnostics.filter((d) => d.phase === 'semantic')
          return (
            <div className="grid grid-cols-[320px_1fr] gap-3">
              <div className="flex flex-col gap-3">
                <Panel title="Scope hierarchy" bodyClass="py-1"><ScopeTree scopes={sem.scopes} selected={scope.id} onSelect={setSel} /></Panel>
                <Panel title="Functions" bodyClass="p-0">
                  <table className="w-full code"><thead><tr><th className="th">Signature</th><th className="th">Calls</th></tr></thead><tbody>
                    {sem.functions.map((f) => <tr key={f.name}><td className="td">{f.name}({f.params.map((p) => `${p.name}: ${p.type}`).join(', ')}) → {f.returnType}{f.builtin && <Chip tone="nova"> built-in</Chip>}</td><td className="td">{f.calls}</td></tr>)}
                  </tbody></table>
                </Panel>
              </div>
              <div className="flex flex-col gap-3 min-w-0">
                <Panel title={`Symbol table · scope #${scope.id} (${scope.kind}${scope.owner ? ' of ' + scope.owner : ''})`}><SymbolTable scope={scope} /></Panel>
                <Panel title={`Semantic diagnostics (${semDiag.length})`}><DiagnosticList items={semDiag} /></Panel>
                <Panel title="Checks performed" bodyClass="p-3 text-fg-dim">
                  <ul className="list-disc pl-5 space-y-0.5"><li>declaration before use, duplicate declarations per scope, shadowing in inner scopes</li><li>operand / assignment / condition / return / argument type agreement (no implicit conversions)</li><li>function existence, argument count and types, void results not used as values</li><li>all-paths-return analysis for non-void functions; <code>main()</code> entry point</li><li>warnings: unused variables, unreachable statements</li></ul>
                </Panel>
              </div>
            </div>
          )
        }}
      </Gate>
    </div>
  )
}
