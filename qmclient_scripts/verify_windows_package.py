#!/usr/bin/env python3
"""依据构建生成清单校验 Windows 归档，阻止测试程序或遗漏工具进入发布。"""

from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path, PurePosixPath
import zipfile
import subprocess

REMOVED_BUNDLED_RESOURCES = {
	"SDL2.dll",
	"data/qmclient/font_catalog.json",
	"data/qmclient/chat_emojis/agree.png",
	"data/qmclient/chat_emojis/angry.png",
	"data/qmclient/chat_emojis/awkward.png",
	"data/qmclient/chat_emojis/cute.png",
	"data/qmclient/chat_emojis/dead.png",
	"data/qmclient/chat_emojis/hehe.png",
	"data/qmclient/chat_emojis/insult.png",
	"data/qmclient/chat_emojis/kneel.png",
	"data/qmclient/chat_emojis/love.png",
	"data/qmclient/chat_emojis/no.png",
	"data/qmclient/chat_emojis/oppose.png",
	"data/qmclient/chat_emojis/question.png",
	"data/qmclient/chat_emojis/shocked.png",
	"data/qmclient/chat_emojis/smell.png",
	"data/qmclient/chat_emojis/support.png",
	"data/qmclient/chat_emojis/surrender.png",
	"data/qmclient/friendlinks/ddnet.png",
	"data/qmclient/friendlinks/ddrace.png",
	"data/qmclient/friendlinks/ddstats.png",
	"data/qmclient/friendlinks/qmclient.png",
	"data/qmclient/friendlinks/shengyan.png",
	"data/qmclient/friendlinks/teedata.png",
	"data/audio/qm_festive_haoyunlai.mp3",
	"data/qmclient/icons/qm_icons_regular_msdf.json",
	"data/qmclient/icons/qm_icons_regular_msdf.png",
	"data/qmclient/icons/qm_icons_bold_msdf.json",
	"data/qmclient/icons/qm_icons_bold_msdf.png",
	"data/qmclient/icons/qm_icons_light_msdf.json",
	"data/qmclient/icons/qm_icons_light_msdf.png",
	"data/qmclient/icons/qm_icons_fill_msdf.json",
	"data/qmclient/icons/qm_icons_fill_msdf.png",
	"data/shader/textured_msdf.frag",
	"data/shader/textured_msdf.vert",
	"data/shader/vulkan/textured_msdf.frag",
	"data/shader/vulkan/textured_msdf.vert",
	"data/shader/vulkan/textured_msdf.frag.spv",
	"data/shader/vulkan/textured_msdf.vert.spv",
	"data/fonts/Phosphor/Phosphor-Duotone.ttf",
	"data/qmclient/icons/qm_icons_duotone_msdf.json",
	"data/qmclient/icons/qm_icons_duotone_msdf.png",
}


# FFmpeg 运行时按族发布，族内版本号随上游大版本变化（FFmpeg 7 是 61/59/5/8，
# FFmpeg 9 是 63/61/7/10）。同一族出现两份说明归档里混入了旧版残留，
# 客户端会按加载顺序加载到不匹配的 ABI，表现为录像功能崩溃或静默失效。
FFMPEG_RUNTIME_FAMILIES = ("avcodec-", "avformat-", "avutil-", "swresample-", "swscale-")


def check_ffmpeg_runtime(names: list[str], ffmpeg_libdir: Path | None) -> None:
	"""每个 FFmpeg 族在包根必须恰好一份，且与捆绑库目录的版本一致。"""
	by_family: dict[str, list[str]] = {family: [] for family in FFMPEG_RUNTIME_FAMILIES}
	for name in names:
		parts = PurePosixPath(name).parts
		if len(parts) != 2:
			continue
		for family in FFMPEG_RUNTIME_FAMILIES:
			if parts[1].casefold().startswith(family):
				by_family[family].append(parts[1])
	duplicated = {family: sorted(files) for family, files in by_family.items() if len(files) > 1}
	if duplicated:
		raise ValueError(f"Windows package has multiple FFmpeg runtime DLLs per family: {duplicated}")
	missing = [family for family, files in by_family.items() if not files]
	if missing:
		raise ValueError(f"Windows package is missing FFmpeg runtime DLLs: {missing}")
	if ffmpeg_libdir is None:
		return
	expected = {path.name for path in ffmpeg_libdir.iterdir() if path.suffix.casefold() == ".dll" and any(path.name.casefold().startswith(family) for family in FFMPEG_RUNTIME_FAMILIES)}
	actual = {files[0] for files in by_family.values()}
	if actual != expected:
		raise ValueError(f"Windows package FFmpeg runtime does not match bundled libraries: missing={sorted(expected - actual)}, extra={sorted(actual - expected)}")


