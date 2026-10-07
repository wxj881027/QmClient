#!/usr/bin/env python3
"""显式联网验收：串行完整下载，复用生产探针，保留原始证据；不加入离线 gate。"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
import uuid

REPO_ROOT = Path(__file__).resolve().parents[2]
API = "https://api.github.com/repos/wxj881027/QmClient/releases/latest"
MARKER = "QM_UPDATE_PROBE_JSON "


def run_probe(probe: Path, arguments: list[str], *, timeout: float, env: dict[str, str] | None = None) -> dict:
    start = time.monotonic()
    try:
        process = subprocess.run([str(probe), *arguments], cwd=REPO_ROOT, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout, check=False)
    except subprocess.TimeoutExpired:
        return {"success": False, "error": "probe process exceeded explicit acceptance budget", "wall_seconds": time.monotonic() - start}
    offset = process.stdout.find(MARKER)
    if offset < 0:
        return {"success": False, "error": "probe returned no structured result", "returncode": process.returncode, "stderr": process.stderr[-4000:], "wall_seconds": time.monotonic() - start}
    result = json.loads(process.stdout[offset + len(MARKER):])
    result.update(returncode=process.returncode, stderr=process.stderr[-4000:], diagnostics=process.stdout[:offset][-4000:], wall_seconds=time.monotonic() - start)
    return result


def service_groups(sources: list[dict], resource: int) -> list[dict]:
    groups, urls, selected = set(), set(), []
    for source in sorted(sources, key=lambda item: item["priority"]):
        if not source["enabled"] or not source["resources"] & resource or source["group"] in groups or source["prefix"] in urls:
            continue
        groups.add(source["group"])
        urls.add(source["prefix"])
        selected.append(source)
    return selected


def inspect_download(path: Path, expected_size: int, expected_hash: str, kind: str) -> dict:
    if expected_size < 1 or len(expected_hash) != 64 or any(char not in "0123456789abcdef" for char in expected_hash):
        return {"valid": False, "error": "invalid expected integrity metadata"}
    try:
        with path.open("rb") as stream:
            prefix = stream.read(256)
            stream.seek(0)
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        size = path.stat().st_size
    except OSError as error:
        return {"valid": False, "error": str(error)}
    magic_valid = prefix.startswith(b"PK\x03\x04") if kind == "zip" else prefix.startswith(b"7z\xbc\xaf\x27\x1c")
    valid = magic_valid and size == expected_size and digest == expected_hash
    return {"valid": valid, "magic_valid": magic_valid, "size": size, "sha256": digest, "error": "" if valid else "file format, size or SHA-256 mismatch"}


class LiveRun:
    def __init__(self, probe: Path, output: Path, budget: float, only: set[str] | None = None):
        self.probe, self.output, self.budget = probe, output, budget
        self.only = only or set()
        self.results: dict = {"date": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "environment": "current user machine; no mainland ISP claim", "cases": [], "surveys": [], "metadata": [], "limitations": ["Single sequential samples are not a controlled throughput benchmark.", "Old unsigned 7z has no independent authenticity guarantee.", "No installed application is modified or launched."]}
        output.mkdir(parents=True, exist_ok=False)
        self.save()

    def save(self):
        (self.output / "results.json").write_text(json.dumps(self.results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    def fetch(self, url: str, name: str, mode: str, max_bytes: int, budget: float | None = None) -> tuple[dict, Path]:
        path = self.output / name
        relative = path.relative_to(REPO_ROOT).as_posix()
        seconds = budget or self.budget
        print(f"Downloading {name}: {mode}, {url}", flush=True)
        result = run_probe(self.probe, ["fetch", url, relative, mode, str(seconds), str(max_bytes)], timeout=seconds + 25)
        print(f"  success={result.get('success')} seconds={result.get('seconds')} MiB/s={result.get('mib_per_second')} status={result.get('http_status')}", flush=True)
        return result, path

    def fetch_small(self, url: str, name: str, sources: list[dict], max_bytes: int) -> Path:
        candidates = [(url, "system"), *[(source["prefix"] + url, "direct") for source in sources], (url, "direct")]
        for index, (candidate, mode) in enumerate(candidates):
            result, path = self.fetch(candidate, f"{name}-{index}", mode, max_bytes, 25)
            self.results["metadata"].append(result)
            self.save()
            if result.get("success"):
                if url == API:
                    validation = run_probe(self.probe, ["release", str(path)], timeout=10)
                    if not validation.get("valid"):
                        result.update(success=False, content_error="production Release parser rejected response")
                        self.save()
                        continue
                return path
        raise RuntimeError(f"all metadata sources failed: {name}")

    def run(self) -> dict:
        sources = run_probe(self.probe, ["sources"], timeout=10)["sources"]
        release_sources, api_sources = service_groups(sources, 1), service_groups(sources, 4)
        release_path = self.fetch_small(API, "release.json", api_sources, 4 * 1024 * 1024)
        parsed = run_probe(self.probe, ["release", str(release_path)], timeout=10)
        if not parsed.get("valid"):
            raise RuntimeError(f"production Release parser rejected metadata: {parsed}")
        self.results["release"] = parsed
        release = json.loads(release_path.read_text(encoding="utf-8"))
        assets = {asset["name"]: asset for asset in release["assets"]}
        package_asset = next(asset for asset in release["assets"] if asset["browser_download_url"] == parsed["package_url"])
        manifest = self.fetch_small(parsed["manifest_url"], "manifest.json", release_sources, 32 * 1024 * 1024)
        manifest_sig = self.fetch_small(parsed["manifest_signature_url"], "manifest.sig", release_sources, 64)
        package_sig = self.fetch_small(parsed["package_signature_url"], "package.sig", release_sources, 64)
        # 大包下载前先验证清单与摘要签名，避免为恶意元数据消耗流量。
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
        from qmclient_scripts.sign_update_release import EXPECTED_PUBLIC_KEY, PACKAGE_SIGNATURE_CONTEXT
        key = Ed25519PublicKey.from_public_bytes(EXPECTED_PUBLIC_KEY)
        key.verify(manifest_sig.read_bytes(), manifest.read_bytes())
        signed = json.loads(manifest.read_text(encoding="utf-8"))["package"]
        key.verify(package_sig.read_bytes(), PACKAGE_SIGNATURE_CONTEXT + bytes.fromhex(signed["sha256"]))
        if signed["name"] != package_asset["name"] or signed["size"] != package_asset["size"]:
            raise RuntimeError("signed package does not match current Release")
        self.results["signed_metadata_verified"] = True
        for source in release_sources:
            result, _ = self.fetch(source["prefix"] + parsed["package_signature_url"], f"survey-{source['group']}.sig", "direct", 64, 15)
            result.update(group=source["group"], prefix=source["prefix"])
            result["reachable"] = bool(result.get("success")) and int(float(result.get("bytes", 0))) == 64
            self.results["surveys"].append(result)
            self.save()
        reachable = sorted((item for item in self.results["surveys"] if item["reachable"]), key=lambda item: float(item["seconds"]))
        # 官方代理/直连都实测，即使首个模式失败也继续其他模式。
        matrix = [("official-system", "", "system"), ("official-direct", "", "direct")]
        matrix.extend((item["group"] + "-direct", item["prefix"], "direct") for item in reachable)
        for label, prefix, mode in matrix:
            if self.only and label not in self.only:
                continue
            result, path = self.fetch(prefix + parsed["package_url"], label + "-" + signed["name"], mode, signed["size"] + 1)
            result.update(label=label, kind="signed-full-package")
            if result.get("success"):
                kind = "7z" if parsed["sevenzip"] else "zip"
                result["integrity"] = inspect_download(path, signed["size"], signed["sha256"], kind)
                result["signature"] = run_probe(self.probe, ["verify", str(path), str(package_sig), str(manifest), str(manifest_sig)], timeout=120)
                result["verified"] = result["integrity"]["valid"] and result["signature"].get("valid", False)
            else:
                result["verified"] = False
            self.results["cases"].append(result)
            self.save()
        # 现有未签名 7z 只测官方 digest 一致性，不能列为可安装包。
        sevenzip = assets.get("QmClient-windows.7z")
        if sevenzip and not parsed["sevenzip"] and sevenzip.get("digest", "").startswith("sha256:") and (not self.only or "legacy-7z-best-reachable-direct" in self.only):
            prefix = reachable[0]["prefix"] if reachable else ""
            result, path = self.fetch(prefix + sevenzip["browser_download_url"], "legacy-unsigned-7z.7z", "direct", sevenzip["size"] + 1)
            result.update(label="legacy-7z-best-reachable-direct", kind="unsigned-7z", authenticity_verified=False)
            if result.get("success"):
                result["integrity"] = inspect_download(path, sevenzip["size"], sevenzip["digest"][7:], "7z")
                result["verified"] = result["integrity"]["valid"]
            else:
                result["verified"] = False
            self.results["cases"].append(result)
        self.results["skipped_sources"] = [{"group": item["group"], "reason": "small signature probe failed; full package not attempted", "diagnostics": item.get("diagnostics", "")} for item in self.results["surveys"] if not item["reachable"]]
        self.results["status"] = "passed" if self.results["cases"] and all(item["verified"] for item in self.results["cases"]) else "partial"
        self.results["missing_release_paths"] = [name for name in ("QmClient-Setup.exe", "QmClient-windows-portable.7z", "QmClient-windows-portable.zip") if name not in assets]
        self.save()
        return self.results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--only", default="", help="逗号分隔的测试标签，复测失败场景时避免重复下载已通过大包")
    parser.add_argument("--budget", type=float, default=300, help="每次验收下载的硬等待预算；不是客户端大包总超时")
    args = parser.parse_args()
    if not 1 <= args.budget <= 3600:
        parser.error("budget must be between 1 and 3600 seconds")
    output = REPO_ROOT / "tmp/update-live" / ("run_" + uuid.uuid4().hex[:12])
    run = LiveRun(args.probe.resolve(strict=True), output, args.budget, {label for label in args.only.split(",") if label})
    try:
        result = run.run()
    except Exception as error:
        run.results.update(status="failed", error=str(error))
        run.save()
        print(f"FAIL: {error}; artifacts: {output}", flush=True)
        return 1
    print(f"{result['status'].upper()}; artifacts: {output}", flush=True)
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
