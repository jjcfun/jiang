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
    assert diagnostics[1]["uri"] == helper_uri and diagnostics[1]["diagnostics"] == []
    assert diagnostics[2]["uri"] == main_uri and diagnostics[2]["version"] == 1
    assert any(item["code"] == "unresolved_value" for item in diagnostics[2]["diagnostics"])
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
        (main_uri, 1), (other_uri, 1), (helper_uri, 1), (main_uri, 1),
        (helper_uri, 2), (main_uri, 1), (helper_uri, None),
        (orphan_uri, 1), (orphan_uri, 2), (orphan_uri, None),
    ], diagnostics
    assert diagnostics[0]["diagnostics"] == [], diagnostics[0]
    assert diagnostics[1]["diagnostics"], diagnostics[1]
    assert diagnostics[2]["diagnostics"] == [], diagnostics[2]
    assert diagnostics[3]["diagnostics"], diagnostics[3]
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
    header += "#doc Computes a local value.\n"
    header += "Int local_helper() { 0 }\n"

    def source(local_name, expression):
        return (header + "Int compute(Int param_count) {\n"
                + f"    Int {local_name} = 1;\n"
                + f"    {expression}\n}}\n")

    def position(expression):
        return {"line": 5, "character": 4 + len(expression)}

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
        change(7, "local_count", "Int selected = loc"),
        request(10, "Int selected = loc"),
        change(8, "local_count", "local_count"),
        {"jsonrpc": "2.0", "id": 8, "method": "textDocument/definition",
         "params": {"textDocument": {"uri": uri},
                    "position": {"line": 5, "character": 7}}},
        {"jsonrpc": "2.0", "id": 11, "method": "textDocument/hover",
         "params": {"textDocument": {"uri": uri},
                    "position": {"line": 5, "character": 7}}},
        {"jsonrpc": "2.0", "id": 12, "method": "textDocument/hover",
         "params": {"textDocument": {"uri": uri},
                    "position": {"line": 4, "character": 10}}},
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
    assert by_id[1]["result"]["capabilities"]["completionProvider"]["triggerCharacters"] == ["."]

    def labels(identifier):
        return {item["label"] for item in by_id[identifier]["result"]}

    assert {"local_count", "local_helper"} <= labels(2), by_id[2]
    first_items = {item["label"]: item for item in by_id[2]["result"]}
    assert first_items["local_count"]["kind"] == 6, first_items["local_count"]
    assert first_items["local_count"]["detail"] == "Int local_count", first_items["local_count"]
    assert first_items["local_helper"]["kind"] == 3, first_items["local_helper"]
    assert first_items["local_helper"]["documentation"] == {
        "kind": "markdown", "value": "Computes a local value."
    }, first_items["local_helper"]
    assert "param_count" in labels(3), by_id[3]
    assert "helper" in labels(4), by_id[4]
    assert "other_count" in labels(5) and "local_count" not in labels(5), by_id[5]
    assert "local_helper" in labels(6) and "local_count" not in labels(6), by_id[6]
    assert "value" in labels(7), by_id[7]
    assert "local_count" in labels(10), by_id[10]
    assert by_id[8]["result"] == {
        "uri": uri,
        "range": {"start": {"line": 4, "character": 8},
                  "end": {"line": 4, "character": 19}},
    }, by_id[8]
    assert by_id[11]["result"]["contents"] == {
        "kind": "plaintext", "value": "Int local_count"
    }, by_id[11]
    assert by_id[12]["result"]["contents"] == by_id[11]["result"]["contents"], by_id[12]
    assert by_id[9]["result"] is None


