// Minimal LSP client: launches scripts/nova-lsp.sh (stdio) and attaches it to .nova files.
const vscode = require('vscode')
const { LanguageClient } = require('vscode-languageclient/node')
let client

exports.activate = (context) => {
  const script = vscode.workspace.getConfiguration('nova').get('serverScript')
  if (!script) { vscode.window.showWarningMessage('NOVA: set "nova.serverScript" to the absolute path of scripts/nova-lsp.sh'); return }
  const server = { command: script, args: [] }
  client = new LanguageClient('nova', 'NOVA Language Server', { run: server, debug: server }, { documentSelector: [{ language: 'nova' }] })
  client.start()
  context.subscriptions.push({ dispose: () => client && client.stop() })
}
exports.deactivate = () => (client ? client.stop() : undefined)
