#!/usr/bin/env bash
# NOVA language server on stdio (for editors). Requires the backend venv: ./scripts/build.sh or ./scripts/dev.sh once.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$here/backend" && exec .venv/bin/python -m app.lsp
