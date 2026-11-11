# REST API

Base URL in development: `http://127.0.0.1:8000` (the dev server proxies `/api` from the website). Interactive docs: `/docs` (OpenAPI). All bodies are JSON. Validation failures return `422 {"error":"invalid_request","detail":[{"field","message"}]}`; compiler failures return `{"error":"compiler_error","detail":…}` with 408 (timeout) / 503 (binary missing) / 500.

Common request fields: `source` (string ≤ 65,536 bytes, no NUL), `passes` (`null` = all passes, `[]` = none, or any of `constant_propagation constant_folding algebraic cse unreachable dce`).

| Method & path | Body | Returns |
|---|---|---|
| `GET /api/health` | | `status`, `compiler_found`, `llm_available`, limits, parser summary (`algorithm`, `states`, `conflicts`) |
| `GET /api/examples` | | `{examples:[{id,title,description,category,source}]}` |
| `GET /api/passes` · `GET /api/isa` | | pass list · VM instruction list |
| `GET /api/grammar` | | productions, FIRST/FOLLOW, all LR(1) states with kernel + ACTION/GOTO rows |
| `GET /api/grammar/state/{n}` | | kernel and full closure of state `n` |
| `POST /api/compile` | `source, passes?, run?, max_steps?` | **Compilation response** (below) |
| `POST /api/execute` | `source, passes?, max_steps?` | `success, failedStage, diagnostics, stages, output[], error, execution` |
| `POST /api/optimize` | `source, passes?` | `config, ir{original,optimized}, optimization{history,perPass,…}, execution, metrics, bytecode` |
| `POST /api/debug/start` | `source, passes?, target ("optimized"\|"baseline"), breakpoints[] (lines), max_steps?` | `{success, bytecode, state}` or compile diagnostics |
| `POST /api/debug/step` | `session_id, direction ("forward"\|"back"), mode ("instruction"\|"line"\|"over"), count` | debug state |
| `POST /api/debug/continue` | `session_id, breakpoints?, breakpoint_addrs?` | state; `reason` = `breakpoint` \| `finished` |
| `POST /api/debug/breakpoints` · `/seek` · `/reset` | `session_id` + `breakpoints[]` / `cursor` / – | debug state |
| `POST /api/tests/run` | `fuzz_count (≤2000), seed, passes?, unit_tests` | `{unit:{passed,failed,total,tests[]}, differential:{…}}` — produced by actually running the C++ binaries |
| `GET /api/capabilities` | | features discovered by compiling a probe program (stages, analyses, parser stats, passes, language adapters, toolchains) |
| `GET /api/tests/last` | | the last `tests/run` result recorded by this server, with timestamp (or `{ran:false}`) |
| `GET /api/explorer/languages` · `POST /api/explorer/c` | `source (≤16 KiB), opt_level 0..3` | adapter list with honest statuses · real gcc/clang output (`outputs{gimple,cfg,assembly}` or `{ast,llvm-ir,assembly}`); 400 for disallowed includes, 503 if no toolchain |
| `POST /api/explain` | `source, diagnostic` | optional LLM rephrasing of an *existing* diagnostic; `{available:false}` without `ANTHROPIC_API_KEY` |

## Compilation response
```jsonc
{
  "success": true, "failedStage": "",
  "stages": [{"id":"lexer","name":"Lexical Analysis","status":"ok","durationMs":0.03,"summary":{"tokens":42}}, …],
  "diagnostics": [{"severity","phase","category","code","message","explanation","suggestion","loc":{"line","col","endLine","endCol"}}],
  "tokens": [{"category","lexeme","terminal","loc","offset"}],
  "parser": {"algorithm":"Canonical LR(1)","states":241,"totalSteps","shifts","reduces","trace":[{"step","stack","lookahead","input","action","prod","detail"}],"tree":{…}},
  "ast": {"id","kind","label","type?","loc","children":[…]},
  "semantic": {"scopes":[{"id","parent","kind","owner","symbols":[…]}],"functions":[…]},
  "ir": {"original":{functions:[{name,instructions:[{index,text,kind,explanation,line,…}]}]}, "optimized":{…}},
  "analysis": {"original":[{"name","blocks[{id,labels,instructions,succ,pred,idom,reachable,loopHeader}]","edges[{from,to,kind,back}]","liveness{use,def,in,out,history[],iterations,converged}","reachingDefinitions{gen,kill,in,out,history[],definitions}","ssa{blocks[{phis,code}],phiCount}","summary"}], "optimized":[…]},
  "optimization": {"history":[{"pass","function","before","after","explanation","line","round"}],"perPass","instructionsBefore","instructionsAfter","passes":[…],"config":{…}},
  "bytecode": {"optimized":{functions,instructions:[{addr,op,text,comment,description,line}]}, "baseline":{…}},
  "execution": {"baseline":{ok,output,steps,error?,opCounts}, "optimized":{…}, "reference":{…}, "optimizationPreservesBehavior", "matchesReference"}  // when run=true
}
```
Fields for stages after a failure are omitted; later `stages[*].status` is `skipped`.

## Debug state
`session_id, cursor, trace_length, step, ip, instruction{addr,text,comment,line,description,function}, event, frames[{depth,function,callLine,locals[{slot,name,type,value,temp,param}],stack[]}], output[], finished, error, history[], breakpoints[], reason`. Sessions expire after 30 minutes (max 25 kept).
