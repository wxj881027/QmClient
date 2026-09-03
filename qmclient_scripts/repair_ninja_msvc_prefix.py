#!/usr/bin/env python3
"""修复 Ninja/MSVC 的 /showIncludes 依赖前缀。"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


def find_build_dir(arguments: list[str]) -> Path | None:
    for index, argument in enumerate(arguments):
        if argument in {"-B", "--build"} and index + 1 < len(arguments):
            return Path(arguments[index + 1])
        if argument.startswith("-B") and len(argument) > 2:
            return Path(argument[2:])
        if argument.startswith("--build="):
            return Path(argument.split("=", 1)[1])
    return None


def read_cache_value(cache_file: Path, name: str) -> str | None:
    for line in cache_file.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(f"{name}:"):
            return line.split("=", 1)[1]
    return None


def extract_prefix(build_dir: Path) -> str | None:
    compiler = read_cache_value(build_dir / "CMakeCache.txt", "CMAKE_C_COMPILER")
    if not compiler:
        return None

    probe_dir = build_dir / "CMakeFiles" / "ShowIncludes"
    probe_dir.mkdir(parents=True, exist_ok=True)
    header = probe_dir / "qm_probe.h"
    source = probe_dir / "qm_probe.c"
    try:
        header.write_text("\n", encoding="utf-8")
        source.write_text('#include "qm_probe.h"\nint main(void) { return 0; }\n', encoding="utf-8")
        header_path = str(header.resolve())
        result = subprocess.run(
            [compiler, "/nologo", "/showIncludes", "/c", source.name],
            cwd=probe_dir,
            capture_output=True,
            check=False,
        )
        if result.returncode != 0:
            return None

        for encoding in ("utf-8", "utf-8-sig", "mbcs"):
            try:
                output = result.stdout.decode(encoding, errors="replace")
            except LookupError:
                continue
            for line in output.splitlines():
                if header_path in line:
                    return line[: line.index(header_path)]
        return None
    finally:
        for probe_file in probe_dir.glob("qm_probe.*"):
            probe_file.unlink(missing_ok=True)


def repair_rules(rules_file: Path, expected_prefix: str) -> bool:
    text = rules_file.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"^msvc_deps_prefix = (.*)$", text, re.MULTILINE)
    if not match or match.group(1) == expected_prefix:
        return False
    rules_file.write_text(
        text.replace(match.group(1), expected_prefix),
        encoding="utf-8",
        newline="\n",
    )
    return True


def main() -> int:
    build_dir = find_build_dir(sys.argv[1:])
    if build_dir is None:
        return 0
    if not build_dir.is_absolute():
        build_dir = Path.cwd() / build_dir

    cache_file = build_dir / "CMakeCache.txt"
    rules_file = build_dir / "CMakeFiles" / "rules.ninja"
    if not cache_file.is_file() or not rules_file.is_file():
        # 配置命令尚未生成规则，或构建目录尚未存在：修复步骤不适用。
        return 0

    prefix = extract_prefix(build_dir)
    if not prefix:
        print(f"Failed to determine MSVC /showIncludes prefix in {build_dir}", file=sys.stderr)
        return 1
    if repair_rules(rules_file, prefix):
        print(f"Patched Ninja MSVC deps prefix: {build_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
