# Assistant: verified quick fixes, auto-fix, runtime analysis, optional Claude

Principle: **the compiler decides what is correct, never a model.**

| Feature | Endpoint | Needs API key | How it works |
|---|---|---|---|
| Quick fixes | `POST /api/suggest` | no | Rules map a diagnostic to a text edit (insert `;`, rename to the suggested identifier, wrap in `to_int/to_float`, add `return`, remove unused variable, …). Each edit is applied and the result **re-compiled**; the fix is `verified` only if the targeted diagnostic disappears without adding errors. |
| Auto-fix | `POST /api/autofix` | no | Repeats: first error → first verified fix → re-compile, up to 12 steps. Returns the steps, the diff and whether the program now compiles (`fixed` / `partial` / `no_automatic_fix`). |
| Runtime analysis | `POST /api/debug/explain` | no | Runs the VM with tracing; derives the root cause from the recorded failing state (source line, variables, last steps) with per-trap rules: division/modulo by zero (identifies the divisor), index out of bounds, negative array size, stack overflow (names the recursing function), step limit. |
| Claude fix | `POST /api/ai/fix` | yes | Model proposes a corrected program; the compiler re-compiles it; only a candidate that compiles is returned (3 attempts, each retry gets the new diagnostics). |
| Claude debug | `POST /api/ai/debug` | yes | Same evidence as runtime analysis, free-form explanation. |
| Editor | LSP `textDocument/codeAction` | no | The same rules as quick-fix code actions with a `WorkspaceEdit`. |

Tests: `backend/tests/test_quickfix.py` (rules, verification flags, auto-fix on the shipped error examples, runtime analysis of the trap examples, LSP code action) and `backend/tests/test_ai_assist.py` (scripted fake model: verified fix, retry with feedback, unverified fix never returned).

Honest limits: rules cover the common diagnostics only (wrong argument counts, duplicate declarations need a human); the Claude path has been exercised only against a scripted model in tests.
