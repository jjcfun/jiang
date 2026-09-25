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
    assert len(diagnostics) == 7, diagnostics
    assert diagnostics[0]["uri"] == main_uri and diagnostics[0]["diagnostics"] == []
    assert diagnostics[1]["uri"] == main_uri and diagnostics[1]["version"] == 1
    assert any(item["code"] == "unresolved_value" for item in diagnostics[1]["diagnostics"])
    assert diagnostics[2]["uri"] == helper_uri and diagnostics[2]["diagnostics"] == []
    assert diagnostics[3]["uri"] == main_uri and diagnostics[3]["version"] == 2
    assert diagnostics[3]["diagnostics"], diagnostics[3]
    assert diagnostics[4]["uri"] == helper_uri and diagnostics[4]["diagnostics"] == []
    assert diagnostics[5]["uri"] == main_uri and diagnostics[5]["diagnostics"] == []
    assert diagnostics[6]["uri"] == main_uri and diagnostics[6]["version"] == 3
    assert diagnostics[6]["diagnostics"] == [], diagnostics[6]


def check_package_documents(binary, directory):
    root = Path(directory)
    app = root / "package app"
    dep = root / "package dep"
    app.mkdir()
    dep.mkdir()
    (app / "package.jiang").write_text(
        '#package { name = "lsp_app"; root = "main.jiang"; '
        'dependencies { dep = "../package dep"; } }\n', encoding="utf-8")
    (dep / "package.jiang").write_text(
        '#package { name = "lsp_dep"; root = "lib.jiang"; }\n', encoding="utf-8")
    (dep / "lib.jiang").write_text("public Int answer() { 1 }\n", encoding="utf-8")
    main_text = ('import dep;\nalias helper = import "./helper.jiang";\n'
                 'Int main() { dep.answer() + helper.value() }\n')
    (app / "main.jiang").write_text(main_text, encoding="utf-8")
    good_helper = "public Int value() { 1 }\n"
    (app / "helper.jiang").write_text(good_helper, encoding="utf-8")
    main_uri = (app / "main.jiang").as_uri()
    helper_uri = (app / "helper.jiang").as_uri()
    orphan_uri = (app / "orphan.jiang").as_uri()
    other_uri = (root / "unrelated.jiang").as_uri()

    def open_document(uri, text):
        return {"jsonrpc": "2.0", "method": "textDocument/didOpen",
                "params": {"textDocument": {"uri": uri, "languageId": "jiang",
                                            "version": 1, "text": text}}}

    messages = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}},
        {"jsonrpc": "2.0", "method": "initialized", "params": {}},
        open_document(main_uri, main_text),
        open_document(other_uri, "Int unrelated() { missing }\n"),
        open_document(helper_uri, "public Int wrong() { 1 }\n"),
        {"jsonrpc": "2.0", "method": "textDocument/didChange",
         "params": {"textDocument": {"uri": helper_uri, "version": 2},
                    "contentChanges": [{"text": good_helper}]}},
        {"jsonrpc": "2.0", "method": "textDocument/didClose",
         "params": {"textDocument": {"uri": helper_uri}}},
        open_document(orphan_uri, "Int orphan() {"),
        {"jsonrpc": "2.0", "method": "textDocument/didChange",
         "params": {"textDocument": {"uri": orphan_uri, "version": 2},
                    "contentChanges": [{"text": "Int orphan() { 1 }\n"}]}},
        {"jsonrpc": "2.0", "id": 3, "method": "textDocument/definition",
         "params": {"textDocument": {"uri": orphan_uri},
                    "position": {"line": 0, "character": 5}}},
        {"jsonrpc": "2.0", "method": "textDocument/didClose",
         "params": {"textDocument": {"uri": orphan_uri}}},
        {"jsonrpc": "2.0", "id": 2, "method": "shutdown"},
        {"jsonrpc": "2.0", "method": "exit"},
    ]
    process = subprocess.run(
        [binary, "lsp"], input=b"".join(map(frame, messages)),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=180, check=False,
    )
    assert process.returncode == 0, (process.returncode, process.stderr.decode())
    assert not process.stderr, process.stderr.decode()
    items = responses(process.stdout)
    diagnostics = [item["params"] for item in items
                   if item.get("method") == "textDocument/publishDiagnostics"]
    assert [(item["uri"], item.get("version")) for item in diagnostics] == [
        (main_uri, 1), (other_uri, 1), (main_uri, 1), (helper_uri, 1),
        (main_uri, 1), (helper_uri, 2), (helper_uri, None),
        (orphan_uri, 1), (orphan_uri, 2), (orphan_uri, None),
    ], diagnostics
    assert diagnostics[0]["diagnostics"] == [], diagnostics[0]
    assert diagnostics[1]["diagnostics"], diagnostics[1]
    assert diagnostics[2]["diagnostics"], diagnostics[2]
    assert diagnostics[3]["diagnostics"] == [], diagnostics[3]
    assert diagnostics[4]["diagnostics"] == [], diagnostics[4]
    assert diagnostics[5]["diagnostics"] == [], diagnostics[5]
    assert diagnostics[6]["diagnostics"] == [], diagnostics[6]
    assert diagnostics[7]["diagnostics"], diagnostics[7]
    assert diagnostics[8]["diagnostics"] == [], diagnostics[8]
    assert diagnostics[9]["diagnostics"] == [], diagnostics[9]
    definition = next(item["result"] for item in items if item.get("id") == 3)
    assert definition == {
        "uri": orphan_uri,
        "range": {"start": {"line": 0, "character": 4},
                  "end": {"line": 0, "character": 10}},
    }, definition


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


