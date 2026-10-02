"""Windows CMake 构建事务：目录互斥、依赖修复和日志留存。"""

from __future__ import annotations

from contextlib import contextmanager, nullcontext
import errno
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

if __package__:
    from . import repair_ninja_msvc_prefix as repair
else:
    import repair_ninja_msvc_prefix as repair

REPO_ROOT = Path(__file__).resolve().parents[1]


@contextmanager
def build_directory_lock(build_dir: Path, *, timeout: float = 600):
    # 规范化目录后使用同一把系统锁，覆盖相对路径、大小写和路径别名。
    directory = build_dir.resolve()
    identity = os.path.normcase(str(directory))
    lock_dir = REPO_ROOT / "tmp" / "build-locks"
    lock_dir.mkdir(parents=True, exist_ok=True)
    lock_file = lock_dir / (hashlib.sha256(identity.encode("utf-8")).hexdigest() + ".lock")
    with lock_file.open("a+b") as handle:
        deadline = time.monotonic() + timeout
        waiting = False
        while True:
            try:
                if os.name == "nt":
                    import msvcrt
                    handle.seek(0)
                    msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except OSError as error:
                if error.errno not in (errno.EACCES, errno.EAGAIN, errno.EDEADLK):
                    raise
                if not waiting:
                    print(f"Waiting for build directory: {directory}", flush=True)
                    waiting = True
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(f"Timed out waiting for build directory: {directory}") from error
                time.sleep(min(0.1, remaining))
        try:
            yield
        finally:
            if os.name == "nt":
                handle.seek(0)
                msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(handle, fcntl.LOCK_UN)


def command_build_dir(argv: list[str]) -> Path | None:
    directory = repair.find_build_dir(argv)
    if directory is not None:
        return directory.resolve()
    mode = argv[0].split("=", 1)[0] if argv else ""
    if mode in ("--install", "--open"):
        target = argv[0].split("=", 1)[1] if "=" in argv[0] else (argv[1] if len(argv) > 1 else None)
        return Path(target).resolve() if target else None
    if argv and argv[0] in ("-E", "-P", "--workflow", "--version", "--help", "--help-full"):
        return None
    # CMake 省略 -B 时使用当前目录配置；读取帮助等命令不参与构建事务。
    if not argv or any(arg.startswith("--help") for arg in argv):
        return None
    return Path.cwd().resolve()


def repair_msvc_rules(build_dir: Path) -> bool:
    rules = build_dir / "CMakeFiles" / "rules.ninja"
    if not rules.is_file() or b"deps = msvc" not in rules.read_bytes():
        return False
    return repair.repair_build_dir(build_dir)


def run(cmake: str, argv: list[str]) -> int:
    directory = command_build_dir(argv)
    transaction = build_directory_lock(directory) if directory is not None else nullcontext()
    mode = argv[0].split("=", 1)[0] if argv else ""
    build = mode == "--build"
    configure = bool(mode and mode not in ("-E", "-P", "--install", "--open", "--workflow", "--version", "--build") and not mode.startswith("--help"))
    command = [cmake, *(["-Wno-dev"] if configure else []), *argv]
    with transaction:
        if build:
            repair_msvc_rules(directory)
        log_dir = REPO_ROOT / "tmp"
        log_dir.mkdir(parents=True, exist_ok=True)
        output = tempfile.NamedTemporaryFile(prefix="cmake-windows-", suffix=".log", dir=log_dir, delete=False)
        log = Path(output.name)
        try:
            with output:
                result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, check=False)
                code = result.returncode
                if code == 0 and directory is not None and (build or configure):
                    changed = repair_msvc_rules(directory)
                    if build and changed:
                        # CMake 重生成规则后补一轮，让丢失的依赖在锁内恢复。
                        result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, check=False)
                        code = result.returncode
                        if code == 0 and repair_msvc_rules(directory):
                            raise RuntimeError(f"Ninja MSVC dependency prefix did not remain stable after rebuild; log: {log}")
        finally:
            # 修复异常时也显示已完成构建的输出，不丢失原始编译诊断。
            filtered = subprocess.run([sys.executable, str(REPO_ROOT / "qmclient_scripts" / "cmake-windows-filter.py"), str(log)], check=False)
            if filtered.returncode != 0:
                sys.stdout.buffer.write(log.read_bytes())

        return code


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="replace")
    if len(sys.argv) < 3:
        print("Usage: cmake_windows.py <cmake executable> <cmake arguments>", file=sys.stderr)
        return 1
    try:
        return run(sys.argv[1], sys.argv[2:])
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Windows CMake build failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
