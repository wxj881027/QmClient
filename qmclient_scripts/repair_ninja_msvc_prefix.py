# 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env python3
from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path


def find_build_dir(argv: list[str]) -> Path | None:
    # 同时兼容 configure 的 -B 和 build 的 --build，两条命令都共用这一份修复入口。
    for i, arg in enumerate(argv):
        if arg == "-B" and i + 1 < len(argv):
            return Path(argv[i + 1])
        if arg == "--build" and i + 1 < len(argv):
            return Path(argv[i + 1])
        if arg.startswith("-B") and len(arg) > 2:
            return Path(arg[2:])
        if arg.startswith("--build="):
            return Path(arg.split("=", 1)[1])
    return None


def _read_cache_value(cache_file: Path, name: str) -> str | None:
    # 这里只读取单个 cache 项，避免为了一次修复再引入额外的 CMake 解析依赖。
    for line in cache_file.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(f"{name}:"):
            return line.split("=", 1)[1]
    return None


def _extract_showincludes_prefix(build_dir: Path) -> bytes | None:
    # 前缀必须与编译器写入管道的字节完全一致，不能先用替换解码再写成 UTF-8。
    compiler = _read_cache_value(build_dir / "CMakeCache.txt", "CMAKE_C_COMPILER")
    if not compiler:
        return None

    show_dir = build_dir / "CMakeFiles" / "ShowIncludes"
    show_dir.mkdir(parents=True, exist_ok=True)
    (show_dir / "foo.h").write_text("\n", encoding="utf-8")
    (show_dir / "main.c").write_text('#include "foo.h"\nint main(void) { return 0; }\n', encoding="utf-8")
    include_path = str((show_dir / "foo.h").resolve())
    result = subprocess.run(
        [compiler, "/nologo", "/showIncludes", "/c", "main.c"],
        cwd=show_dir,
        capture_output=True,
        check=False,
        timeout=30,
    )
    if result.returncode != 0:
        return None

    for encoding in ("utf-8", "mbcs"):
        try:
            path_bytes = include_path.encode(encoding)
        except (LookupError, UnicodeEncodeError):
            continue
        for line in result.stdout.splitlines():
            if path_bytes in line:
                return line[: line.index(path_bytes)]
    return None


def _read_rules_prefix(rules_file: Path) -> bytes | None:
    match = re.search(rb"^msvc_deps_prefix = ([^\r\n]*)", rules_file.read_bytes(), re.MULTILINE)
    return match.group(1) if match else None


def _repair_rules_file(rules_file: Path, expected_prefix: bytes) -> bool:
    # Ninja 按原始字节匹配 /showIncludes；保留生成文件的换行和其他非 UTF-8 内容。
    text = rules_file.read_bytes()
    match = re.search(rb"^msvc_deps_prefix = ([^\r\n]*)", text, re.MULTILINE)
    if match:
        current_prefix = match.group(1)
        if current_prefix == expected_prefix:
            return False
        updated = text[: match.start(1)] + expected_prefix + text[match.end(1) :]
        # RC 的 cmcldeps 命令使用同一前缀参数，随全局前缀一起修复。
        if current_prefix:
            updated = updated.replace(b'"' + current_prefix + b'"', b'"' + expected_prefix + b'"')
    else:
        newline = b"\r\n" if b"\r\n" in text else b"\n"
        updated = b"msvc_deps_prefix = " + expected_prefix + newline + text
    rules_file.write_bytes(updated)
    return True


def _invalidate_zero_dependency_objects(build_dir: Path) -> int:
    # 修复前缀不会改变 C++ 编译命令；让已丢失头文件依赖的对象重新变脏。
    if not (build_dir / ".ninja_deps").is_file():
        return 0
    ninja = _read_cache_value(build_dir / "CMakeCache.txt", "CMAKE_MAKE_PROGRAM")
    if not ninja:
        raise ValueError("CMAKE_MAKE_PROGRAM is missing")
    result = subprocess.run(
        [ninja, "-C", str(build_dir), "-t", "deps"],
        capture_output=True,
        check=True,
        timeout=30,
    )
    root = build_dir.resolve()
    count = 0
    for line in result.stdout.decode("utf-8").splitlines():
        match = re.match(r"(.+): #deps 0,.*\(VALID\)$", line)
        if not match:
            continue
        obj = (root / match.group(1)).resolve()
        if not obj.is_relative_to(root):
            raise ValueError(f"Dependency object is outside build directory: {obj}")
        if obj.is_file() and obj.stat().st_mtime_ns != 0:
            os.utime(obj, ns=(0, 0))
            count += 1
    return count


def repair_build_dir(build_dir: Path) -> bool:
    prefix = _extract_showincludes_prefix(build_dir)
    if not prefix:
        raise RuntimeError(f"Could not detect MSVC dependency prefix: {build_dir}")
    changed = _read_rules_prefix(build_dir / "CMakeFiles" / "rules.ninja") != prefix
    if changed:
        count = _invalidate_zero_dependency_objects(build_dir)
        _repair_rules_file(build_dir / "CMakeFiles" / "rules.ninja", prefix)
        print(f"Patched Ninja MSVC deps prefix: {build_dir}; invalidated objects: {count}")
    return changed


def main() -> int:
    argv = sys.argv[1:]
    build_dir = find_build_dir(argv)
    if build_dir is None:
        return 1
    build_dir = build_dir.resolve()
    if not (build_dir / "CMakeCache.txt").is_file() or not (build_dir / "CMakeFiles" / "rules.ninja").is_file():
        return 1
    changed = repair_build_dir(build_dir)
    return 2 if changed and "--post-build" in argv and "--build" in argv else 0


if __name__ == "__main__":
    raise SystemExit(main())
