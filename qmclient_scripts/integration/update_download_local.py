#!/usr/bin/env python3
"""本地真实 curl 行为验收：强制直连覆盖环境代理、正文、截断和预算取消。"""
from __future__ import annotations

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import hashlib
import io
import json
import os
from pathlib import Path
import socket
import threading
import time
import uuid
import zipfile

from qmclient_scripts.integration.update_download_live import REPO_ROOT, inspect_download, run_probe


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    args = parser.parse_args()
    probe = args.probe.resolve(strict=True)
    output = REPO_ROOT / "tmp/update-local" / ("run_" + uuid.uuid4().hex[:12])
    output.mkdir(parents=True)
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w") as archive:
        archive.writestr("client.txt", b"real local HTTP fixture")
    body = stream.getvalue()
    digest = hashlib.sha256(body).hexdigest()
    release = threading.Event()
    requested_paths: list[str] = []

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *arguments):
            pass

        def do_GET(self):
            requested_paths.append(self.path)
            if self.path == "/delayed":
                release.wait(5)
            payload = b"<html>gateway error</html>" if self.path == "/html" else body
            self.send_response(200)
            self.send_header("Content-Length", str(len(payload) * 2 if self.path == "/truncated" else len(payload)))
            self.end_headers()
            if self.path == "/stalled":
                release.wait(5)
            try:
                self.wfile.write(payload)
                self.wfile.flush()
                if self.path == "/truncated":
                    self.connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    environment = dict(os.environ)
    for name in ("http_proxy", "https_proxy", "all_proxy", "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY"):
        environment[name] = "http://127.0.0.1:1"
    environment.update(NO_PROXY="", no_proxy="")
    results = []
    try:
        def fetch(route: str, name: str, mode="direct", budget=5):
            path = output / name
            result = run_probe(probe, ["fetch", f"http://127.0.0.1:{server.server_port}/{route}", path.relative_to(REPO_ROOT).as_posix(), mode, str(budget), "1048576"], timeout=budget + 10, env=environment)
            result["scenario"] = name
            results.append(result)
            return result, path

        queued = run_probe(probe, ["cancel-queued", f"http://127.0.0.1:{server.server_port}/queued"], timeout=10, env=environment)
        results.append(dict(queued, scenario="queued cancellation and same-engine recovery"))
        assert queued.get("success") and requested_paths.count("/queued") == 1, (queued, requested_paths)
        direct, path = fetch("file", "direct.zip")
        assert direct.get("success"), direct
        assert direct["sha256"] == digest
        assert inspect_download(path, len(body), digest, "zip")["valid"]
        inherited, _ = fetch("file", "environment.zip", "environment")
        assert not inherited.get("success"), inherited
        html, path = fetch("html", "html.zip")
        assert html.get("success"), html
        assert not inspect_download(path, len(body), digest, "zip")["valid"]
        truncated, _ = fetch("truncated", "truncated.zip")
        assert not truncated.get("success"), truncated
        stalled, _ = fetch("stalled", "stalled.zip", budget=1)
        assert not stalled.get("success") and stalled["wall_seconds"] < 3, stalled
        delayed, _ = fetch("delayed", "delayed.zip", budget=1)
        assert not delayed.get("success") and delayed["wall_seconds"] < 6, delayed
        result = {"status": "passed", "scenarios": results, "external_network_used": False}
    except Exception as error:
        result = {"status": "failed", "error": str(error), "scenarios": results}
    finally:
        release.set()
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)
    (output / "results.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"{result['status'].upper()}; artifacts: {output}")
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
