"""QmClient 两段版本、预览批次和平台元数据。"""

from __future__ import annotations

import re
from typing import NamedTuple


VERSION_RE = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-preview\.([1-9][0-9]*))?")


class ReleaseVersion(NamedTuple):
	major: int
	sequence: int
	preview: int = 0

	@property
	def base(self) -> str:
		return f"{self.major}.{self.sequence}"

	@property
	def version(self) -> str:
		return self.base + (f"-preview.{self.preview}" if self.preview else "")

	@property
	def tag(self) -> str:
		return f"v{self.version}"

	@property
	def display(self) -> str:
		return self.base + (f" Preview {self.preview}" if self.preview else "")

	@property
	def sort_key(self) -> tuple[int, int, bool, int]:
		return self.major, self.sequence, self.preview == 0, self.preview

	@property
	def android_version_code(self) -> int:
		# 预览批次排在同版正式版之前；新编码也高于旧三段式版本的编码。
		if self.sequence >= 10_000 or self.preview >= 9_999:
			raise ValueError("Android 版本序号必须小于 10000，预览批次必须小于 9999")
		code = (self.major * 10_000 + self.sequence) * 10_000
		code += self.preview or 9_999
		if code > 2_100_000_000:
			raise ValueError("版本超过 Android versionCode 上限")
		return code


def parse_version(value: str) -> ReleaseVersion:
	normalized = value[1:] if value.startswith("v") else value
	match = VERSION_RE.fullmatch(normalized)
	if match is None or len(normalized) >= 32:
		raise ValueError(f"版本格式非法：{value}；应为 X.Y 或 X.Y-preview.N")
	parts = tuple(int(part or 0) for part in match.groups())
	if any(part > 2_147_483_647 for part in parts):
		raise ValueError(f"版本数字超出范围：{value}")
	return ReleaseVersion(*parts)


def version_from_header(source: str) -> ReleaseVersion:
	base = re.search(r'^#define QMCLIENT_BASE_VERSION "([^"]+)"\r?$', source, re.MULTILINE)
	preview = re.search(r"^#define QMCLIENT_PREVIEW_NUMBER ([0-9]+)\r?$", source, re.MULTILINE)
	if base is None or preview is None:
		raise ValueError("version.h 缺少统一版本定义")
	suffix = f"-preview.{preview[1]}" if int(preview[1]) else ""
	return parse_version(base[1] + suffix)


def preview_tag_for_build(current: ReleaseVersion, head: str, tags: list[tuple[str, str]], build_number: int) -> str:
	"""按 CI 构建序号选出预览 Tag。

	预览批次不再写进源码：新提交使用构建序号，同一提交重跑复用已有 Tag；
	构建序号低于仓库里已有批次时取「最大批次 + 1」，保证编号始终前进。
	基础版本已经打过正式版时直接报错，避免产出比已发布版本更旧的预览。
	"""
	if build_number <= 0:
		raise ValueError("构建序号必须为正整数")
	highest = 0
	reused: list[ReleaseVersion] = []
	for tag, commit in tags:
		try:
			version = parse_version(tag)
		except ValueError:
			continue
		if version.base != current.base:
			continue
		if not version.preview:
			raise ValueError(f"{current.base} 已发布正式版，请先用 --tag 提升 QMCLIENT_BASE_VERSION")
		highest = max(highest, version.preview)
		if commit == head:
			reused.append(version)
	if reused:
		return max(reused).tag
	return f"v{current.base}-preview.{max(build_number, highest + 1)}"
