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
import socketserver
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
    segment_body = b"PK\x03\x04" + bytes(range(256)) * (16 * 1024)
    segment_requests: list[tuple[str, str | None]] = []
    segment_lock = threading.Lock()
    segment_barrier = threading.Barrier(4)
    stalled_segment_release = threading.Event()
    stalled_segment_attempts = 0
    socks_connections: list[int] = []

    class SocksHandler(socketserver.BaseRequestHandler):
        def read_exact(self, size: int) -> bytes:
            result = b""
            while len(result) < size:
                data = self.request.recv(size - len(result))
                if not data:
                    raise ConnectionError("incomplete SOCKS fixture handshake")
                result += data
            return result

        def handle(self):
            self.request.settimeout(5)
            version, methods = self.read_exact(2)
            assert version == 5 and 0 in self.read_exact(methods)
            self.request.sendall(b"\x05\x00")
            version, command, _, address_type = self.read_exact(4)
            assert version == 5 and command == 1
            if address_type == 1:
                self.read_exact(4)
            elif address_type == 3:
                self.read_exact(self.read_exact(1)[0])
            else:
                self.read_exact(16)
            self.read_exact(2)
            socks_connections.append(1)
            self.request.sendall(b"\x05\x00\x00\x01\x7f\x00\x00\x01\x00\x00")
            request = b""
            while b"\r\n\r\n" not in request:
                request += self.read_exact(1)
            self.request.sendall(f"HTTP/1.1 200 OK\r\nContent-Length: {len(body)}\r\nConnection: close\r\n\r\n".encode() + body)

    socks_server = socketserver.ThreadingTCPServer(("127.0.0.1", 0), SocksHandler)
    socks_server.daemon_threads = True
    socks_thread = threading.Thread(target=socks_server.serve_forever, daemon=True)
    socks_thread.start()

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *arguments):
            pass

        def do_GET(self):
            nonlocal stalled_segment_attempts
            requested_paths.append(self.path)
            if self.path.startswith("/segment-"):
                requested_range = self.headers.get("Range")
                with segment_lock:
                    segment_requests.append((self.path, requested_range))
                payload = segment_body
                if self.path == "/segment-7z":
                    payload = b"7z\xbc\xaf\x27\x1c" + payload[6:]
                elif self.path == "/segment-exe":
                    payload = b"MZ" + payload[2:]
                first, last = 0, len(payload) - 1
                partial = requested_range is not None and self.path != "/segment-ignore"
                if partial:
                    first, last = map(int, requested_range.removeprefix("bytes=").split("-"))
                    if first != last and self.path == "/segment-zip":
                        try:
                            segment_barrier.wait(timeout=3)
                        except threading.BrokenBarrierError:
                            pass
                stall = False
                if self.path == "/segment-stall" and partial and first == 0 and last > first:
                    with segment_lock:
                        stalled_segment_attempts += 1
                        stall = stalled_segment_attempts == 1
                    if not stall:
                        stalled_segment_release.set()
                status = 206 if partial else 200
                if self.path == "/segment-limited" and first != last:
                    status = 429
                self.send_response(status)
                if status == 429:
                    self.send_header("Retry-After", "300")
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                if partial:
                    range_first = first + 1 if self.path == "/segment-wrong" and first != last else first
                    self.send_header("Content-Range", f"bytes {range_first}-{last}/{len(payload)}")
                self.send_header("Content-Length", str(last - first + 1))
                self.end_headers()
                try:
                    result = payload[first:last + 1]
                    if stall:
                        self.wfile.write(result[:1])
                        self.wfile.flush()
                        stalled_segment_release.wait(25)
                        result = result[1:]
                    if self.path == "/segment-truncated" and first != last:
                        result = result[:len(result) // 2]
                    self.wfile.write(result)
                    self.wfile.flush()
                    if self.path == "/segment-truncated" and first != last:
                        self.connection.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                return
            if self.path == "/delayed":
                release.wait(5)
            payload = b"<html>gateway error</html>" if self.path == "/html" else body
            if self.path in ("/sample-large", "/sample-no-length"):
                payload = b"PK\x03\x04" + b"p" * (4 * 1024 * 1024)
            self.send_response(429 if self.path == "/rate-limited" else 200)
            if self.path == "/rate-limited":
                self.send_header("Retry-After", "300")
            if self.path != "/sample-no-length":
                self.send_header("Content-Length", str(len(payload) * 2 if self.path == "/truncated" else len(payload)))
            else:
                self.send_header("Connection", "close")
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
        def fetch(route: str, name: str, mode="direct", budget=5, env=None):
            path = output / name
            result = run_probe(probe, ["fetch", f"http://127.0.0.1:{server.server_port}/{route}", path.relative_to(REPO_ROOT).as_posix(), mode, str(budget), "1048576"], timeout=budget + 10, env=environment if env is None else env)
            result["scenario"] = name
            results.append(result)
            return result, path

        queued = run_probe(probe, ["cancel-queued", f"http://127.0.0.1:{server.server_port}/queued"], timeout=10, env=environment)
        results.append(dict(queued, scenario="queued cancellation and same-engine recovery"))
        assert queued.get("success") and requested_paths.count("/queued") == 1, (queued, requested_paths)
        for route in ("zip", "7z", "exe", "ignore", "wrong", "truncated", "limited", "stall"):
            path = output / f"segmented-{route}.tmp"
            budget = "35" if route == "stall" else "15"
            downloaded = run_probe(probe, ["segmented", f"http://127.0.0.1:{server.server_port}/segment-{route}", path.relative_to(REPO_ROOT).as_posix(), budget], timeout=40, env=environment)
            results.append(dict(downloaded, scenario=f"segmented-{route}"))
            if route in ("wrong", "truncated", "limited"):
                assert not downloaded.get("success") and not path.exists(), downloaded
                assert downloaded["requests"] <= 9, downloaded
                if route == "limited":
                    assert downloaded["http_status"] == 429, downloaded
                continue
            assert downloaded.get("success"), downloaded
            expected = segment_body
            if route == "7z":
                expected = b"7z\xbc\xaf\x27\x1c" + expected[6:]
            elif route == "exe":
                expected = b"MZ" + expected[2:]
            assert path.read_bytes() == expected
            assert downloaded["sha256"] == hashlib.sha256(expected).hexdigest()
            expected_requests = 2 if route == "ignore" else 6 if route == "stall" else 5
            assert downloaded["requests"] == expected_requests, downloaded
            if route == "stall":
                assert stalled_segment_attempts == 2, downloaded
        assert not segment_barrier.broken, "four package segments did not run concurrently"
        for route in ("sample-large", "sample-no-length"):
            sample = run_probe(probe, ["sample", f"http://127.0.0.1:{server.server_port}/{route}"], timeout=12, env=environment)
            results.append(dict(sample, scenario=route))
            assert sample.get("success") and sample["sample_bytes"] == 256 * 1024 and sample["zip_prefix"], sample
        limited = run_probe(probe, ["sample", f"http://127.0.0.1:{server.server_port}/rate-limited"], timeout=12, env=environment)
        results.append(dict(limited, scenario="sample rate limit"))
        assert not limited.get("success") and limited["http_status"] == 429, limited
        direct, path = fetch("file", "direct.zip")
        assert direct.get("success"), direct
        assert not direct["used_proxy"], direct
        assert direct["sha256"] == digest
        assert inspect_download(path, len(body), digest, "zip")["valid"]
        inherited, _ = fetch("file", "environment.zip", "environment")
        assert not inherited.get("success"), inherited
        assert inherited["proxy_route_selected"] and inherited["official_direct_retry_allowed"], inherited
        http_environment = dict(environment)
        http_environment.update(http_proxy=f"http://127.0.0.1:{server.server_port}", HTTP_PROXY=f"http://127.0.0.1:{server.server_port}")
        proxied, _ = fetch("file", "http-proxy.zip", "environment", env=http_environment)
        assert proxied.get("success") and proxied["used_proxy"] and proxied["sha256"] == digest, proxied
        bypass_environment = dict(http_environment)
        bypass_environment.update(NO_PROXY="127.0.0.1", no_proxy="127.0.0.1")
        bypassed, _ = fetch("file", "proxy-bypass.zip", "environment", env=bypass_environment)
        assert bypassed.get("success") and not bypassed["used_proxy"], bypassed
        socks_environment = dict(environment)
        for name in ("http_proxy", "HTTP_PROXY", "https_proxy", "HTTPS_PROXY"):
            socks_environment.pop(name, None)
        socks_url = f"socks5h://127.0.0.1:{socks_server.server_address[1]}"
        socks_environment.update(all_proxy=socks_url, ALL_PROXY=socks_url)
        socks, _ = fetch("file", "socks-proxy.zip", "environment", env=socks_environment)
        assert socks.get("success") and socks["used_proxy"] and socks["sha256"] == digest and socks_connections, socks
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
        stalled_segment_release.set()
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)
        socks_server.shutdown()
        socks_server.server_close()
        socks_thread.join(timeout=2)
    (output / "results.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"{result['status'].upper()}; artifacts: {output}")
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