def check_dot_completion(binary, directory):
    root = Path(directory)
    (root / "dot helper.jiang").write_text(
        "public Int answer() { 1 }\n"
        "Int hidden() { 2 }\n"
        "public struct Imported { public Int value; }\n", encoding="utf-8")
    uri = (root / "dot main.jiang").as_uri()
    header = ('alias helper = import "./dot helper.jiang";\n'
              'struct User {\n'
              '    Int id;\n'
              '    Bool active = true;\n'
              '    #doc Scores the user.\n'
              '    public Int score(self) { 1 }\n'
              '}\n'
              'enum Result { ok(Int), err(Int), }\n')

    def source(expression):
        return (header + 'Int main() {\n'
                '    User user = User(id = 1);\n'
                f'    {expression}\n'
                '    Int after = 1;\n'
                '    return after;\n' + '}\n')

    def completion(identifier, expression, character=None):
        return {"jsonrpc": "2.0", "id": identifier, "method": "textDocument/completion",
                "params": {"textDocument": {"uri": uri},
                           "position": {"line": source(expression).splitlines().index('    ' + expression),
                                        "character": character if character is not None else 4 + len(expression)}}}

    def change(version, expression):
        return {"jsonrpc": "2.0", "method": "textDocument/didChange",
                "params": {"textDocument": {"uri": uri, "version": version},
                           "contentChanges": [{"text": source(expression)}]}}

    messages = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}},
        {"jsonrpc": "2.0", "method": "initialized", "params": {}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen",
         "params": {"textDocument": {"uri": uri, "languageId": "jiang", "version": 1,
                                     "text": source('user.')}}},
        completion(2, 'user.'),
        completion(10, 'user.'),
        change(2, 'user.ac'), completion(3, 'user.ac'),
        change(3, 'helper.'), completion(4, 'helper.'),
        change(4, 'helper.an'), completion(5, 'helper.an'),
        change(5, 'Result second = .'), completion(6, 'Result second = .'),
        change(6, 'Result second = .e'), completion(7, 'Result second = .e'),
        change(7, 'Result second = .;'),
        completion(8, 'Result second = .;', 4 + len('Result second = .')),
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

    def labels(identifier):
        return {item["label"] for item in by_id[identifier]["result"]}

    assert {"id", "active", "score"} <= labels(2), by_id[2]
    assert by_id[10]["result"] == by_id[2]["result"], by_id[10]
    assert "helper" not in labels(2), by_id[2]
    score = next(item for item in by_id[2]["result"] if item["label"] == "score")
    assert score["kind"] == 3 and score["documentation"] == {
        "kind": "markdown", "value": "Scores the user."
    }, score
    assert labels(3) == {"active"}, by_id[3]
    assert {"answer", "Imported"} <= labels(4), by_id[4]
    assert "hidden" not in labels(4) and "user" not in labels(4), by_id[4]
    assert labels(5) == {"answer"}, by_id[5]
    assert labels(6) == {"ok", "err"}, by_id[6]
    assert labels(7) == {"err"}, by_id[7]
    assert all(item["kind"] == 20 for item in by_id[6]["result"]), by_id[6]
    assert labels(8) == {"ok", "err"}, by_id[8]
    assert by_id[9]["result"] is None


def check_completion_cache_invalidation(binary, directory):
    uri = (Path(directory) / "completion revision.jiang").as_uri()

    def source(field):
        return (f"struct User {{ Int {field} = 1; }}\n"
                "Int main() { User user = User(); user.\nreturn 0; }\n")

    def completion(identifier):
        return {"jsonrpc": "2.0", "id": identifier, "method": "textDocument/completion",
                "params": {"textDocument": {"uri": uri},
                           "position": {"line": 1, "character": len("Int main() { User user = User(); user.")}}}

    messages = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}},
        {"jsonrpc": "2.0", "method": "initialized", "params": {}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen",
         "params": {"textDocument": {"uri": uri, "languageId": "jiang", "version": 1, "text": source("a")}}},
        completion(2), completion(3),
        {"jsonrpc": "2.0", "method": "textDocument/didChange",
         "params": {"textDocument": {"uri": uri, "version": 2},
                    "contentChanges": [{"text": source("b")}]}},
        completion(4), completion(5),
        {"jsonrpc": "2.0", "id": 6, "method": "shutdown"},
        {"jsonrpc": "2.0", "method": "exit"},
    ]
    process = subprocess.run(
        [binary, "lsp"], input=b"".join(map(frame, messages)),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120, check=False,
    )
    assert process.returncode == 0, (process.returncode, process.stderr.decode())
    assert not process.stderr, process.stderr.decode()
    by_id = {item["id"]: item for item in responses(process.stdout) if "id" in item}
    labels = lambda identifier: {item["label"] for item in by_id[identifier]["result"]}
    assert "a" in labels(2) and "b" not in labels(2), by_id[2]
    assert by_id[3]["result"] == by_id[2]["result"], by_id[3]
    assert "b" in labels(4) and "a" not in labels(4), by_id[4]
    assert by_id[5]["result"] == by_id[4]["result"], by_id[5]


