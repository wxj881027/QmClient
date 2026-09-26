# 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env python3
"""统一更新 QmClient 仓库内的版本定义。"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
VERSION_H_PATH = REPO_ROOT / "src/game/version.h"
VERSION_RE = re.compile(r"^\d+(?:\.\d+){0,2}$")
DEV_VERSION_RE = re.compile(r"^\d+\.\d+\.\d+$")
VERSION_DEFINE_RE = re.compile(
    r'^(#define\s+QMCLIENT_STABLE_VERSION\s+)"[^"]+"(?=\r?$)', re.MULTILINE
)
DEV_VERSION_DEFINE_RE = re.compile(
    r'^(#define\s+QMCLIENT_DEV_VERSION\s+)"[^"]+"(?=\r?$)', re.MULTILINE
)

def configure_stdio() -> None:
    for stream in (sys.stdout, sys.stderr):
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure is not None:
            reconfigure(encoding="utf-8", errors="replace")



def normalize_version(version: str | None, tag: str | None) -> str:
    if bool(version) == bool(tag):
        raise ValueError("必须且只能提供 --version 或 --tag 其中之一。")

    raw = version if version is not None else tag
    assert raw is not None
    normalized = raw[1:] if raw[:1] in {"v", "V"} else raw
    if not VERSION_RE.fullmatch(normalized):
        raise ValueError(f"正式版本格式非法：{raw}。期望格式为 X、X.Y 或 X.Y.Z。")
    return normalized


def update_version_h(version: str, *, development: bool = False) -> None:
    with VERSION_H_PATH.open("r", encoding="utf-8", newline="") as version_file:
        content = version_file.read()
    define_re = DEV_VERSION_DEFINE_RE if development else VERSION_DEFINE_RE
    updated, count = define_re.subn(rf'\1"{version}"', content, count=1)
    if count != 1:
        name = "QMCLIENT_DEV_VERSION" if development else "QMCLIENT_STABLE_VERSION"
        raise RuntimeError(f"未找到 {name} 宏，无法更新 src/game/version.h。")
    with VERSION_H_PATH.open("w", encoding="utf-8", newline="") as version_file:
        version_file.write(updated)


def latest_tag_version() -> str | None:
    """读取最近的 v* tag，返回去掉 v 前缀的版本号；无 tag 或非 git 仓库返回 None。"""
    try:
        result = subprocess.run(
            ["git", "tag", "--list", "v*", "--sort=-v:refname"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            encoding="utf-8",
            check=False,
        )
    except (FileNotFoundError, OSError):
        return None
    for line in result.stdout.splitlines():
        tag = line.strip()
        if not tag:
            continue
        normalized = tag[1:] if tag[:1] in {"v", "V"} else tag
        # 只接受纯 X.Y[.Z]，跳过上游遗留的 v16.5-headless、nightly 等干扰 tag
        if VERSION_RE.fullmatch(normalized):
            return normalized
    return None


def _version_key(version: str) -> list[int]:
    parts: list[int] = []
    for piece in version.split("."):
        try:
            parts.append(int(piece))
        except ValueError:
            parts.append(0)
    return parts


def warn_if_not_progressing(version: str) -> None:
    """目标版本未高于最近 tag 时，打印 stderr 警告（不阻断）。"""
    latest = latest_tag_version()
    if latest is None:
        return
    target = _version_key(version)
    current = _version_key(latest)
    width = max(len(target), len(current))
    target += [0] * (width - len(target))
    current += [0] * (width - len(current))
    if target <= current:
        print(
            f"[bump-version] 警告：目标版本 {version} 未高于最近 tag v{latest}，"
            "可能是重复或倒退版本。",
            file=sys.stderr,
        )


def main() -> int:
    configure_stdio()

    parser = argparse.ArgumentParser(description="统一更新 QmClient 版本号")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--version", help="正式版本号，如 3、3.1 或 3.1.1")
    group.add_argument("--tag", help="正式版本 tag，如 v3 或 v3.1")
    group.add_argument("--dev-version", help="开发测试版本号，必须为 X.Y.Z")
    parser.add_argument(
        "--dry-run", action="store_true", help="只打印解析后的版本，不写回文件"
    )
    args = parser.parse_args()

    development = args.dev_version is not None
    if development:
        normalized = args.dev_version
        if not DEV_VERSION_RE.fullmatch(normalized):
            raise ValueError(f"开发测试版本格式非法：{normalized}。期望格式为 X.Y.Z。")
    else:
        normalized = normalize_version(args.version, args.tag)
        warn_if_not_progressing(normalized)
    print(f"目标版本：{normalized}")
    if args.dry_run:
        print("Dry-run：未写回文件。")
        return 0

    update_version_h(normalized, development=development)
    print(f"已更新：{VERSION_H_PATH}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
