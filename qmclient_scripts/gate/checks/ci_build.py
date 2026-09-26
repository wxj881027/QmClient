# 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env python3
"""CI 等价构建验证：clang-tidy 构建 + ASan/UBSan 构建。"""

from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

from lib import runner
from lib.report import ResultCollector

REPO_ROOT = Path(__file__).resolve().parents[3]


def _resolve_tool(name: str) -> str | None:
    resolved = shutil.which(name)
    if resolved:
        return resolved
    for versioned in (f"{name}-22", f"{name}-18", f"{name}-16"):
        resolved = shutil.which(versioned)
        if resolved:
            return resolved
    return None


def _run_command(
    title: str,
    cmd: list[str],
    cwd: Path | None = None,
    env: dict[str, str] | None = None,
    timeout: int | None = 1800,
) -> tuple[int, str]:
    runner.print_section(title)
    print(f"命令: {' '.join(cmd)}")
    proc = subprocess.Popen(
        cmd,
        cwd=str(cwd) if cwd else None,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    lines: list[str] = []
    assert proc.stdout is not None
    for line in proc.stdout:
        print(line, end="")
        lines.append(line)
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        return (
            -1,
            f"{title} 超时（>{timeout}s），已强制终止\n" + "".join(lines[-30:]),
        )
    return proc.returncode, "".join(lines)


def _run_clang_tidy_build(results: ResultCollector, dry_run: bool) -> None:
    title = "clang-tidy 构建检查"
    runner.print_section(title)

    clang = _resolve_tool("clang")
    clangxx = _resolve_tool("clang++")
    clang_tidy = _resolve_tool("clang-tidy")
    cmake = _resolve_tool("cmake")
    ninja = _resolve_tool("ninja")

    missing = []
    if not clang:
        missing.append("clang")
    if not clangxx:
        missing.append("clang++")
    if not clang_tidy:
        missing.append("clang-tidy")
    if not cmake:
        missing.append("cmake")
    if not ninja:
        missing.append("ninja")

    if missing:
        results.add(
            "WARN",
            title,
            f"缺少工具，跳过: {', '.join(missing)}",
        )
        return

    if dry_run:
        results.add("INFO", title, "DryRun，仅展示命令")
        return

    tool_path = (
        REPO_ROOT / "qmclient_scripts" / "gate" / "tools" / "clang_tidy_build.py"
    )
    returncode, output = _run_command(
        title,
        [os.sys.executable, str(tool_path)],
        cwd=REPO_ROOT,
    )
    if returncode != 0:
        tail = "\n".join(output.splitlines()[-50:])
        results.add("FAIL", title, f"失败，退出码 {returncode}\n{tail}")
    else:
        results.add("PASS", title, "构建通过")


def _run_sanitizer_build(results: ResultCollector, dry_run: bool) -> None:
    title = "ASan/UBSan 构建检查"
    runner.print_section(title)

    if os.name == "nt":
        results.add(
            "WARN",
            title,
            "Windows 不支持与 CI 等价的 ASan/UBSan 构建流程，跳过",
        )
        return

    clang = _resolve_tool("clang")
    clangxx = _resolve_tool("clang++")
    cmake = _resolve_tool("cmake")
    ninja = _resolve_tool("ninja")

    missing = []
    if not clang:
        missing.append("clang")
    if not clangxx:
        missing.append("clang++")
    if not cmake:
        missing.append("cmake")
    if not ninja:
        missing.append("ninja")

    if missing:
        results.add(
            "WARN",
            title,
            f"缺少工具，跳过: {', '.join(missing)}",
        )
        return

    if dry_run:
        results.add("INFO", title, "DryRun，仅展示命令")
        return

    build_dir = REPO_ROOT / "build-sanitizer-gate"
    if build_dir.exists():
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True, exist_ok=True)
    (build_dir / ".gate-marker").touch()

    san_flags = (
        "-fsanitize=address,undefined,leak "
        "-fsanitize-recover=all "
        "-fno-omit-frame-pointer "
        "-fno-common"
    )

    env = os.environ.copy()
    env["CC"] = clang
    env["CXX"] = clangxx
    env["CXXFLAGS"] = san_flags
    env["CFLAGS"] = san_flags
    env["UBSAN_OPTIONS"] = "log_path=SAN:print_stacktrace=1:halt_on_error=0"
    env["ASAN_OPTIONS"] = (
        "log_path=SAN:strict_string_checks=1:"
        "detect_stack_use_after_return=1:"
        "check_initialization_order=1:"
        "strict_init_order=1:"
        "detect_leaks=0:"
        "halt_on_error=0"
    )

    # 配置
    returncode, output = _run_command(
        "ASan/UBSan 配置",
        [
            cmake,
            "-G",
            "Ninja",
            f"-DCMAKE_MAKE_PROGRAM={ninja}",
            "-DCMAKE_BUILD_TYPE=Debug",
            "-DHEADLESS_CLIENT=ON",
            "-DVULKAN=OFF",
            "-DDOWNLOAD_GTEST=ON",
            "-DDEV=ON",
            "..",
        ],
        cwd=build_dir,
        env=env,
    )
    if returncode != 0:
        tail = "\n".join(output.splitlines()[-30:])
        results.add("FAIL", title, f"配置失败，退出码 {returncode}\n{tail}")
        return

    # 构建
    returncode, output = _run_command(
        "ASan/UBSan 构建",
        [cmake, "--build", ".", "--config", "Debug", "--target", "everything"],
        cwd=build_dir,
        env=env,
    )
    if returncode != 0:
        tail = "\n".join(output.splitlines()[-30:])
        results.add("FAIL", title, f"构建失败，退出码 {returncode}\n{tail}")
        return

    failed = False

    # 运行客户端
    client_rc, client_out = _run_command(
        "ASan/UBSan 客户端运行",
        ["./DDNet", "cl_download_skins 0;quit"],
        cwd=build_dir,
        env=env,
    )
    if client_rc != 0:
        tail = "\n".join(client_out.splitlines()[-20:])
        results.add("FAIL", "ASan/UBSan 客户端", f"退出码 {client_rc}\n{tail}")
        failed = True

    # 运行服务端
    server_rc, server_out = _run_command(
        "ASan/UBSan 服务端运行",
        ["./DDNet-Server", "shutdown"],
        cwd=build_dir,
        env=env,
    )
    if server_rc != 0:
        tail = "\n".join(server_out.splitlines()[-20:])
        results.add("FAIL", "ASan/UBSan 服务端", f"退出码 {server_rc}\n{tail}")
        failed = True

    # 运行测试
    test_rc, test_out = _run_command(
        "ASan/UBSan 单元测试",
        [cmake, "--build", ".", "--config", "Debug", "--target", "run_cxx_tests"],
        cwd=build_dir,
        env=env,
    )
    if test_rc != 0:
        tail = "\n".join(test_out.splitlines()[-20:])
        results.add("FAIL", "ASan/UBSan 单元测试", f"失败，退出码 {test_rc}\n{tail}")
        failed = True

    # 检查 SAN.* 文件
    san_files = sorted(build_dir.glob("SAN.*"))
    if san_files:
        details: list[str] = []
        for f in san_files:
            details.append(f"===== {f.name} =====")
            text = f.read_text(encoding="utf-8", errors="replace")
            details.append("\n".join(text.splitlines()[-30:]))
        results.add(
            "FAIL",
            "ASan/UBSan Sanitizer",
            "发现 sanitizer 错误:\n" + "\n".join(details),
        )
        failed = True

    if not failed:
        results.add(
            "PASS",
            title,
            "配置、构建、运行、测试全部通过，无 sanitizer 错误",
        )


def run(
    results: ResultCollector,
    included: list[str],
    dry_run: bool = False,
) -> None:
    _run_clang_tidy_build(results, dry_run)
    _run_sanitizer_build(results, dry_run)