def check_extension_completion(binary, directory):
    root = Path(directory)
    (root / "extension helper.jiang").write_text(
        "public struct Imported { public Int value; }\n"
        "public extend Imported { public Int exported(self) { 1 } }\n"
        "extend Imported { public Int concealed(self) { 2 } }\n", encoding="utf-8")
    uri = (root / "extension main.jiang").as_uri()
    header = ('import dep = "./extension helper.jiang";\n'
              'alias Imported = dep.Imported;\n'
              'struct User { Int id; }\n'
              'extend User { #doc Extension member.\n public Int extra(self) { 1 } }\n'
              'struct Other {}\n'
              'extend Other { public Int wrong(self) { 2 } }\n'
              'struct Holder<T> { T value; }\n'
              '@where(T == Int)\n'
              'extend <T> Holder<T> { public Int only_int(self) { 3 } }\n')

    def source(expression):
        return header + 'Int main() {\n' + f'    {expression}\n' + '    return 0;\n}\n'

    def completion(identifier, expression):
        return {"jsonrpc": "2.0", "id": identifier, "method": "textDocument/completion",
                "params": {"textDocument": {"uri": uri},
                           "position": {"line": source(expression).splitlines().index('    ' + expression),
                                        "character": 4 + len(expression)}}}

    def change(version, expression):
        return {"jsonrpc": "2.0", "method": "textDocument/didChange",
                "params": {"textDocument": {"uri": uri, "version": version},
                           "contentChanges": [{"text": source(expression)}]}}

    messages = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}},
        {"jsonrpc": "2.0", "method": "initialized", "params": {}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen",
         "params": {"textDocument": {"uri": uri, "languageId": "jiang", "version": 1,
                                     "text": source('User user = User(id = 1); user.')}}},
        completion(2, 'User user = User(id = 1); user.'),
        change(2, 'Holder<Int> h = Holder<Int>(value = 1); h.'),
        completion(3, 'Holder<Int> h = Holder<Int>(value = 1); h.'),
        change(3, 'Holder<Bool> h = Holder<Bool>(value = true); h.'),
        completion(4, 'Holder<Bool> h = Holder<Bool>(value = true); h.'),
        change(4, 'Imported item = Imported(value = 1); item.'),
        completion(5, 'Imported item = Imported(value = 1); item.'),
        {"jsonrpc": "2.0", "id": 6, "method": "shutdown"},
        {"jsonrpc": "2.0", "method": "exit"},
    ]
    process = subprocess.run(
        [binary, "lsp"], input=b"".join(map(frame, messages)),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=180, check=False,
    )
    assert process.returncode == 0, (process.returncode, process.stderr.decode())
    assert not process.stderr, process.stderr.decode()
    by_id = {item["id"]: item for item in responses(process.stdout) if "id" in item}

    def labels(identifier):
        return {item["label"] for item in by_id[identifier]["result"]}

    assert {"id", "extra"} <= labels(2), by_id[2]
    assert "wrong" not in labels(2) and "only_int" not in labels(2), by_id[2]
    extra = next(item for item in by_id[2]["result"] if item["label"] == "extra")
    assert extra["kind"] == 3 and extra["documentation"] == {
        "kind": "markdown", "value": "Extension member."
    }, extra
    assert "only_int" in labels(3), by_id[3]
    assert "only_int" not in labels(4), by_id[4]
    assert "exported" in labels(5) and "concealed" not in labels(5), by_id[5]
    assert by_id[6]["result"] is None


