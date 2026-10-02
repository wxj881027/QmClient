#!/usr/bin/env python3
"""统一更新和读取 QmClient 的正式、预览版本。"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

if not __package__:
	sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from qmclient_scripts.versioning import ReleaseVersion, next_preview_tag, parse_version, version_from_header


REPO_ROOT = Path(__file__).resolve().parents[1]
VERSION_H_PATH = REPO_ROOT / "src/game/version.h"


def configure_stdio() -> None:
	for stream in (sys.stdout, sys.stderr):
		reconfigure = getattr(stream, "reconfigure", None)
		if reconfigure is not None:
			reconfigure(encoding="utf-8", errors="replace")


def normalize_version(version: str | None, tag: str | None) -> str:
	if bool(version) == bool(tag):
		raise ValueError("必须且只能提供 --version 或 --tag 其中之一")
	parsed = parse_version(version or tag or "")
	if tag is not None and tag != parsed.tag:
		raise ValueError(f"Tag 必须使用规范名称 {parsed.tag}")
	return parsed.version


def update_version_h(version: str) -> None:
	parsed = parse_version(version)
	with VERSION_H_PATH.open("r", encoding="utf-8", newline="") as version_file:
		content = version_file.read()
	newline = "\r\n" if "\r\n" in content else "\n"
	definitions = f'''// QmClient
// 正式版 X.Y；预览版 X.Y-preview.N。预览批次为 0 时是正式版。
#define QMCLIENT_BASE_VERSION "{parsed.base}"
#define QMCLIENT_PREVIEW_NUMBER {parsed.preview}

#define QMCLIENT_STRINGIFY_IMPL(Value) #Value
#define QMCLIENT_STRINGIFY(Value) QMCLIENT_STRINGIFY_IMPL(Value)
#if QMCLIENT_PREVIEW_NUMBER > 0
#define QMCLIENT_VERSION QMCLIENT_BASE_VERSION "-preview." QMCLIENT_STRINGIFY(QMCLIENT_PREVIEW_NUMBER)
#define CLIENT_RELEASE_VERSION QMCLIENT_BASE_VERSION " Preview " QMCLIENT_STRINGIFY(QMCLIENT_PREVIEW_NUMBER)
#define QMCLIENT_IS_DEVELOPMENT_BUILD 1
#else
#define QMCLIENT_VERSION QMCLIENT_BASE_VERSION
#define CLIENT_RELEASE_VERSION QMCLIENT_BASE_VERSION
#define QMCLIENT_IS_DEVELOPMENT_BUILD 0
#endif

#define CLIENT_NAME "QmClient"
'''.replace("\n", newline)
	block = re.compile(r"// QmClient\r?\n.*?(?=\r?\n#endif[^\n]*\r?\n?\Z)", re.DOTALL)
	updated, count = block.subn(lambda _: definitions, content, count=1)
	if count != 1:
		raise ValueError("version.h 缺少 QmClient 版本定义区块")
	with VERSION_H_PATH.open("w", encoding="utf-8", newline="") as version_file:
		version_file.write(updated)


def describe(version: ReleaseVersion) -> dict[str, str | int | bool]:
	return {
		"version": version.version,
		"base": version.base,
		"tag": version.tag,
		"preview": version.preview,
		"display": version.display,
		"prerelease": bool(version.preview),
		"android_version_code": version.android_version_code,
	}


def find_next_preview() -> str:
	current = version_from_header(VERSION_H_PATH.read_text(encoding="utf-8"))
	head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO_ROOT, text=True).strip()
	rows = subprocess.check_output(
		["git", "for-each-ref", "--format=%(refname:short) %(objectname) %(*objectname)", "refs/tags/v*"],
		cwd=REPO_ROOT,
		text=True,
	)
	tags = []
	for row in rows.splitlines():
		fields = row.split()
		tags.append((fields[0], fields[-1]))
	return next_preview_tag(current, head, tags)


def main() -> int:
	configure_stdio()
	parser = argparse.ArgumentParser(description="QmClient 两段版本和发布通道")
	group = parser.add_mutually_exclusive_group(required=True)
	group.add_argument("--version", help="X.Y 或 X.Y-preview.N")
	group.add_argument("--tag", help="vX.Y 或 vX.Y-preview.N")
	group.add_argument("--describe", action="store_true", help="输出当前版本的 JSON 元数据")
	group.add_argument("--field", choices=("version", "display", "android_version_code"))
	group.add_argument("--next-preview-tag", action="store_true", help="读取当前提交应使用的预览 Tag，不修改文件")
	parser.add_argument("--dry-run", action="store_true")
	args = parser.parse_args()
	if args.next_preview_tag:
		print(find_next_preview())
		return 0
	if args.describe or args.field:
		metadata = describe(version_from_header(VERSION_H_PATH.read_text(encoding="utf-8")))
		print(metadata[args.field] if args.field else json.dumps(metadata, ensure_ascii=False))
		return 0
	version = normalize_version(args.version, args.tag)
	describe(parse_version(version))
	print(f"目标版本：{version}")
	if args.dry_run:
		print("Dry-run：未写回文件")
	else:
		update_version_h(version)
		print(f"已更新：{VERSION_H_PATH}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
