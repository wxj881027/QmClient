#!/usr/bin/env python3
# 请抬头享受阳光｜日子很好 我很我---------致咩子
"""把已收集好的便携目录写成一个 UTF-8 文件名的 ZIP。

Windows 上 `cmake -E tar --format=zip` 按宿主代码页写归档名，非 ASCII
文件名会被替换成 `?`：`霞鹜新致宋.ttf` 与 `霞鹜新晰黑.ttf` 都变成
`?????.ttf`，发布校验随后看到重复条目并中止；即使不重复，客户端也
再也找不到这些字体。这里改用 Python 的 zipfile，固定按 UTF-8 写入
并置位 UTF-8 标志，写完后再逐项核对归档名与磁盘名逐字节一致。
"""
from __future__ import annotations

import argparse
import os
import sys
import time
import zipfile
from collections.abc import Iterator
from pathlib import Path

# 归档名里不允许出现的字符：反斜杠会被 Windows 解压当成目录分隔符，
# `?` 与替换字符是有损代码页转换的痕迹，冒号与空字符是非法路径字符。
FORBIDDEN_NAME_CHARACTERS = ("\\", ":", "\0", "?", "\ufffd")


def configure_stdio() -> None:
    # CI 的 Windows 构建把 stdout 接到 cp1252 上，中文提示会让打包命令整体失败。
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure is not None:
            reconfigure(encoding="utf-8", errors="replace")


def _iter_entries(source: Path) -> Iterator[tuple[Path, str, bool]]:
    """按稳定顺序产出 (磁盘路径, 归档名, 是否为目录)，每个目录恰好一次。"""
    prefix = source.name
    for directory, sub_directories, file_names in os.walk(source):
        sub_directories.sort()
        file_names.sort()
        current = Path(directory)
        relative = current.relative_to(source).as_posix()
        archive_directory = prefix if relative == "." else f"{prefix}/{relative}"
        yield current, f"{archive_directory}/", True
        for name in file_names:
            yield current / name, f"{archive_directory}/{name}", False


def _validate_archive_name(name: str) -> None:
    # 目录项按 ZIP 约定以 `/` 结尾，校验时先去掉这个尾部分隔符。
    candidate = name[:-1] if name.endswith("/") else name
    if not candidate:
        raise ValueError("归档名为空")
    if any(character in candidate for character in FORBIDDEN_NAME_CHARACTERS):
        raise ValueError(f"归档名含有非法字符: {name!r}")
    parts = candidate.split("/")
    if any(part in {"", ".", ".."} for part in parts):
        raise ValueError(f"归档名含有非法路径片段: {name!r}")
    if any(part.endswith((".", " ")) for part in parts):
        raise ValueError(f"归档名以点或空格结尾: {name!r}")


def _zip_info(path: Path, archive_name: str, is_directory: bool) -> zipfile.ZipInfo:
    stat_result = path.stat()
    # mtime 截断到秒，与 ZIP 的时间字段一致；超出可表示范围时退回 1980-01-01。
    date_time = time.localtime(stat_result.st_mtime)[:6]
    if date_time[0] < 1980:
        date_time = (1980, 1, 1, 0, 0, 0)
    info = zipfile.ZipInfo(archive_name, date_time=date_time)
    info.create_system = 3  # 3 = Unix，保留权限位；同时置 MS-DOS 目录标志
    info.external_attr = (stat_result.st_mode & 0xFFFF) << 16
    if is_directory:
        info.external_attr |= 0x10
    info.compress_type = zipfile.ZIP_DEFLATED
    return info


def _collect(source: Path) -> dict[str, int]:
    expected: dict[str, int] = {}
    file_count = 0
    for path, archive_name, is_directory in _iter_entries(source):
        _validate_archive_name(archive_name)
        if archive_name in expected:
            raise ValueError(f"归档名重复: {archive_name}")
        if is_directory:
            expected[archive_name] = 0
            continue
        file_count += 1
        expected[archive_name] = path.stat().st_size
    if file_count == 0:
        raise ValueError(f"便携目录为空: {source}")
    return expected


def _verify(package: Path, expected: dict[str, int]) -> None:
    """重新读回归档，确认每个名字和大小都与磁盘一致。"""
    actual: dict[str, int] = {}
    with zipfile.ZipFile(package, "r") as archive:
        for info in archive.infolist():
            name = info.filename
            if info.is_dir() and not name.endswith("/"):
                name += "/"
            if name in actual:
                raise ValueError(f"归档名重复: {name!r}")
            actual[name] = info.file_size
            if name not in expected:
                raise ValueError(f"归档名与磁盘名不一致: {name!r}")
            if expected[name] != info.file_size:
                raise ValueError(f"归档条目大小与磁盘不一致: {name!r}")
    missing = sorted(set(expected) - set(actual))
    if missing:
        raise ValueError(f"归档缺少条目: {', '.join(missing[:5])}")
    if len(actual) != len(expected):
        raise ValueError(
            f"归档条目数量与磁盘不一致: {len(actual)} != {len(expected)}"
        )


def pack(source: Path, output: Path) -> int:
    """把 source 目录写成 output，返回条目数量。"""
    source = source.resolve(strict=True)
    if not source.is_dir():
        raise ValueError(f"便携目录不存在: {source}")
    if not source.name:
        raise ValueError(f"便携目录必须有目录名: {source}")
    output = output.resolve()
    if output == source or source in output.parents:
        raise ValueError(f"输出文件不能落在被打包的目录内: {output}")

    expected = _collect(source)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(f".{output.name}.tmp")
    temporary.unlink(missing_ok=True)
    try:
        with zipfile.ZipFile(
            temporary, "w", zipfile.ZIP_DEFLATED, allowZip64=True
        ) as archive:
            for path, archive_name, is_directory in _iter_entries(source):
                info = _zip_info(path, archive_name, is_directory)
                if is_directory:
                    archive.writestr(info, b"")
                    continue
                with path.open("rb") as source_stream, archive.open(
                    info, "w", force_zip64=True
                ) as target_stream:
                    while chunk := source_stream.read(1024 * 1024):
                        target_stream.write(chunk)
        _verify(temporary, expected)
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    return len(expected)


def main() -> int:
    configure_stdio()
    parser = argparse.ArgumentParser(
        description="把便携目录打成 UTF-8 文件名的 ZIP"
    )
    parser.add_argument("--source", type=Path, required=True, help="便携目录")
    parser.add_argument("--output", type=Path, required=True, help="输出的 ZIP")
    args = parser.parse_args()
    try:
        count = pack(args.source, args.output)
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        print(f"打包失败: {error}", file=sys.stderr)
        return 1
    print(f"已写入 {args.output}（{count} 个条目）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
