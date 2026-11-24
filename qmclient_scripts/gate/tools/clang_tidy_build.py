# 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env python3
"""QmClient clang-tidy 独立工具。

两个环境变量控制扫描范围：

- `CLANG_TIDY_CHANGED_ONLY=1`：只扫本次改动涉及的翻译单元（PR / merge queue 用）。
  改动里出现头文件、构建脚本或 `.clang-tidy` 本身时自动退回全量——头文件会被
  许多 TU 包含，只编译「改名的那一个 .cpp」会把影响面漏掉。
- `CLANG_TIDY_DIFF_BASE`：增量比较的基准（默认 `origin/master`）。
"""

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
CLANG_VERSION = os.environ.get("CLANG_VERSION", "22")
BUILD_DIR = REPO_ROOT / os.environ.get("CLANG_TIDY_BUILD_DIR", "build-clang-tidy")
BUILD_TARGET = os.environ.get("CLANG_TIDY_TARGET", "game-client")
COMPILER_MODE = os.environ.get(
    "CLANG_TIDY_COMPILER", "clang-cl" if os.name == "nt" else "clang"
)
CHANGED_ONLY = os.environ.get("CLANG_TIDY_CHANGED_ONLY", "").strip() not in ("", "0")
DIFF_BASE = os.environ.get("CLANG_TIDY_DIFF_BASE", "").strip() or "origin/master"

SOURCE_EXCLUDES = (
    "src/rust-bridge/base/cxx.h",
    "src/engine/external/",
    "src/generated/",
)

SOURCE_SUFFIXES = (".cpp", ".cc", ".cxx", ".c")
HEADER_SUFFIXES = (".h", ".hpp", ".hh", ".hxx", ".inl", ".inc", ".ipp")
# 这些文件一变，编译参数、TU 组成或扫描规则就可能整体变化，增量结论不再可信。
THIS_TOOL = Path(__file__).resolve().relative_to(REPO_ROOT).as_posix()
FULL_SCAN_TRIGGERS = ("CMakeLists.txt", ".clang-tidy", THIS_TOOL)
FULL_SCAN_PREFIXES = ("cmake/",)


def resolve_tool(tool, env_var=None, versioned=False):
    candidate = os.environ.get(env_var) if env_var else None
    if candidate:
        resolved = shutil.which(candidate)
        if resolved:
            return resolved
        raise SystemExit(f"{env_var} points to an unavailable command: {candidate}")

    candidates = []
    if versioned:
        candidates.extend((f"{tool}-{CLANG_VERSION}", f"{tool}-{CLANG_VERSION}.exe"))
    candidates.extend((tool, f"{tool}.exe"))

    for name in candidates:
        resolved = shutil.which(name)
        if resolved:
            return resolved

    raise SystemExit(f"Required command not found: {', '.join(candidates)}")


