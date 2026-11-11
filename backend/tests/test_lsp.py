"""LSP server tests: protocol logic via handle(), plus one real stdio round-trip with Content-Length framing."""
import io
import json

from app import lsp

URI = "file:///t.nova"
SRC = (
    "fn add(a: int, b: int) -> int {\n"      # line 0
    "    let sum: int = a + b;\n"             # line 1
    "    return sum;\n"                       # line 2
    "}\n"                                     # line 3
    "fn main() {\n"                           # line 4
    "    let x: int = add(1, 2);\n"           # line 5
    "    print(x);\n"                         # line 6
    "}\n"
)


def open_doc(text=SRC):
    s = lsp.NovaLanguageServer()
    out = s.handle({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {"uri": URI, "version": 1, "text": text}}})
    return s, out


def req(s, method, line, ch, **extra):
    r = s.handle({"jsonrpc": "2.0", "id": 1, "method": method, "params": {"textDocument": {"uri": URI}, "position": {"line": line, "character": ch}, **extra}})
    return r[0]["result"]


def test_initialize_advertises_capabilities():
    s = lsp.NovaLanguageServer()
    caps = s.handle({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})[0]["result"]["capabilities"]
    assert caps["hoverProvider"] and caps["definitionProvider"] and caps["referencesProvider"] and caps["textDocumentSync"] == 1


def test_clean_program_publishes_no_errors():
    _, out = open_doc()
    assert out[0]["method"] == "textDocument/publishDiagnostics"
    assert [d for d in out[0]["params"]["diagnostics"] if d["severity"] == 1] == []


def test_type_error_diagnostic_has_precise_range():
    bad = SRC.replace("let sum: int = a + b;", "let sum: int = a + 1.5;")
    _, out = open_doc(bad)
    d = out[0]["params"]["diagnostics"][0]
    assert d["severity"] == 1 and d["source"] == "novac" and d["code"].startswith("E-SEM")
    assert d["range"]["start"]["line"] == 1          # 0-based line of the bad expression
    assert d["range"]["end"]["character"] > d["range"]["start"]["character"]


def test_syntax_error_reported_then_fixed_clears():
    s, out = open_doc("fn main() {\n    let x: int = ;\n}\n")
    assert out[0]["params"]["diagnostics"], "syntax error must be reported"
    out = s.handle({"jsonrpc": "2.0", "method": "textDocument/didChange", "params": {"textDocument": {"uri": URI, "version": 2}, "contentChanges": [{"text": "fn main() {\n    let x: int = 1;\n    print(x);\n}\n"}]}})
    assert [d for d in out[0]["params"]["diagnostics"] if d["severity"] == 1] == []


def test_hover_on_variable_and_function():
    s, _ = open_doc()
    h = req(s, "textDocument/hover", 2, 11)               # `sum` in `return sum;`
    assert "sum: int" in h["contents"]["value"]
    h = req(s, "textDocument/hover", 5, 17)               # `add` in the call
    assert "fn add(a: int, b: int) -> int" in h["contents"]["value"]
    assert req(s, "textDocument/hover", 3, 0) is None


def test_go_to_definition_variable_and_function():
    s, _ = open_doc()
    loc = req(s, "textDocument/definition", 2, 11)        # use of `sum` -> declaration on line 1
    assert loc["range"]["start"]["line"] == 1 and loc["uri"] == URI
    loc = req(s, "textDocument/definition", 5, 17)        # call of add -> `fn add` on line 0
    assert loc["range"]["start"]["line"] == 0


def test_references_include_declaration():
    s, _ = open_doc()
    refs = req(s, "textDocument/references", 1, 8, context={"includeDeclaration": True})   # `sum` declaration
    assert sorted(r["range"]["start"]["line"] for r in refs) == [1, 2]
    refs = req(s, "textDocument/references", 1, 8, context={"includeDeclaration": False})
    assert [r["range"]["start"]["line"] for r in refs] == [2]


def test_completion_offers_keywords_functions_and_scoped_variables():
    s, _ = open_doc()
    labels = {i["label"] for i in req(s, "textDocument/completion", 6, 4)["items"]}
    assert {"while", "int", "string", "add", "len", "to_int", "x"} <= labels
    assert "sum" not in labels                            # `sum` is local to add()


def test_document_symbols_list_user_functions():
    s, _ = open_doc()
    syms = s.handle({"jsonrpc": "2.0", "id": 2, "method": "textDocument/documentSymbol", "params": {"textDocument": {"uri": URI}}})[0]["result"]
    assert [x["name"] for x in syms] == ["add", "main"]


def test_unknown_request_returns_method_not_found_and_notifications_are_ignored():
    s = lsp.NovaLanguageServer()
    assert s.handle({"jsonrpc": "2.0", "id": 9, "method": "foo/bar"})[0]["error"]["code"] == -32601
    assert s.handle({"jsonrpc": "2.0", "method": "$/cancelRequest", "params": {}}) == []


def test_stdio_framing_round_trip():
    msgs = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {"uri": URI, "version": 1, "text": SRC}}},
        {"jsonrpc": "2.0", "id": 2, "method": "shutdown"},
        {"jsonrpc": "2.0", "method": "exit"},
    ]
    inp = io.BytesIO()
    for m in msgs:
        lsp.write_message(inp, m)
    inp.seek(0)
    out = io.BytesIO()
    assert lsp.serve(inp, out) == 0
    out.seek(0)
    got = []
    while (m := lsp.read_message(out)) is not None:
        got.append(m)
    assert got[0]["result"]["serverInfo"]["name"] == "nova-lsp"
    assert any(m.get("method") == "textDocument/publishDiagnostics" for m in got)
    assert any(m.get("id") == 2 and m["result"] is None for m in got)