def check_hover_documentation(binary, directory):
    uri = (Path(directory) / "hover documentation.jiang").as_uri()
    original = ("#doc Adds **one**.\n"
                "Int documented(Int value) { value + 1 }\n"
                "Int plain() { 0 }\n"
                "Int main() { documented(plain()) }\n")
    changed = original.replace("Adds **one**.", "Adds **two**.")
    undocumented = original.replace("#doc Adds **one**.\n", "")

    def position(text, needle, last=False):
        offset = (text.rindex(needle) if last else text.index(needle)) + 1
        before = text[:offset]
        return {"line": before.count("\n"), "character": len(before.rsplit("\n", 1)[-1])}

    def hover(identifier, text, needle, last=False):
        return {"jsonrpc": "2.0", "id": identifier, "method": "textDocument/hover",
                "params": {"textDocument": {"uri": uri},
                           "position": position(text, needle, last)}}

    def change(version, text):
        return {"jsonrpc": "2.0", "method": "textDocument/didChange",
                "params": {"textDocument": {"uri": uri, "version": version},
                           "contentChanges": [{"text": text}]}}

    messages = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen",
         "params": {"textDocument": {"uri": uri, "languageId": "jiang",
                                     "version": 1, "text": original}}},
        hover(2, original, "documented"),
        hover(3, original, "documented", True),
        hover(4, original, "plain"),
        change(2, changed),
        hover(5, changed, "documented", True),
        change(3, undocumented),
        hover(6, undocumented, "documented", True),
        {"jsonrpc": "2.0", "id": 7, "method": "shutdown"},
        {"jsonrpc": "2.0", "method": "exit"},
    ]
    process = subprocess.run(
        [binary, "lsp"], input=b"".join(map(frame, messages)),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120, check=False,
    )
    assert process.returncode == 0, (process.returncode, process.stderr.decode())
    assert not process.stderr, process.stderr.decode()
    by_id = {item["id"]: item for item in responses(process.stdout) if "id" in item}
    documented = {"kind": "markdown", "value":
                  "```jiang\nInt documented(Int value)\n```\n\nAdds **one**."}
    assert by_id[2]["result"]["contents"] == documented, by_id[2]
    assert by_id[3]["result"]["contents"] == documented, by_id[3]
    assert by_id[4]["result"]["contents"] == {"kind": "plaintext", "value": "Int plain()"}, by_id[4]
    assert by_id[5]["result"]["contents"]["value"].endswith("Adds **two**."), by_id[5]
    assert by_id[6]["result"]["contents"] == {
        "kind": "plaintext", "value": "Int documented(Int value)"
    }, by_id[6]


def check_imported_hover_documentation(binary, directory):
    root = Path(directory) / "hover import"
    root.mkdir()
    (root / "helper.jiang").write_text(
        "#doc Returns **42**.\npublic Int answer() { 42 }\n", encoding="utf-8")
    uri = (root / "main.jiang").as_uri()
    source = 'alias helper = import "./helper.jiang";\nInt main() { helper.answer() }\n'
    answer = source.index("answer") + 1
    before = source[:answer]
    position = {"line": before.count("\n"), "character": len(before.rsplit("\n", 1)[-1])}
    messages = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen",
         "params": {"textDocument": {"uri": uri, "languageId": "jiang",
                                     "version": 1, "text": source}}},
        {"jsonrpc": "2.0", "id": 2, "method": "textDocument/hover",
         "params": {"textDocument": {"uri": uri}, "position": position}},
        {"jsonrpc": "2.0", "id": 3, "method": "shutdown"},
        {"jsonrpc": "2.0", "method": "exit"},
    ]
    for _ in range(2):
        process = subprocess.run(
            [binary, "lsp"], input=b"".join(map(frame, messages)),
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120, check=False,
        )
        assert process.returncode == 0, (process.returncode, process.stderr.decode())
        assert not process.stderr, process.stderr.decode()
        by_id = {item["id"]: item for item in responses(process.stdout) if "id" in item}
        assert by_id[2]["result"]["contents"] == {
            "kind": "markdown", "value": "```jiang\nInt answer()\n```\n\nReturns **42**."
        }, by_id[2]


