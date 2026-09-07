#!/usr/bin/env python3
"""检查 Qm 新代码的 TClient 依赖和纯 logic include 边界。"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


BANNED_PATTERNS = (
    re.compile(r"m_TClient\b"),
    re.compile(r"\bCTClient\b"),
    re.compile(r"\bTClientComponent\s*\("),
    re.compile(r"\btclient\.h\b", re.IGNORECASE),
)
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hh", ".hpp", ".inl"}
REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SCAN_PATHS = (
    "src/game/client/components/qmclient",
    "src/game/client/gameclient.cpp",
    "src/game/client/gameclient.h",
    "src/engine/client/client.cpp",
    "src/engine/client.h",
    "src/engine/shared/config_variables.h",
)

LOGIC_SCOPE = "src/game/client/components/qmclient/features"
INCLUDE_RE = re.compile(r"^\s*#\s*include\s*(<[^>]+>|\"[^\"]+\")")
STANDARD_HEADERS = {
    "algorithm",
    "array",
    "atomic",
    "bit",
    "cassert",
    "cctype",
    "cerrno",
    "cfenv",
    "cfloat",
    "charconv",
    "chrono",
    "cinttypes",
    "climits",
    "clocale",
    "cmath",
    "codecvt",
    "compare",
    "complex",
    "concepts",
    "condition_variable",
    "coroutine",
    "csetjmp",
    "csignal",
    "cstdarg",
    "cstddef",
    "cstdint",
    "cstdio",
    "cstdlib",
    "cstring",
    "ctgmath",
    "ctime",
    "cuchar",
    "cwchar",
    "cwctype",
    "deque",
    "exception",
    "execution",
    "filesystem",
    "format",
    "forward_list",
    "fstream",
    "functional",
    "future",
    "initializer_list",
    "iomanip",
    "ios",
    "iosfwd",
    "iostream",
    "istream",
    "iterator",
    "latch",
    "list",
    "limits",
    "map",
    "memory",
    "mutex",
    "new",
    "numbers",
    "numeric",
    "optional",
    "ostream",
    "queue",
    "random",
    "ranges",
    "ratio",
    "regex",
    "scoped_allocator",
    "set",
    "shared_mutex",
    "source_location",
    "span",
    "sstream",
    "stack",
    "stdexcept",
    "stop_token",
    "streambuf",
    "string",
    "string_view",
    "syncstream",
    "system_error",
    "thread",
    "tuple",
    "type_traits",
    "typeindex",
    "typeinfo",
    "unordered_map",
    "unordered_set",
    "utility",
    "valarray",
    "variant",
    "vector",
    "version",
}


def iter_source_files(roots: list[Path]) -> list[Path]:
    files: list[Path] = []
    for root in roots:
        if root.is_file() and root.suffix.lower() in SOURCE_SUFFIXES:
            files.append(root)
        elif root.is_dir():
            files.extend(
                path
                for path in root.rglob("*")
                if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES
            )
    return sorted(set(files))


def iter_qm_test_files() -> list[Path]:
    test_root = REPO_ROOT / "src/test"
    if not test_root.is_dir():
        return []
    files: list[Path] = []
    for path in test_root.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        relative_parts = path.relative_to(test_root).parts
        if path.name.startswith("qmclient_") or any(
            part.lower().startswith("qmclient") for part in relative_parts[:-1]
        ):
            files.append(path)
    return sorted(files)


def iter_logic_files() -> list[Path]:
    features_root = REPO_ROOT / LOGIC_SCOPE
    if not features_root.is_dir():
        return []
    return sorted(
        path
        for path in features_root.rglob("*")
        if path.is_file()
        and path.suffix.lower() in SOURCE_SUFFIXES
        and path.stem.endswith("_logic")
    )


def read_source(path: Path) -> list[str] | str:
    try:
        return path.read_text(encoding="utf-8-sig").splitlines()
    except UnicodeDecodeError as error:
        return f"{path}: 无法按 UTF-8 读取: {error}"


def check_file(path: Path) -> list[str]:
    content = read_source(path)
    if isinstance(content, str):
        return [content]

    violations: list[str] = []
    for line_number, line in enumerate(content, start=1):
        for pattern in BANNED_PATTERNS:
            if pattern.search(line):
                violations.append(
                    f"{path}:{line_number}: 命中禁止的 TClient 依赖: {line.strip()}"
                )
    return violations


def check_logic_includes(path: Path) -> list[str]:
    content = read_source(path)
    if isinstance(content, str):
        return [content]

    violations: list[str] = []
    for line_number, line in enumerate(content, start=1):
        match = INCLUDE_RE.match(line)
        if not match:
            continue

        target = match.group(1)[1:-1]
        if target.startswith("base/") or target in STANDARD_HEADERS:
            continue
        if match.group(1).startswith('"'):
            resolved = (path.parent / target).resolve()
            if resolved.is_file() and resolved.parent == path.parent.resolve():
                continue

        violations.append(
            f"{path}:{line_number}: logic 文件只允许 STL、base/ 和同目录头，"
            f"命中外部 include: {line.strip()}"
        )
    return violations


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "paths",
        nargs="*",
        type=Path,
        help="要扫描的 Qm 源码目录或文件；不传则扫描默认边界",
    )
    args = parser.parse_args()

    roots = (
        [path.resolve() for path in args.paths]
        if args.paths
        else [REPO_ROOT / path for path in DEFAULT_SCAN_PATHS] + iter_qm_test_files()
    )
    missing = [path for path in roots if not path.exists()]
    if missing:
        print("扫描路径不存在:\n" + "\n".join(str(path) for path in missing), file=sys.stderr)
        return 1

    files = iter_source_files(roots)
    if not files:
        print("未找到可扫描的 QmClient 源文件。", file=sys.stderr)
        return 1

    logic_files = iter_logic_files()
    violations = [
        violation
        for path in files
        for violation in check_file(path)
    ]
    violations.extend(
        violation
        for path in logic_files
        for violation in check_logic_includes(path)
    )
    if violations:
        print("\n".join(violations), file=sys.stderr)
        return 1

    print(
        f"QmClient boundary check passed: {len(files)} source file(s) scanned, "
        f"{len(logic_files)} logic file(s) checked."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
