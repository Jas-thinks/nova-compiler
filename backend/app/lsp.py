"""NOVA language server (LSP over stdio): diagnostics, hover, go-to-definition, references, completion, document symbols.

Everything is derived from the real C++ compiler's output (diagnostics + semantic symbol tables); there is no second
parser. The protocol logic lives in `NovaLanguageServer.handle()` (pure: message in, outgoing messages out) so it is
unit-testable; `serve()` adds the Content-Length framing for stdio.
"""
from __future__ import annotations

import json
import sys
from typing import Any, BinaryIO

from . import compiler_client as cc
from . import quickfix

KEYWORDS = ["fn", "let", "if", "else", "while", "return", "print", "new", "true", "false"]
TYPES = ["int", "float", "bool", "string"]
BUILTINS = {"to_float": "to_float(x: int) -> float", "to_int": "to_int(x: float) -> int", "len": "len(x: string | T[]) -> int"}

# LSP kinds
SEV = {"error": 1, "warning": 2}
KIND_FUNCTION, KIND_VARIABLE, KIND_KEYWORD, KIND_TYPE = 3, 6, 14, 25


def _range(loc: dict) -> dict:
    """Compiler locations are 1-based with an exclusive end column; LSP is 0-based."""
    return {
        "start": {"line": loc["line"] - 1, "character": loc["col"] - 1},
        "end": {"line": loc.get("endLine", loc["line"]) - 1, "character": loc.get("endCol", loc["col"] + 1) - 1},
    }


def _contains(loc: dict, line: int, ch: int) -> bool:
    """Is the 0-based LSP position inside the 1-based compiler range?"""
    r = _range(loc)
    p = (line, ch)
    return (r["start"]["line"], r["start"]["character"]) <= p < (r["end"]["line"], r["end"]["character"])


class Document:
    def __init__(self, text: str, version: int = 0):
        self.text = text
        self.version = version
        self.result: dict = {}
        self.sem_ok = False          # semantic tables available (compile got past the parser)

    def analyze(self) -> None:
        try:
            self.result = cc.compile_source(self.text, run=False)
        except cc.CompilerError as exc:
            self.result = {"diagnostics": [{"severity": "error", "message": str(exc), "loc": {"line": 1, "col": 1, "endLine": 1, "endCol": 2}}]}
        self.sem_ok = bool(self.result.get("semantic"))

    # -- symbol queries over the semantic JSON -------------------------------------------------
    def _symbols(self):
        for sc in (self.result.get("semantic") or {}).get("scopes", []):
            for s in sc["symbols"]:
                yield sc, s

    def symbol_at(self, line: int, ch: int) -> dict | None:
        for _, s in self._symbols():
            for loc in [s["declLoc"], *s["reads"], *s["writes"]]:
                if _contains(loc, line, ch):
                    return s
        return None

    def function_at(self, line: int, ch: int) -> dict | None:
        """A call site or definition name: the word under the cursor matching a declared function."""
        word = self.word_at(line, ch)
        for f in (self.result.get("semantic") or {}).get("functions", []):
            if f["name"] == word:
                return f
        return None

    def word_at(self, line: int, ch: int) -> str:
        lines = self.text.split("\n")
        if line >= len(lines):
            return ""
        s = lines[line]
        a = b = min(ch, len(s))
        while a > 0 and (s[a - 1].isalnum() or s[a - 1] == "_"):
            a -= 1
        while b < len(s) and (s[b].isalnum() or s[b] == "_"):
            b += 1
        return s[a:b]

    def visible_symbols(self, line: int) -> list[dict]:
        """Variables whose scope encloses the 1-based line (innermost first), plus all of that function's names."""
        out = []
        for sc, s in self._symbols():
            loc = sc["loc"]
            if loc["line"] <= line + 1 <= loc.get("endLine", loc["line"]) and s["declLoc"]["line"] <= line + 1:
                out.append(s)
        return out


def signature(f: dict) -> str:
    ps = ", ".join(f"{p['name']}: {p['type']}" for p in f["params"])
    ret = "" if f["returnType"] == "void" else f" -> {f['returnType']}"
    return f"fn {f['name']}({ps}){ret}"


