#!/usr/bin/env python3
"""Exercise Jiang's stdio LSP lifecycle, overlays, versions, and diagnostics."""

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def frame(message):
    body = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    return f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body


def responses(data):
    offset = 0
    result = []
    while offset < len(data):
        header_end = data.index(b"\r\n\r\n", offset)
        fields = data[offset:header_end].split(b"\r\n")
        lengths = [int(field.split(b":", 1)[1].strip()) for field in fields
                   if field.lower().startswith(b"content-length:")]
        assert len(lengths) == 1, fields
        body_start = header_end + 4
        body_end = body_start + lengths[0]
        result.append(json.loads(data[body_start:body_end]))
        offset = body_end
    return result


def check_overlay_restore(binary, directory):
    root = Path(directory)
    helper = root / "helper.jiang"
    helper.write_text("public Int answer() { 0 }\n", encoding="utf-8")
    main_uri = (root / "importer.jiang").as_uri()
    helper_uri = helper.as_uri()
    source = 'alias helper = import "./helper.jiang";\nInt main() { helper.answer() }\n'
    messages = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}},
        {"jsonrpc": "2.0", "method": "initialized", "params": {}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen",
         "params": {"textDocument": {"uri": main_uri, "languageId": "jiang",
                                     "version": 1, "text": source}}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen",
         "params": {"textDocument": {"uri": helper_uri, "languageId": "jiang",
                                     "version": 1, "text": "public Int wrong() { 0 }\n"}}},
        {"jsonrpc": "2.0", "method": "textDocument/didChange",
         "params": {"textDocument": {"uri": main_uri, "version": 2},
                    "contentChanges": [{"text": source}]}},
        {"jsonrpc": "2.0", "method": "textDocument/didClose",
         "params": {"textDocument": {"uri": helper_uri}}},
        {"jsonrpc": "2.0", "method": "textDocument/didChange",
         "params": {"textDocument": {"uri": main_uri, "version": 3},
                    "contentChanges": [{"text": source}]}},
        {"jsonrpc": "2.0", "id": 2, "method": "shutdown"},
        {"jsonrpc": "2.0", "method": "exit"},
    ]
    process = subprocess.run(
        [binary, "lsp"], input=b"".join(map(frame, messages)),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120, check=False,
    )
    assert process.returncode == 0, (process.returncode, process.stderr.decode())
    assert not process.stderr, process.stderr.decode()
    diagnostics = [item["params"] for item in responses(process.stdout)
                   if item.get("method") == "textDocument/publishDiagnostics"]
    assert len(diagnostics) == 5, diagnostics
    assert diagnostics[0]["uri"] == main_uri and diagnostics[0]["diagnostics"] == []
    assert diagnostics[1]["uri"] == helper_uri and diagnostics[1]["diagnostics"] == []
    assert diagnostics[2]["uri"] == main_uri and diagnostics[2]["version"] == 2
    assert any(item["code"] == "unresolved_value" for item in diagnostics[2]["diagnostics"])
    assert diagnostics[3]["uri"] == helper_uri and diagnostics[3]["diagnostics"] == []
    assert diagnostics[4]["uri"] == main_uri and diagnostics[4]["version"] == 3
    assert diagnostics[4]["diagnostics"] == [], diagnostics[4]


def check_semantics(binary, directory):
    root = Path(directory)
    helper = root / "semantic helper.jiang"
    helper.write_text("public Int answer() { 0 }\n", encoding="utf-8")
    main_uri = (root / "semantic main.jiang").as_uri()
    helper_uri = helper.as_uri()
    source = 'alias helper = import "./semantic helper.jiang";\nInt main() { helper.answer() }\n'
    column = source.splitlines()[1].index("answer")
    position = {"line": 1, "character": column}
    document = {"uri": main_uri}

    def request(identifier, method, at=position):
        return {"jsonrpc": "2.0", "id": identifier, "method": method,
                "params": {"textDocument": document, "position": at}}

    messages = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}},
        {"jsonrpc": "2.0", "method": "initialized", "params": {}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen",
         "params": {"textDocument": {"uri": main_uri, "languageId": "jiang",
                                     "version": 1, "text": source}}},
        request(2, "textDocument/definition"),
        request(3, "textDocument/hover"),
        request(4, "textDocument/definition", {"line": 1, "character": column - 1}),
        request(5, "textDocument/hover", {"line": 99, "character": 0}),
        {"jsonrpc": "2.0", "method": "textDocument/didOpen",
         "params": {"textDocument": {"uri": helper_uri, "languageId": "jiang",
                                     "version": 1, "text": "public Int wrong() { 1 }\n"}}},
        request(6, "textDocument/definition"),
        {"jsonrpc": "2.0", "method": "textDocument/didChange",
         "params": {"textDocument": {"uri": helper_uri, "version": 2},
                    "contentChanges": [{"text": "public Int answer() { 1 }\n"}]}},
        request(7, "textDocument/definition"),
        {"jsonrpc": "2.0", "method": "textDocument/didClose",
         "params": {"textDocument": {"uri": helper_uri}}},
        request(8, "textDocument/definition"),
        {"jsonrpc": "2.0", "id": 9, "method": "textDocument/hover",
         "params": {"textDocument": document}},
        {"jsonrpc": "2.0", "id": 10, "method": "shutdown"},
        {"jsonrpc": "2.0", "method": "exit"},
    ]
    process = subprocess.run(
        [binary, "lsp"], input=b"".join(map(frame, messages)),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120, check=False,
    )
    assert process.returncode == 0, (process.returncode, process.stderr.decode())
    assert not process.stderr, process.stderr.decode()
    items = responses(process.stdout)
    by_id = {item["id"]: item for item in items if "id" in item}
    capabilities = by_id[1]["result"]["capabilities"]
    assert capabilities["definitionProvider"] and capabilities["hoverProvider"]
    target = {"uri": helper_uri,
              "range": {"start": {"line": 0, "character": 11},
                        "end": {"line": 0, "character": 17}}}
    assert by_id[2]["result"] == target, by_id[2]
    assert by_id[3]["result"]["contents"] == {
        "kind": "plaintext", "value": "Int answer()"
    }, by_id[3]
    assert by_id[4]["result"] is None and by_id[5]["result"] is None
    assert by_id[6]["result"] is None, by_id[6]
    assert by_id[7]["result"] == target, by_id[7]
    assert by_id[8]["result"] == target, by_id[8]
    assert by_id[9]["error"]["code"] == -32602
    assert by_id[10]["result"] is None


