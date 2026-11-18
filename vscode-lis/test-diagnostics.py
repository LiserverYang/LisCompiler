"""Exercise editor diagnostics over the language server's stdio protocol."""

import io
import json
from pathlib import Path
import subprocess
import sys


def frame(message):
    body = json.dumps({"jsonrpc": "2.0", **message}).encode()
    return f"Content-Length: {len(body)}\r\n\r\n".encode() + body


def messages(output):
    stream = io.BytesIO(output)
    while header := stream.readline():
        name, size = header.decode().split(":", 1)
        if name.lower() != "content-length" or stream.readline() != b"\r\n":
            raise AssertionError(f"invalid LSP frame: {header!r}")
        yield json.loads(stream.read(int(size)))


def main():
    uri = (Path.cwd() / "editor diagnostics.lis").as_uri()
    document = {"uri": uri}
    requests = [
        {"id": 1, "method": "initialize", "params": {}},
        {
            "method": "textDocument/didOpen",
            "params": {"textDocument": {
                **document, "version": 1, "text":
                "fn main() -> i32 { let x: i32; ret x; }",
            }},
        },
    ]
    for version, text in enumerate([
        "fn main() -> i32 { let x = ; ret 0; }",
        "fn main() -> i32 { ret 0; }",
        "fn bad() -> &i32 { let x = 0; ret &x; }",
    ], start=2):
        requests.append({
            "method": "textDocument/didChange",
            "params": {
                "textDocument": {**document, "version": version},
                "contentChanges": [{"text": text}],
            },
        })
    requests.extend([{"id": 2, "method": "shutdown"}, {"method": "exit"}])
    result = subprocess.run(
        [str(Path(sys.argv[1]).resolve())],
        input=b"".join(map(frame, requests)),
        capture_output=True,
        timeout=30,
    )
    result.check_returncode()
    notifications = [
        message["params"] for message in messages(result.stdout)
        if message.get("method") == "textDocument/publishDiagnostics"
    ]
    assert all(params["uri"] == uri for params in notifications), notifications
    codes = [
        [diag.get("code", "") for diag in params["diagnostics"] if diag["severity"] == 1]
        for params in notifications
    ]
    assert len(codes) == 4, codes
    assert codes[0] == ["E3011"], codes
    assert any(code.startswith("E2") for code in codes[1]), codes
    assert codes[2] == [], codes
    assert codes[3] == ["E4007"], codes
    print("Editor diagnostics passed.")


if __name__ == "__main__":
    main()