def check_request_lifecycle(binary, directory):
    uri = (Path(directory) / "lifecycle.jiang").as_uri()
    original = "Int value() { 1 }\nInt main() { value() }\n"
    reopened = "Int revised() { 1 }\nInt main() { revised() }\n"

    def open_document(text):
        return {"jsonrpc": "2.0", "method": "textDocument/didOpen",
                "params": {"textDocument": {"uri": uri, "languageId": "jiang",
                                            "version": 1, "text": text}}}

    def change_document(version, text):
        return {"jsonrpc": "2.0", "method": "textDocument/didChange",
                "params": {"textDocument": {"uri": uri, "version": version},
                           "contentChanges": [{"text": text}]}}

    def request(identifier, method):
        return {"jsonrpc": "2.0", "id": identifier, "method": method,
                "params": {"textDocument": {"uri": uri},
                           "position": {"line": 1, "character": 13}}}

    messages = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}},
        open_document(original),
        request(2, "textDocument/definition"),
        change_document(1, "Int stale() { 0 }\n"),
        request(3, "textDocument/definition"),
        {"jsonrpc": "2.0", "method": "textDocument/didClose",
         "params": {"textDocument": {"uri": uri}}},
        change_document(2, "Int closed() { 0 }\n"),
        request(4, "textDocument/definition"),
        request(5, "textDocument/hover"),
        request(6, "textDocument/completion"),
        open_document(reopened),
        request(7, "textDocument/definition"),
        request(8, "textDocument/definition"),
        {"jsonrpc": "2.0", "method": "$/cancelRequest", "params": {"id": 8}},
        request(9, "textDocument/definition"),
        {"jsonrpc": "2.0", "id": 10, "method": "shutdown"},
        request(11, "textDocument/definition"),
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
    assert set(by_id) == {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, items
    original_target = {"uri": uri,
                       "range": {"start": {"line": 0, "character": 4},
                                 "end": {"line": 0, "character": 9}}}
    reopened_target = {"uri": uri,
                       "range": {"start": {"line": 0, "character": 4},
                                 "end": {"line": 0, "character": 11}}}
    assert by_id[2]["result"] == original_target, by_id[2]
    assert by_id[3]["result"] == original_target, by_id[3]
    assert by_id[4]["result"] is None and by_id[5]["result"] is None
    assert by_id[6]["result"] == [], by_id[6]
    assert all(by_id[index]["result"] == reopened_target for index in (7, 8, 9)), by_id
    assert by_id[10]["result"] is None
    assert by_id[11]["error"]["code"] == -32002, by_id[11]
    diagnostics = [item["params"] for item in items
                   if item.get("method") == "textDocument/publishDiagnostics"]
    assert [(item["uri"], item.get("version")) for item in diagnostics] == [
        (uri, 1), (uri, None), (uri, 1),
    ], diagnostics


def check_frontend_and_save_diagnostics(binary, directory):
    root = Path(directory) / "staged diagnostics"
    root.mkdir()
    (root / "package.jiang").write_text(
        '#package { name = "lsp_stage"; root = "main.jiang"; }\n', encoding="utf-8")
    uri = (root / "main.jiang").as_uri()
    helper_uri = (root / "helper.jiang").as_uri()
    unrelated_uri = (Path(directory) / "staged unrelated.jiang").as_uri()
    header = "struct Value { Int value; }\n"
    invalid = (header + "Int main() { Value first = Value(value = 1); "
               "Value second = first; first.value + second.value }\n")
    valid = (header + "Int main() { Value first = Value(value = 1); "
             "Value second = first; second.value }\n")
    (root / "main.jiang").write_text(invalid, encoding="utf-8")
    (root / "helper.jiang").write_text("Int helper() { 0 }\n", encoding="utf-8")
    messages = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen",
         "params": {"textDocument": {"uri": uri, "languageId": "jiang",
                                     "version": 1, "text": invalid}}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen",
         "params": {"textDocument": {"uri": helper_uri, "languageId": "jiang",
                                     "version": 1, "text": "Int helper() { 0 }\n"}}},
        {"jsonrpc": "2.0", "method": "textDocument/didSave",
         "params": {"textDocument": {"uri": helper_uri}}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen",
         "params": {"textDocument": {"uri": unrelated_uri, "languageId": "jiang",
                                     "version": 1, "text": "Int unrelated() { 0 }\n"}}},
        {"jsonrpc": "2.0", "method": "textDocument/didChange",
         "params": {"textDocument": {"uri": uri, "version": 2},
                    "contentChanges": [{"text": valid}]}},
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
    diagnostics = [item["params"] for item in items
                   if item.get("method") == "textDocument/publishDiagnostics"]
    assert [(item["uri"], item["version"]) for item in diagnostics] == [
        (uri, 1), (helper_uri, 1), (uri, 1), (unrelated_uri, 1), (uri, 2),
    ], diagnostics
    assert diagnostics[0]["diagnostics"] == [], diagnostics[0]
    assert diagnostics[1]["diagnostics"] == [], diagnostics[1]
    assert any(item["code"] == "use_after_move" for item in diagnostics[2]["diagnostics"])
    assert diagnostics[3]["diagnostics"] == [], diagnostics[3]
    assert diagnostics[4]["diagnostics"] == [], diagnostics[4]