def read_manifest(path: Path) -> set[str]:
	names = path.read_text(encoding="utf-8").splitlines()
	if any(not name or Path(name).name != name or "/" in name or "\\" in name or ":" in name for name in names):
		raise ValueError(f"invalid executable manifest: {path}")
	if len({name.casefold() for name in names}) != len(names):
		raise ValueError(f"duplicate executable manifest entries: {path}")
	return {name for name in names if name.casefold().endswith(".exe")}


def package_entries(package: Path) -> list[str]:
	if package.suffix.casefold() == ".zip":
		with zipfile.ZipFile(package) as archive:
			return [info.filename for info in archive.infolist() if not info.is_dir()]
	if package.suffix.casefold() != ".7z":
		raise ValueError(f"unsupported Windows package format: {package.suffix}")
	# 7z 的结构化清单无需解压，UTF-8 输出保留中文路径；失败必须阻断发布。
	result = subprocess.run(["7z", "l", "-slt", "-ba", "-sccUTF-8", str(package.resolve())], check=True, capture_output=True, encoding="utf-8", timeout=120)
	entries = []
	for block in result.stdout.replace("\r\n", "\n").split("\n\n"):
		fields = dict(line.split(" = ", 1) for line in block.splitlines() if " = " in line)
		if "Path" in fields and "D" not in fields.get("Attributes", "") and fields.get("Folder") != "+":
			entries.append(fields["Path"].replace("\\", "/"))
	if not entries:
		raise ValueError("Windows 7z package contains no files")
	return entries


def verify(package: Path, runtime_manifest: Path, tools_manifest: Path, portable: bool, ffmpeg_libdir: Path | None = None) -> None:
	runtime = read_manifest(runtime_manifest)
	tools = read_manifest(tools_manifest)
	required = {"DDNet.exe", "DDNet-Server.exe", "QmClient-Updater.exe"}
	if not required.issubset(runtime):
		raise ValueError("runtime manifest missing required client, server or updater")
	if portable and tools:
		raise ValueError("portable package must not declare tools")
	expected = runtime | tools
	names = package_entries(package)
	if any("?" in name or "\ufffd" in name for name in names):
		raise ValueError("Windows package has lossy file names")
	# 删除后的随包资源不能由旧打包缓存重新带回。
	removed = {name.casefold() for name in REMOVED_BUNDLED_RESOURCES}
	obsolete = [name for name in names if "/" in name and name.split("/", 1)[1].casefold() in removed]
	if obsolete:
		raise ValueError(f"removed bundled resources present in Windows package: {obsolete}")
	executables = [name for name in names if name.casefold().endswith(".exe")]
	counts = Counter(PurePosixPath(name).name.casefold() for name in executables)
	expected_names = {name.casefold() for name in expected}
	missing = sorted(name for name in expected_names if counts[name] != 1)
	extra = sorted(set(counts) - expected_names)
	# 每个程序须位于唯一包根目录中，与运行依赖 DLL 同级。
	roots = {PurePosixPath(name).parts[0] for name in executables}
	misplaced = [name for name in executables if len(PurePosixPath(name).parts) != 2 or "\\" in name]
	if missing or extra or misplaced or len(roots) != 1:
		raise ValueError(f"invalid Windows executable payload: missing/duplicate={missing}, extra={extra}, misplaced={misplaced}")
	# 包根必须携带 steam_appid.txt（SteamAPI_Init 在 CWD 查找），缺失会让非 Steam 位置启动失去运行状态与时长统计。
	if not any(PurePosixPath(name).parts[1:] == ("steam_appid.txt",) for name in names):
		raise ValueError("Windows package is missing root steam_appid.txt")
	check_ffmpeg_runtime(names, ffmpeg_libdir)


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--package", type=Path, action="append", required=True)
	parser.add_argument("--runtime-manifest", type=Path, required=True)
	parser.add_argument("--tools-manifest", type=Path, required=True)
	parser.add_argument("--portable", action="store_true")
	parser.add_argument("--ffmpeg-libdir", type=Path, help="bundled FFmpeg DLL directory the package must match")
	args = parser.parse_args()
	for package in args.package:
		verify(package, args.runtime_manifest, args.tools_manifest, args.portable, args.ffmpeg_libdir)
		print(f"Verified Windows runtime and tool executable inventory: {package}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
