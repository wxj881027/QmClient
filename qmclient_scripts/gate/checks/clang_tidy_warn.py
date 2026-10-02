# 请抬头享受阳光｜日子很好 我很我---------致咩子
#!/usr/bin/env python3
"""全量 .clang-tidy 附加检查（高噪音，默认 WARN）。"""

from __future__ import annotations

import shutil
from pathlib import Path

from lib import runner
from lib.report import ResultCollector

REPO_ROOT = Path(__file__).resolve().parents[3]

CONFIG_MACRO_HEADERS = {
	"src/engine/shared/config_variables.h",
	"src/engine/shared/config_variables_qmclient.h",
	"src/engine/shared/config_variables_tclient.h",
}

SOURCE_SUFFIXES = (".c", ".cc", ".cpp")


def _existing_source_files(included: list[str]) -> list[str]:
	source_files = []
	for file in included:
		normalized_file = file.replace("\\", "/")
		source_path = Path(normalized_file)
		if not source_path.is_absolute():
			source_path = REPO_ROOT / source_path
		if normalized_file not in CONFIG_MACRO_HEADERS and normalized_file.endswith(SOURCE_SUFFIXES) and source_path.is_file():
			source_files.append(normalized_file)
	return source_files


def run(results: ResultCollector, included: list[str], dry_run: bool = False) -> None:
	if not shutil.which("clang-tidy"):
		results.add("WARN", "全量 .clang-tidy 附加检查", "PATH 中未找到 clang-tidy，已跳过")
		return
	# 自动探测含 compile_commands.json 的构建目录（debug 优先，其次 release-pdb/release）
	candidate_dirs = [
		"cmake-build-debug",
		"cmake-build-release-pdb",
		"cmake-build-release",
	]
	build_dir = None
	cc = None
	for d in candidate_dirs:
		candidate = REPO_ROOT / d / "compile_commands.json"
		if candidate.exists():
			build_dir = d
			cc = candidate
			break
	if cc is None:
		results.add(
			"WARN",
			"全量 .clang-tidy 附加检查",
			"缺少 compile_commands.json，请先跑 strict-debug-check 或 default/full 构建层（探测目录：cmake-build-debug、cmake-build-release-pdb、cmake-build-release）",
		)
		return
	source_files = _existing_source_files(included)
	source_file_set = set(source_files)
	skipped = [file for file in included if file.replace("\\", "/") not in source_file_set]
	if skipped:
		results.add(
			"INFO",
			"全量 .clang-tidy 附加检查跳过非独立翻译单元",
			"配置变量宏头和 header 文件不作为独立翻译单元分析，已跳过：\n" + "\n".join(skipped),
		)
	for file in source_files:
		code, out = runner.run(
			[
				"clang-tidy",
				file,
				f"-p={build_dir}",
				f"--config-file={REPO_ROOT / '.clang-tidy'}",
				"--extra-arg=-Qunused-arguments",
				# Clang 无法读取 MSVC 的二进制 PCH，直接解析同一组头文件。
				*(["--extra-arg=/Y-"] if runner.is_windows_host() else []),
				"--header-filter=^$",
				"--quiet",
			],
			title=f"全量 .clang-tidy 附加检查: {file}",
			check=False,
		)
		if code != 0:
			results.add("WARN", f"全量 .clang-tidy 附加检查: {file}", out)
		else:
			results.add("PASS", f"全量 .clang-tidy 附加检查: {file}", "通过")