def check_dependency_roots(binary, directory):
    root = Path(directory) / "dependency roots"
    app = root / "app"
    dep = root / "dep"
    app.mkdir(parents=True)
    dep.mkdir()
    (app / "package.jiang").write_text(
        '#package { name = "lsp_root_app"; root = "main.jiang"; '
        'dependencies { dep = "../dep"; } }\n', encoding="utf-8")
    (dep / "package.jiang").write_text(
        '#package { name = "lsp_root_dep"; root = "lib.jiang"; }\n', encoding="utf-8")
    main = "import dep;\nInt main() { dep.answer() }\n"
    (app / "main.jiang").write_text(main, encoding="utf-8")
    (dep / "lib.jiang").write_text("public Int answer() { 1 }\n", encoding="utf-8")
    main_uri = (app / "main.jiang").as_uri()
    dep_uri = (dep / "lib.jiang").as_uri()
    other = "Int other() { 0 }\n"
    (app / "other.jiang").write_text(other, encoding="utf-8")
    other_uri = (app / "other.jiang").as_uri()
    messages = [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen",
         "params": {"textDocument": {"uri": main_uri, "languageId": "jiang",
                                     "version": 1, "text": main}}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen",
         "params": {"textDocument": {"uri": other_uri, "languageId": "jiang",
                                     "version": 1, "text": other}}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen",
         "params": {"textDocument": {"uri": dep_uri, "languageId": "jiang",
                                     "version": 1, "text": "public Int wrong() { 1 }\n"}}},
        {"jsonrpc": "2.0", "method": "textDocument/didChange",
         "params": {"textDocument": {"uri": dep_uri, "version": 2},
                    "contentChanges": [{"text": "public Int answer() { 2 }\n"}]}},
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
    app_diagnostics = [item for item in diagnostics if item["uri"] == main_uri]
    assert [item["diagnostics"] == [] for item in app_diagnostics] == [True, False, True], diagnostics
    assert any(item["code"] == "unresolved_value" for item in app_diagnostics[1]["diagnostics"])
    other_diagnostics = [item for item in diagnostics if item["uri"] == other_uri]
    assert len(other_diagnostics) == 1 and other_diagnostics[0]["diagnostics"] == [], diagnostics
    dep_diagnostics = [item for item in diagnostics if item["uri"] == dep_uri]
    assert [item["version"] for item in dep_diagnostics] == [1, 2], diagnostics


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
        assert capabilities["textDocumentSync"] == {"openClose": True, "change": 1,
                                                     "save": True}
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
        check_dot_completion(binary, directory)
        check_completion_cache_invalidation(binary, directory)
        check_extension_completion(binary, directory)
        check_hover_documentation(binary, directory)
        check_imported_hover_documentation(binary, directory)
        check_request_lifecycle(binary, directory)
        check_frontend_and_save_diagnostics(binary, directory)
        check_dependency_roots(binary, directory)
    print("PASS jiang lsp smoke")


if __name__ == "__main__":
    main()
