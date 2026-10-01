# 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env python3
"""子进程调用、Python 命令查找、Windows 路径转换。"""

from __future__ import annotations

import os
import platform
import shutil
import re
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
_CONSOLE_OUTPUT_LIMIT = 12000


def resolve_parallel_jobs() -> str:
    """自适应并行度：CI 环境用全部核心，本地限制在合理上限。"""
    cpu = os.cpu_count() or 4
    # CI runner 通常核心少（2-4），直接用全部；本地高性能机器限制上限避免 OOM
    if os.environ.get("CI"):
        return str(cpu)
    return str(min(cpu, 14))


def is_windows_env() -> bool:
    return (
        platform.system().startswith("MINGW")
        or platform.system().startswith("MSYS")
        or platform.system().startswith("CYGWIN")
    )


def is_windows_host() -> bool:
    if is_windows_env():
        return True
    return shutil.which("cmd.exe") is not None


def to_windows_path(path: str) -> str:
    # WSL /mnt/X/...
    m = re.match(r"^/mnt/([A-Za-z])/(.*)$", path)
    if m:
        tail = m.group(2).replace("/", "\\")
        return f"{m.group(1).upper()}:\\{tail}"
    # /X/...
    m = re.match(r"^/([A-Za-z])/(.*)$", path)
    if m:
        tail = m.group(2).replace("/", "\\")
        return f"{m.group(1).upper()}:\\{tail}"
    if shutil.which("wslpath"):
        try:
            return subprocess.check_output(["wslpath", "-w", path], text=True).strip()
        except subprocess.CalledProcessError:
            pass
    if shutil.which("cygpath"):
        try:
            return subprocess.check_output(["cygpath", "-w", path], text=True).strip()
        except subprocess.CalledProcessError:
            pass
    return path


def _uses_windows_paths(py_cmd: str) -> bool:
    return bool(re.search(r"py\.exe|python\.exe|^[A-Za-z]:[/\\]", py_cmd))


def _is_windowsapps_stub(path: str) -> bool:
    normalized = path.replace("/", "\\").lower()
    return (
        "windowsapps\\python.exe" in normalized
        or "windowsapps\\python3.exe" in normalized
    )


def _is_working_python(path: str) -> bool:
    try:
        proc = subprocess.run(
            [path, "-c", "import sys; print(sys.executable)"],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=10,
        )
    except (OSError, subprocess.SubprocessError):
        return False
    return proc.returncode == 0


def resolve_python_cmd() -> str | None:
    candidates: list[str] = []

    if sys.executable:
        candidates.append(sys.executable)

    for env_name in ("PYTHON", "PYTHON3"):
        value = os.environ.get(env_name, "").strip()
        if value:
            candidates.append(value)

    for scoop_candidate in (
        str(Path.home() / "scoop" / "apps" / "python" / "current" / "python.exe"),
        r"D:\Scoop\apps\python\current\python.exe",
    ):
        candidates.append(scoop_candidate)

    for name in ("python", "python.exe", "py.exe", "py"):
        cmd = shutil.which(name)
        if cmd:
            candidates.append(cmd)

    seen: set[str] = set()
    for candidate in candidates:
        if not candidate:
            continue
        normalized = candidate.replace("/", "\\").lower()
        if normalized in seen or _is_windowsapps_stub(candidate):
            continue
        seen.add(normalized)
        if _is_working_python(candidate):
            return candidate

    if is_windows_host() and shutil.which("where.exe"):
        for name in ("python", "python.exe", "py", "py.exe"):
            try:
                out = subprocess.check_output(
                    ["where.exe", name], text=True, stderr=subprocess.DEVNULL
                )
                for line in out.strip().splitlines():
                    if (
                        line
                        and not _is_windowsapps_stub(line)
                        and _is_working_python(line)
                    ):
                        return line
            except (subprocess.CalledProcessError, IndexError):
                pass
    return None


def resolve_cmake_command() -> str | None:
    if (
        is_windows_host()
        and (REPO_ROOT / "qmclient_scripts" / "cmake-windows.cmd").exists()
    ):
        return "cmd.exe"
    cmake = shutil.which("cmake")
    return cmake if cmake else None


def run_python(
    *args: str,
    title: str = "",
    fail_level: str = "FAIL",
    dry_run: bool = False,
    check: bool = True,
) -> tuple[int, str]:
    py = resolve_python_cmd()
    if not py:
        return (1, "未找到可用的 Python")
    cmd = [py]
    if re.search(r"py\.exe|py$", py, re.IGNORECASE) and not py.lower().endswith(
        "python.exe"
    ):
        cmd.append("-3")
    for arg in args:
        if _uses_windows_paths(py) and (
            arg.startswith("/") or re.match(r"^/mnt/[A-Za-z]/", arg)
        ):
            cmd.append(to_windows_path(arg))
        else:
            cmd.append(arg)
    return run(cmd, title=title, fail_level=fail_level, dry_run=dry_run, check=check)


def run(
    cmd: list[str],
    title: str = "",
    fail_level: str = "FAIL",
    dry_run: bool = False,
    check: bool = True,
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
    stdin=None,
) -> tuple[int, str]:
    _prepare_console_output()
    if title:
        print(f"\n==> {title}")
    print(f"命令: {' '.join(cmd)}")
    if dry_run:
        return (0, "DryRun，仅展示命令")

    merged_env = {**os.environ, **env} if env else None
    proc = subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        cwd=str(cwd) if cwd else None,
        env=merged_env,
        stdin=stdin,
    )
    output = (proc.stdout or "") + (proc.stderr or "")
    # 过滤 showincludes 行
    lines = [
        line for line in output.splitlines() if not line.startswith("注意: 包含文件:")
    ]
    filtered = "\n".join(lines)
    if filtered:
        print(_truncate_console_output(filtered))

    if proc.returncode != 0:
        return (
            proc.returncode,
            f"{title or '命令'} 失败，退出码 {proc.returncode}\n{filtered}",
        )

    if check:
        # 检查 warning 文本（只对需要 fail_on_warnings 的调用方生效）
        pass

    return (0, filtered)


def _truncate_console_output(output: str, limit: int = _CONSOLE_OUTPUT_LIMIT) -> str:
    if len(output) <= limit:
        return output
    head_limit = int(limit * 0.7)
    tail_limit = limit - head_limit
    return (
        output[:head_limit].rstrip()
        + "\n\n... [output truncated by gate runner] ...\n\n"
        + output[-tail_limit:].lstrip()
    )


def _prepare_console_output() -> None:
    # GBK 控制台无法表示替换字符时，只降级显示，保留返回值中的完整诊断。
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")


# 保持与旧脚本的 print_section 兼容
def print_section(name: str) -> None:
    _prepare_console_output()
    print(f"\n==> {name}")


def print_result(level: str, message: str) -> None:
    _prepare_console_output()
    print(f"[{level}] {message}")
