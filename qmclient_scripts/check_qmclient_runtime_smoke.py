#!/usr/bin/env python3
"""运行当前工作区客户端并校验自动 diagnostics session/report。"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BUILD_DIR = REPO_ROOT / "cmake-build-release"


def fail(message: str) -> int:
    print(f"runtime smoke failed: {message}", file=sys.stderr)
    return 1


def hide_process_windows(process_id: int) -> None:
    if os.name != "nt":
        return

    user32 = ctypes.windll.user32
    callback_type = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

    @callback_type
    def hide_window(hwnd, _):
        window_process_id = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(window_process_id))
        if window_process_id.value == process_id:
            user32.ShowWindow(hwnd, 0)  # SW_HIDE
        return True

    user32.EnumWindows(hide_window, 0)


def run_hidden_client(command: list[str], run_dir: Path, timeout: float):
    startup_info = None
    creation_flags = 0
    if os.name == "nt":
        startup_info = subprocess.STARTUPINFO()
        startup_info.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup_info.wShowWindow = subprocess.SW_HIDE
        creation_flags = subprocess.CREATE_NO_WINDOW

    process = subprocess.Popen(
        command,
        cwd=run_dir,
        env=os.environ.copy(),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
        startupinfo=startup_info,
        creationflags=creation_flags,
    )
    deadline = time.monotonic() + timeout
    while process.poll() is None:
        hide_process_windows(process.pid)
        if time.monotonic() >= deadline:
            process.kill()
            output, _ = process.communicate()
            return None, f"客户端超过 {timeout:.1f}s 未退出\n{output[-2000:]}"
        time.sleep(0.02)
    output, _ = process.communicate()
    return process.returncode, output


def run_client(
    executable: Path,
    data_dir: Path,
    run_dir: Path,
    user_dir: Path,
    config: str,
    timeout: float,
    base_config: Path | None,
):
    run_dir.mkdir(exist_ok=True)
    user_dir.mkdir()
    if base_config is not None:
        shutil.copy2(base_config, user_dir / "settings_ddnet.cfg")
    (run_dir / "storage.cfg").write_text(
        f"add_path {user_dir.as_posix()}\n"
        f"add_path {data_dir.as_posix()}\n",
        encoding="utf-8",
    )
    config_file = run_dir / "smoke.cfg"
    config_file.write_text(config, encoding="utf-8")
    return_code, output = run_hidden_client(
        [str(executable), "-s", "-f", str(config_file)], run_dir, timeout
    )
    if return_code is None:
        return None, output
    if return_code != 0:
        return None, f"客户端退出码为 {return_code}\n{output[-2000:]}"
    return return_code, None


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
    parser.add_argument(
        "--base-config",
        type=Path,
        default=None,
        help="复制到临时用户目录的初始化 settings_ddnet.cfg；未指定时自动使用 APPDATA/DDNet/settings_ddnet.cfg（如果存在）",
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

    base_config = args.base_config
    if base_config is None:
        appdata = os.environ.get("APPDATA")
        if appdata:
            candidate = Path(appdata) / "DDNet" / "settings_ddnet.cfg"
            if candidate.is_file():
                base_config = candidate
    elif not base_config.is_file():
        return fail(f"找不到初始化配置: {base_config}")
    if base_config is not None:
        base_config = base_config.resolve()

    with tempfile.TemporaryDirectory(prefix="qmclient-runtime-smoke-") as temp_name:
        temp_dir = Path(temp_name)
        user_dir = temp_dir / "user"
        benchmark_file = temp_dir / "benchmark.csv"
        _, error = run_client(
            executable,
            data_dir,
            temp_dir,
            user_dir,
            "gfx_backend OpenGL\n"
            "gfx_fullscreen 0\n"
            "gfx_vsync 0\n"
            "dbg_gfx 0\n"
            "qm_diagnostics 1\n"
            f"benchmark_quit 2 {benchmark_file.as_posix()}\n",
            args.timeout,
            base_config,
        )
        if error:
            return fail(error)

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

        disabled_dir = temp_dir / "disabled"
        disabled_user_dir = disabled_dir / "user"
        disabled_benchmark_file = disabled_dir / "benchmark.csv"
        _, error = run_client(
            executable,
            data_dir,
            disabled_dir,
            disabled_user_dir,
            "gfx_backend OpenGL\n"
            "gfx_fullscreen 0\n"
            "gfx_vsync 0\n"
            "dbg_gfx 0\n"
            "qm_diagnostics 0\n"
            f"benchmark_quit 2 {disabled_benchmark_file.as_posix()}\n",
            args.timeout,
            base_config,
        )
        if error:
            return fail(f"diagnostics 关闭路径失败: {error}")
        disabled_diagnostics_dir = disabled_user_dir / "qmclient" / "diagnostics"
        if disabled_diagnostics_dir.exists():
            return fail("qm_diagnostics=0 时仍创建了 diagnostics 目录")
        if not disabled_benchmark_file.is_file() or disabled_benchmark_file.stat().st_size == 0:
            return fail("diagnostics 关闭路径 benchmark 文件缺失或为空")

        print(
            "QmClient runtime smoke passed: "
            f"session_lines={len(session_events)}, "
            f"backend_config={report['backend_config']}, "
            f"active_api_name={report['active_api_name']}, "
            "write_failed=false, report_tmp=0, diagnostics_disabled=no_files, "
            f"base_config={base_config if base_config is not None else 'none'}"
        )
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
