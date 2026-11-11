import type { ReactNode } from 'react'
import { FileCode2, Play, ShieldAlert } from 'lucide-react'
import { useNova } from '../state/NovaContext'
import type { CompileResponse } from '../types/api'
import { DiagnosticList, Empty } from './ui'

/** Renders children only when a compile result exists and the required artifact was produced. */
export default function Gate({ need, children }: { need?: (r: CompileResponse) => unknown; children: (r: CompileResponse) => ReactNode }) {
  const { result, compile, compiling } = useNova()
  if (!result) {
    return (
      <Empty
        icon={<FileCode2 size={28} />}
        title="Nothing compiled yet"
        hint="Every view shows real output from the C++ compiler. Compile the program in the editor to populate this page."
      />
    )
  }
  if (need && !need(result)) {
    const failed = result.stages.find((s) => s.status === 'error')
    return (
      <div className="max-w-3xl">
        <div className="panel p-4 mb-3 flex gap-3">
          <ShieldAlert className="text-bad shrink-0" size={20} />
          <div>
            <div className="font-medium">This stage did not run</div>
            <p className="text-fg-dim mt-1">
              Compilation stopped at <b>{failed?.name ?? result.failedStage}</b>, so later stages were skipped. Fix the errors below and compile again.
            </p>
            <button className="btn btn-primary mt-3" onClick={() => void compile()} disabled={compiling}><Play size={14} /> Recompile</button>
          </div>
        </div>
        <div className="panel"><DiagnosticList items={result.diagnostics} /></div>
      </div>
    )
  }
  return <>{children(result)}</>
}
