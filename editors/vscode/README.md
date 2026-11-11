# NOVA for VS Code
1. `./scripts/dev.sh` once (creates the backend venv and builds the compiler).
2. `cd editors/vscode && npm install`, then open this folder in VS Code and press F5 (Extension Development Host).
3. In the host window set `nova.serverScript` to the absolute path of `scripts/nova-lsp.sh`, open any `.nova` file.

The server itself (`backend/app/lsp.py`) is editor-agnostic and covered by `backend/tests/test_lsp.py`; this thin client has **not** been exercised in a real VS Code session yet.