def require_llvm_major(command, display_name):
    result = subprocess.run(
        [command, "--version"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    output = result.stdout
    if not re.search(rf"(version|LLVM version)\s+{re.escape(CLANG_VERSION)}\.", output):
        raise SystemExit(
            f"{display_name} must be LLVM/Clang {CLANG_VERSION}.x, got:\n{output}"
        )
    print(output.splitlines()[0])


def normalize_path(path):
    return path.replace("\\", "/")


def git_changed_paths(base):
    """返回相对仓库根、以 `/` 分隔的改动文件列表；失败时返回 None。"""
    result = subprocess.run(
        ["git", "diff", "--name-only", "--diff-filter=ACMR", f"{base}...HEAD"],
        cwd=REPO_ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        print(f"[clang-tidy] 无法对比 {base}: {result.stderr.strip()}", flush=True)
        return None
    return [normalize_path(line.strip()) for line in result.stdout.splitlines() if line.strip()]


def classify_changed_paths(paths):
    """把改动文件分成「源码」与「必须全量」两类。

    返回 (源码列表, 是否需要全量, 原因)。头文件命中任何一条就全量：它会被很多 TU
    包含，只重编改名的那几个 .cpp 会漏掉真正受影响的面。
    """
    sources = []
    for path in paths:
        if path.endswith(HEADER_SUFFIXES):
            return [], True, f"改动含头文件 {path}"
        if path in FULL_SCAN_TRIGGERS or path.startswith(FULL_SCAN_PREFIXES):
            return [], True, f"改动含构建配置 {path}"
        if path.endswith(SOURCE_SUFFIXES):
            sources.append(path)
    return sources, False, ""


def object_targets_for(sources):
    """用 compile_commands.json 把源文件映射到 ninja 的 .o 路径。

    clang-tidy 挂在编译命令上（CMAKE_CXX_CLANG_TIDY），所以「只编译这几个 .o」
    等价于「只对这几个 TU 跑 clang-tidy」，不需要改扫描机制本身。
    """
    compile_commands = BUILD_DIR / "compile_commands.json"
    if not compile_commands.is_file():
        print("[clang-tidy] 缺少 compile_commands.json，退回全量扫描", flush=True)
        return None

    entries = json.loads(compile_commands.read_text(encoding="utf-8"))
    by_source = {}
    for entry in entries:
        source = entry.get("file")
        output = entry.get("output")
        if not source or not output:
            continue
        by_source[normalize_path(os.path.relpath(source, REPO_ROOT))] = output
    if not by_source:
        # 老版本 CMake 的 Ninja 生成器不写 output 字段；此时无法定位 .o，必须退回
        # 全量，否则会把「找不到映射」误当成「没有需要扫描的 TU」而整轮跳过。
        print("[clang-tidy] compile_commands.json 缺少 output 字段，退回全量扫描", flush=True)
        return None

    targets = []
    missing = []
    for source in sources:
        output = by_source.get(source)
        if output:
            targets.append(output)
        else:
            missing.append(source)
    if missing:
        print(
            "[clang-tidy] 这些改动源文件不属于 "
            f"{BUILD_TARGET}，不参与扫描: {', '.join(missing)}",
            flush=True,
        )
    # 去重并保持稳定顺序，避免重复边。
    return sorted(set(targets))


def load_header_exclude_regex():
    clang_tidy = REPO_ROOT / ".clang-tidy"
    for line in clang_tidy.read_text(encoding="utf-8").splitlines():
        if line.startswith("ExcludeHeaderFilterRegex:"):
            value = line.split(":", 1)[1].strip()
            if len(value) >= 2 and value[0] == value[-1] == "'":
                value = value[1:-1]
            return value.replace("/", r"[\\/]")
    return None


def write_filter_script(filter_script, clang_tidy_bin):
    header_exclude = load_header_exclude_regex()
    filter_script.write_text(
        f"""#!/usr/bin/env python3
import os
import subprocess
import sys

SOURCE_EXCLUDES = {SOURCE_EXCLUDES!r}
CLANG_TIDY = {str(clang_tidy_bin)!r}
HEADER_EXCLUDE = {header_exclude!r}


def normalize(path):
\treturn path.replace('\\\\', '/')


def find_source(argv):
\tfor arg in argv:
\t\tif arg == '--':
\t\t\tbreak
\t\tif arg.startswith('--source='):
\t\t\treturn arg[len('--source='):]
\t\tif not arg.startswith('-'):
\t\t\treturn arg
\treturn ''


source = normalize(find_source(sys.argv[1:]))
for excluded in SOURCE_EXCLUDES:
\tif source.endswith(excluded) or f'/{{excluded}}' in source:
\t\tsys.exit(0)

command = [CLANG_TIDY, '--allow-no-checks']
if os.name == 'nt':
\tcommand.append('--extra-arg-before=/clang:-Wno-unused-command-line-argument')
\tcommand.append('--extra-arg-before=/EHsc')
if HEADER_EXCLUDE:
\tcommand.append(f'--exclude-header-filter={{HEADER_EXCLUDE}}')
command.extend(sys.argv[1:])
sys.exit(subprocess.call(command))
""",
        encoding="utf-8",
        newline="\n",
    )


def run_command(command, cwd=None, log_path=None, log_header=None):
    if log_path:
        with log_path.open("w", encoding="utf-8", newline="\n") as log_file:
            if log_header:
                log_file.write(log_header)
            process = subprocess.Popen(
                command,
                cwd=cwd,
                text=True,
                encoding="utf-8",
                errors="replace",
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
            )
            assert process.stdout is not None
            for line in process.stdout:
                print(line, end="")
                log_file.write(line)
            return process.wait()
    subprocess.run(command, cwd=cwd, check=True)
    return 0


def main():
    if COMPILER_MODE == "clang-cl":
        clang_bin = resolve_tool("clang-cl", "CLANG_BIN", versioned=True)
        clangxx_bin = clang_bin
    else:
        clang_bin = resolve_tool("clang", "CLANG_BIN", versioned=True)
        clangxx_bin = resolve_tool("clang++", "CLANGXX_BIN", versioned=True)

    clang_tidy_bin = resolve_tool("clang-tidy", "CLANG_TIDY_BIN", versioned=True)
    cmake_bin = resolve_tool("cmake", "CMAKE_BIN")
    ninja_bin = resolve_tool("ninja", "NINJA_BIN")

    require_llvm_major(clang_bin, "clang")
    require_llvm_major(clangxx_bin, "clang++")
    require_llvm_major(clang_tidy_bin, "clang-tidy")

    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    filter_script = BUILD_DIR / "clang-tidy-filter.py"
    write_filter_script(filter_script, clang_tidy_bin)

    clang_tidy_command = f"{sys.executable};{filter_script}"
    configure_command = [
        cmake_bin,
        "-G",
        "Ninja",
        f"-DCMAKE_MAKE_PROGRAM={ninja_bin}",
        f"-DCMAKE_CXX_COMPILER={clangxx_bin}",
        f"-DCMAKE_C_COMPILER={clang_bin}",
        f"-DCMAKE_CXX_CLANG_TIDY={clang_tidy_command}",
        f"-DCMAKE_C_CLANG_TIDY={clang_tidy_command}",
        "-DQM_DISABLE_PARALLEL_COMPILE=ON",
        "-DCMAKE_BUILD_TYPE=Debug",
        "-DHEADLESS_CLIENT=ON",
        "-DVULKAN=OFF",
        "-DDOWNLOAD_GTEST=ON",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "..",
    ]
    run_command(configure_command, cwd=BUILD_DIR)

    log_path = BUILD_DIR / "clang-tidy.log"
    # None 表示全量；否则是增量要编译的具体 .o 路径。
    incremental_targets = None
    mode_line = "[clang-tidy] 模式: 全量扫描\n"
    if CHANGED_ONLY:
        paths = git_changed_paths(DIFF_BASE)
        if paths is None:
            mode_line = f"[clang-tidy] 模式: 全量扫描（增量基准 {DIFF_BASE} 不可用）\n"
        else:
            sources, needs_full, reason = classify_changed_paths(paths)
            if needs_full:
                mode_line = f"[clang-tidy] 模式: 全量扫描（{reason}）\n"
            else:
                incremental_targets = object_targets_for(sources)
                if incremental_targets is not None and not incremental_targets:
                    message = (
                        "[clang-tidy] 模式: 增量，但没有需要扫描的 TU"
                        f"（改动 {len(paths)} 个文件，其中 C/C++ 源文件 {len(sources)} 个）\n"
                    )
                    print(message, end="", flush=True)
                    log_path.write_text(message, encoding="utf-8", newline="\n")
                    return 0
                if incremental_targets is not None:
                    mode_line = (
                        f"[clang-tidy] 模式: 增量，{len(incremental_targets)} 个 TU"
                        f"（改动 {len(paths)} 个文件，基准 {DIFF_BASE}）\n"
                    )
    print(mode_line, end="", flush=True)

    build_command = [cmake_bin, "--build", ".", "--config", "Debug"]
    if incremental_targets:
        # ninja 直接接受 .o 路径：clang-tidy 挂在编译命令上，编这几个就等于只扫这几个 TU。
        build_command.extend(["--"])
        build_command.extend(incremental_targets)
        build_command.extend(["-k", "0"])
    else:
        build_command.extend(["--target", BUILD_TARGET, "--", "-k", "0"])
    return run_command(build_command, cwd=BUILD_DIR, log_path=log_path, log_header=mode_line)


if __name__ == "__main__":
    sys.exit(main())
