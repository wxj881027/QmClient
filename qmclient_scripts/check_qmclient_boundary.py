#!/usr/bin/env python3
"""检查新 Qm 代码是否意外依赖旧 TClient 聚合器。"""

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
    """扫描根层和嵌套目录中的 Qm 测试源文件。"""
    test_root = REPO_ROOT / "src/test"
    files: list[Path] = []
    for path in test_root.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        relative_parts = path.relative_to(test_root).parts
        if path.name.startswith("qmclient_") or any(part.lower().startswith("qmclient") for part in relative_parts[:-1]):
            files.append(path)
    return sorted(files)


def check_file(path: Path) -> list[str]:
    violations: list[str] = []
    try:
        lines = path.read_text(encoding="utf-8-sig").splitlines()
    except UnicodeDecodeError as error:
        return [f"{path}: 无法按 UTF-8 读取: {error}"]

    for line_number, line in enumerate(lines, start=1):
        for pattern in BANNED_PATTERNS:
            if pattern.search(line):
                violations.append(f"{path}:{line_number}: 命中禁止的 TClient 依赖: {line.strip()}")
    return violations


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "paths",
        nargs="*",
        type=Path,
        help="要扫描的新增 Qm 源码目录或文件",
    )
    args = parser.parse_args()
    explicit_paths = bool(args.paths)
    roots = [path.resolve() for path in args.paths] if explicit_paths else [REPO_ROOT / path for path in DEFAULT_SCAN_PATHS]
    if not args.paths:
        roots.extend(iter_qm_test_files())

    if not explicit_paths:
        roots = [path for path in roots if path.exists()]
    missing = [path for path in roots if not path.exists()]
    if missing:
        print("扫描路径不存在:\n" + "\n".join(str(path) for path in missing), file=sys.stderr)
        return 1

    files = iter_source_files(roots)
    if not files:
        print("未找到可扫描的 QmClient 源文件。", file=sys.stderr)
        return 1
    violations = [violation for path in files for violation in check_file(path)]
    if violations:
        print("\n".join(violations), file=sys.stderr)
        return 1

    print(f"QmClient boundary check passed: {len(files)} source file(s) scanned.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