def main():
    binary = sys.argv[1]
    with tempfile.TemporaryDirectory(prefix="jiang lsp ") as directory:
        uri = (Path(directory) / "main.jiang").as_uri()
        other_uri = (Path(directory) / "other.jiang").as_uri()
        messages = [
            {"jsonrpc": "2.0", "id": 1, "method": "initialize",
             "params": {"processId": None, "rootUri": None, "capabilities": {}}},
            {"jsonrpc": "2.0", "method": "initialized", "params": {}},
            {"jsonrpc": "2.0", "method": "textDocument/didOpen",
             "params": {"textDocument": {"uri": uri, "languageId": "jiang",
                                         "version": 1, "text": '// 😀\nInt main() { "😀" @ }'}}},
            {"jsonrpc": "2.0", "method": "textDocument/didOpen",
             "params": {"textDocument": {"uri": other_uri, "languageId": "jiang",
                                         "version": 1, "text": "Int other() {"}}},
            {"jsonrpc": "2.0", "method": "textDocument/didChange",
             "params": {"textDocument": {"uri": uri, "version": 1},
                        "contentChanges": [{"text": "Int main() { 0 }"}]}},
            {"jsonrpc": "2.0", "method": "textDocument/didChange",
             "params": {"textDocument": {"uri": uri, "version": 2},
                        "contentChanges": [{"text": "Int main() { 0 }"}]}},
            {"jsonrpc": "2.0", "method": "textDocument/didClose",
             "params": {"textDocument": {"uri": uri}}},
            {"jsonrpc": "2.0", "id": "missing", "method": "unknown/request", "params": {}},
            {"jsonrpc": "2.0", "id": 2, "method": "shutdown"},
            {"jsonrpc": "2.0", "method": "exit"},
        ]
        process = subprocess.run(
            [binary, "lsp"], input=b"".join(map(frame, messages)),
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120, check=False,
        )
        assert process.returncode == 0, (process.returncode, process.stderr.decode())
        assert not process.stderr, process.stderr.decode()
        items = responses(process.stdout)
        assert len(items) == 7, items
        capabilities = items[0]["result"]["capabilities"]
        assert capabilities["positionEncoding"] == "utf-16"
        assert capabilities["textDocumentSync"] == {"openClose": True, "change": 1}
        first = items[1]["params"]
        assert first["uri"] == uri and first["version"] == 1
        assert len(first["diagnostics"]) > 0, first
        assert first["diagnostics"][0]["range"]["start"]["line"] == 1
        assert first["diagnostics"][0]["range"]["start"]["character"] == 18
        other = items[2]["params"]
        assert other["uri"] == other_uri and other["version"] == 1
        assert len(other["diagnostics"]) > 0, other
        second = items[3]["params"]
        assert second["version"] == 2 and second["diagnostics"] == [], second
        assert second["uri"] == uri
        closed = items[4]["params"]
        assert closed["uri"] == uri and closed["diagnostics"] == []
        assert items[5]["id"] == "missing" and items[5]["error"]["code"] == -32601
        assert items[6] == {"jsonrpc": "2.0", "id": 2, "result": None}
        check_overlay_restore(binary, directory)
        check_semantics(binary, directory)
    print("PASS jiang lsp smoke")


if __name__ == "__main__":
    main()