class NovaLanguageServer:
    def __init__(self) -> None:
        self.docs: dict[str, Document] = {}
        self.shutdown_requested = False
        self.exited = False

    # ------------------------------------------------------------------ dispatch
    def handle(self, msg: dict) -> list[dict]:
        method = msg.get("method")
        mid = msg.get("id")
        params = msg.get("params") or {}
        fn = getattr(self, "_m_" + (method or "").replace("/", "_").replace("$", "_"), None)
        if fn is None:
            if mid is not None:   # unknown request: MethodNotFound; unknown notifications are ignored per spec
                return [{"jsonrpc": "2.0", "id": mid, "error": {"code": -32601, "message": f"method not supported: {method}"}}]
            return []
        out = fn(params)
        if mid is None:
            return list(out or [])
        return [{"jsonrpc": "2.0", "id": mid, "result": out}]

    # ------------------------------------------------------------------ lifecycle
    def _m_initialize(self, p):
        return {
            "capabilities": {
                "textDocumentSync": 1,   # full document sync
                "hoverProvider": True,
                "definitionProvider": True,
                "referencesProvider": True,
                "documentSymbolProvider": True,
                "codeActionProvider": {"codeActionKinds": ["quickfix"]},
                "completionProvider": {"triggerCharacters": []},
            },
            "serverInfo": {"name": "nova-lsp", "version": "0.1"},
        }

    def _m_initialized(self, p):
        return []

    def _m_shutdown(self, p):
        self.shutdown_requested = True
        return None

    def _m_exit(self, p):
        self.exited = True
        return []

    # ------------------------------------------------------------------ documents
    def _publish(self, uri: str) -> list[dict]:
        d = self.docs[uri]
        diags = []
        for x in d.result.get("diagnostics", []):
            loc = x.get("loc") or {"line": 1, "col": 1}
            diags.append({
                "range": _range(loc), "severity": SEV.get(x.get("severity"), 1), "code": x.get("code"), "source": "novac",
                "message": x["message"] + (f"\n{x['suggestion']}" if x.get("suggestion") else ""),
            })
        return [{"jsonrpc": "2.0", "method": "textDocument/publishDiagnostics", "params": {"uri": uri, "version": d.version, "diagnostics": diags}}]

    def _update(self, uri: str, text: str, version: int) -> list[dict]:
        d = Document(text, version)
        d.analyze()
        old = self.docs.get(uri)
        if not d.sem_ok and old is not None and old.sem_ok:   # keep last good symbol tables for completion while the code is mid-edit
            d.result["semantic"] = old.result.get("semantic")
        self.docs[uri] = d
        return self._publish(uri)

    def _m_textDocument_didOpen(self, p):
        t = p["textDocument"]
        return self._update(t["uri"], t["text"], t.get("version", 0))

    def _m_textDocument_didChange(self, p):
        t = p["textDocument"]
        return self._update(t["uri"], p["contentChanges"][-1]["text"], t.get("version", 0))

    def _m_textDocument_didClose(self, p):
        uri = p["textDocument"]["uri"]
        self.docs.pop(uri, None)
        return [{"jsonrpc": "2.0", "method": "textDocument/publishDiagnostics", "params": {"uri": uri, "diagnostics": []}}]

    # ------------------------------------------------------------------ queries
    def _doc(self, p) -> Document | None:
        return self.docs.get(p["textDocument"]["uri"])

    def _m_textDocument_hover(self, p):
        d = self._doc(p)
        if not d:
            return None
        pos = p["position"]
        s = d.symbol_at(pos["line"], pos["character"])
        if s:
            kind = "parameter" if s["kind"] == "parameter" else "variable"
            md = f"```nova\n{s['name']}: {s['type']}\n```\n{kind} · {s['readCount']} read(s), {s['writeCount']} write(s)"
            return {"contents": {"kind": "markdown", "value": md}, "range": _range(self._hit_loc(s, pos))}
        f = d.function_at(pos["line"], pos["character"])
        if f:
            sig = BUILTINS.get(f["name"]) if f["builtin"] else signature(f)
            return {"contents": {"kind": "markdown", "value": f"```nova\n{sig}\n```\n{'built-in ' if f['builtin'] else ''}function · {f['calls']} call(s)"}}
        return None

    @staticmethod
    def _hit_loc(s: dict, pos: dict) -> dict:
        for loc in [s["declLoc"], *s["reads"], *s["writes"]]:
            if _contains(loc, pos["line"], pos["character"]):
                return loc
        return s["declLoc"]

    def _uri_loc(self, p, loc):
        return {"uri": p["textDocument"]["uri"], "range": _range(loc)}

    def _m_textDocument_definition(self, p):
        d = self._doc(p)
        if not d:
            return None
        pos = p["position"]
        s = d.symbol_at(pos["line"], pos["character"])
        if s:
            return self._uri_loc(p, s["declLoc"])
        f = d.function_at(pos["line"], pos["character"])
        if f and not f["builtin"] and f.get("loc"):
            return self._uri_loc(p, f["loc"])
        return None

    def _m_textDocument_references(self, p):
        d = self._doc(p)
        if not d:
            return []
        pos = p["position"]
        s = d.symbol_at(pos["line"], pos["character"])
        if not s:
            return []
        locs = [*s["reads"], *s["writes"]]
        if p.get("context", {}).get("includeDeclaration", True):
            locs.append(s["declLoc"])
        return [self._uri_loc(p, loc) for loc in locs]

    def _m_textDocument_documentSymbol(self, p):
        d = self._doc(p)
        if not d:
            return []
        out = []
        for f in (d.result.get("semantic") or {}).get("functions", []):
            if f["builtin"] or not f.get("loc"):
                continue
            r = _range(f["loc"])
            out.append({"name": f["name"], "detail": signature(f), "kind": KIND_FUNCTION, "range": r, "selectionRange": r})
        return out

    def _m_textDocument_codeAction(self, p):
        """Compiler diagnostics -> quick fixes (the same rule engine as /api/suggest)."""
        d = self._doc(p)
        if not d:
            return []
        rng = p["range"]
        lo, hi = (rng["start"]["line"], rng["start"]["character"]), (rng["end"]["line"], rng["end"]["character"])
        actions = []
        for diag in d.result.get("diagnostics", []):
            r = _range(diag["loc"])
            s, e = (r["start"]["line"], r["start"]["character"]), (r["end"]["line"], r["end"]["character"])
            if not (s <= hi and lo <= max(e, s)):
                continue
            for f in quickfix.fixes_for(d.text, diag):
                edits = [{"range": _range({"line": x["range"]["line"], "col": x["range"]["col"], "endLine": x["range"]["endLine"], "endCol": x["range"]["endCol"]}), "newText": x["newText"]} for x in f["edits"]]
                actions.append({"title": f["title"], "kind": "quickfix", "isPreferred": f["kind"] == "quickfix",
                                "diagnostics": [{"range": r, "message": diag["message"], "code": diag["code"], "severity": SEV.get(diag["severity"], 1)}],
                                "edit": {"changes": {p["textDocument"]["uri"]: edits}}})
        return actions

    def _m_textDocument_completion(self, p):
        d = self._doc(p)
        items = [{"label": k, "kind": KIND_KEYWORD} for k in KEYWORDS] + [{"label": t, "kind": KIND_TYPE} for t in TYPES]
        if d:
            for f in (d.result.get("semantic") or {}).get("functions", []):
                items.append({"label": f["name"], "kind": KIND_FUNCTION, "detail": BUILTINS.get(f["name"]) if f["builtin"] else signature(f)})
            seen = set()
            for s in d.visible_symbols(p["position"]["line"]):
                if s["name"] not in seen:
                    seen.add(s["name"])
                    items.append({"label": s["name"], "kind": KIND_VARIABLE, "detail": s["type"]})
        return {"isIncomplete": False, "items": items}


# ---------------------------------------------------------------------- stdio transport
def read_message(stream: BinaryIO) -> dict | None:
    length = None
    while True:
        line = stream.readline()
        if not line:
            return None
        line = line.strip()
        if not line:
            break
        if line.lower().startswith(b"content-length:"):
            length = int(line.split(b":", 1)[1])
    if length is None:
        return None
    return json.loads(stream.read(length).decode("utf-8"))


def write_message(stream: BinaryIO, msg: dict[str, Any]) -> None:
    body = json.dumps(msg).encode("utf-8")
    stream.write(b"Content-Length: " + str(len(body)).encode() + b"\r\n\r\n" + body)
    stream.flush()


def serve(rd: BinaryIO | None = None, wr: BinaryIO | None = None) -> int:
    rd = rd or sys.stdin.buffer
    wr = wr or sys.stdout.buffer
    server = NovaLanguageServer()
    while not server.exited:
        msg = read_message(rd)
        if msg is None:
            break
        for out in server.handle(msg):
            write_message(wr, out)
    return 0 if server.shutdown_requested else 1


if __name__ == "__main__":
    sys.exit(serve())