def check_completion(binary, directory):
    root = Path(directory)
    helper = root / "completion helper.jiang"
    helper.write_text("public Int value() { 0 }\n", encoding="utf-8")
    uri = (root / "completion main.jiang").as_uri()
    header = 'alias helper = import "./completion helper.jiang";\n'
    header += "Int local_helper() { 0 }\n"

    def source(local_name, expression):
        return (header + "Int compute(Int param_count) {\n"
                + f"    Int {local_name} = 1;\n"
                + f"    {expression}\n}}\n")

    def position(expression):
        return {"line": 4, "character": 4 + len(expression)}

    def request(identifier, expression):
        return {"jsonrpc": "2.0", "id": identifier, "method": "textDocument/completion",
                "params": {"textDocument": {"uri": uri}, "position": position(expression)}}

    def change(version, local_name, expression):
        return {"jsonrpc": "2.0", "method": "textDocument/didChange",
                "params": {"textDocument": {"uri": uri, "version": version},
                           "contentChanges": [{"text": source(local_name, expression)}]}}

    messages = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}},
        {"jsonrpc": "2.0", "method": "initialized", "params": {}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen",
         "params": {"textDocument": {"uri": uri, "languageId": "jiang", "version": 1,
                                     "text": source("local_count", "loc")}}},
        request(2, "loc"),
        change(2, "local_count", "par"),
        request(3, "par"),
        change(3, "local_count", "hel"),
        request(4, "hel"),
        change(4, "other_count", "oth"),
        request(5, "oth"),
        change(5, "other_count", "loc"),
        request(6, "loc"),
        change(6, "other_count", "helper."),
        request(7, "helper."),
        change(7, "local_count", "local_count"),
        {"jsonrpc": "2.0", "id": 8, "method": "textDocument/definition",
         "params": {"textDocument": {"uri": uri},
                    "position": {"line": 4, "character": 7}}},
        {"jsonrpc": "2.0", "id": 9, "method": "shutdown"},
        {"jsonrpc": "2.0", "method": "exit"},
    ]
    process = subprocess.run(
        [binary, "lsp"], input=b"".join(map(frame, messages)),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=180, check=False,
    )
    assert process.returncode == 0, (process.returncode, process.stderr.decode())
    assert not process.stderr, process.stderr.decode()
    by_id = {item["id"]: item for item in responses(process.stdout) if "id" in item}
    assert "completionProvider" in by_id[1]["result"]["capabilities"]

    def labels(identifier):
        return {item["label"] for item in by_id[identifier]["result"]}

    assert {"local_count", "local_helper"} <= labels(2), by_id[2]
    assert "param_count" in labels(3), by_id[3]
    assert "helper" in labels(4), by_id[4]
    assert "other_count" in labels(5) and "local_count" not in labels(5), by_id[5]
    assert "local_helper" in labels(6) and "local_count" not in labels(6), by_id[6]
    assert by_id[7]["result"] == [], by_id[7]
    assert by_id[8]["result"] == {
        "uri": uri,
        "range": {"start": {"line": 3, "character": 8},
                  "end": {"line": 3, "character": 19}},
    }, by_id[8]
    assert by_id[9]["result"] is None


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
        check_package_documents(binary, directory)
        check_semantics(binary, directory)
        check_completion(binary, directory)
    print("PASS jiang lsp smoke")


if __name__ == "__main__":
    main()
