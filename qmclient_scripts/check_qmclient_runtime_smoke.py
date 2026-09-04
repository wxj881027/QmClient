#!/usr/bin/env python3
"""运行当前工作区客户端并校验自动 diagnostics session/report。"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUILD_DIR = REPO_ROOT / "cmake-build-release"


def fail(message: str) -> int:
    print(f"runtime smoke failed: {message}", file=sys.stderr)
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=DEFAULT_BUILD_DIR,
        help="当前工作区的构建目录，默认是 cmake-build-release",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=30.0,
        help="客户端最大运行秒数",
    )
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    executable = build_dir / "DDNet.exe"
    data_dir = build_dir / "data"
    if not executable.is_file():
        return fail(f"找不到客户端: {executable}")
    if not data_dir.is_dir():
        return fail(f"找不到运行时 data 目录: {data_dir}")
    if args.timeout <= 0:
        return fail("--timeout 必须大于 0")

    with tempfile.TemporaryDirectory(prefix="qmclient-runtime-smoke-") as temp_name:
        temp_dir = Path(temp_name)
        user_dir = temp_dir / "user"
        user_dir.mkdir()
        (temp_dir / "storage.cfg").write_text(
            f"add_path {user_dir.as_posix()}\n"
            f"add_path {data_dir.as_posix()}\n",
            encoding="utf-8",
        )
        benchmark_file = temp_dir / "benchmark.csv"
        config_file = temp_dir / "smoke.cfg"
        config_file.write_text(
            "gfx_backend OpenGL\n"
            "gfx_fullscreen 0\n"
            "gfx_vsync 0\n"
            "dbg_gfx 0\n"
            f"benchmark_quit 2 {benchmark_file.as_posix()}\n",
            encoding="utf-8",
        )

        environment = os.environ.copy()
        try:
            completed = subprocess.run(
                [str(executable), "-s", "-f", str(config_file)],
                cwd=temp_dir,
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                errors="replace",
                timeout=args.timeout,
                check=False,
            )
        except subprocess.TimeoutExpired as error:
            output = error.stdout or ""
            return fail(f"客户端超过 {args.timeout:.1f}s 未退出\n{output[-2000:]}")

        if completed.returncode != 0:
            return fail(f"客户端退出码为 {completed.returncode}\n{completed.stdout[-2000:]}")

        diagnostics_dir = user_dir / "qmclient" / "diagnostics"
        sessions = sorted(diagnostics_dir.glob("session-*.jsonl"), key=lambda path: path.stat().st_mtime)
        reports = sorted(diagnostics_dir.glob("report-*.json"), key=lambda path: path.stat().st_mtime)
        temp_reports = list(diagnostics_dir.glob("report-*.tmp"))
        if len(sessions) != 1:
            return fail(f"预期 1 个 session，实际为 {len(sessions)}")
        if len(reports) != 1:
            return fail(f"预期 1 个 report，实际为 {len(reports)}")
        if temp_reports:
            return fail(f"发现未清理的 report 临时文件: {temp_reports}")

        try:
            session_events = [json.loads(line) for line in sessions[0].read_text(encoding="utf-8").splitlines()]
            report = json.loads(reports[0].read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            return fail(f"diagnostics JSON 无法读取或解析: {error}")

        if not any(event.get("type") == "session_end" for event in session_events):
            return fail("session 中缺少 session_end")
        if report.get("type") != "report":
            return fail(f"report type 不正确: {report.get('type')!r}")
        if report.get("write_failed") is not False:
            return fail(f"report write_failed 不是 false: {report.get('write_failed')!r}")
        if not report.get("backend_config") or not report.get("active_api_name"):
            return fail("report 缺少 backend_config 或 active_api_name")
        if not benchmark_file.is_file() or benchmark_file.stat().st_size == 0:
            return fail("benchmark 文件缺失或为空")

        print(
            "QmClient runtime smoke passed: "
            f"session_lines={len(session_events)}, "
            f"backend_config={report['backend_config']}, "
            f"active_api_name={report['active_api_name']}, "
            "write_failed=false, report_tmp=0"
        )
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
