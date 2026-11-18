#!/usr/bin/env python3
"""真实便携客户端的字体扫描、SFNT 名称和多面集合回归。"""

from __future__ import annotations

import argparse
import json
import os
import re
from pathlib import Path
import shutil
import socket
import tempfile
import uuid

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTCollection, TTFont

try:
	from qmclient_scripts.integration.icon_resources_smoke import prepare_client, wait_new
	from qmclient_scripts.integration.process_harness import Process
except ModuleNotFoundError:
	from icon_resources_smoke import prepare_client, wait_new
	from process_harness import Process

REPO_ROOT = Path(__file__).resolve().parents[2]
SCENARIOS = ("downloaded-font-unicode-name", "user-only-unicode-name", "data-only-family-and-book", "corrupt-user-shadow-falls-back", "collection-and-cross-directory-duplicates", "bundled-typographic-families", "canonical-alias-priority")


def fixture_font(family: str, style: str = "Regular", bad_english_name: bool = False) -> TTFont:
	# 生成最小合法轮廓字体，让 FreeType 实际解析名称、cmap 和字形，不依赖系统字体。
	glyphs = [".notdef", "space", "A", "uni4E2D"]
	builder = FontBuilder(1000, isTTF=True)
	builder.setupGlyphOrder(glyphs)
	builder.setupCharacterMap({32: "space", 65: "A", 0x4E2D: "uni4E2D"})
	outlines = {}
	for name in glyphs:
		pen = TTGlyphPen(None)
		if name != "space":
			pen.moveTo((80, 0))
			pen.lineTo((450, 0))
			pen.lineTo((450, 700))
			pen.lineTo((80, 700))
			pen.closePath()
		outlines[name] = pen.glyph()
	builder.setupGlyf(outlines)
	advance = 800 if style == "Book" else 600
	builder.setupHorizontalMetrics({name: (advance, 80 if name != "space" else 0) for name in glyphs})
	builder.setupHorizontalHeader(ascent=800, descent=-200)
	# PostScript 标识只用 ASCII，与供玩家选择的本地化字体族名称分离。
	identifier = "QmFixture" + "".join(str(ord(character)) for character in family) + style
	builder.setupNameTable({"familyName": "???" if bad_english_name else family, "styleName": style, "uniqueFontIdentifier": identifier, "fullName": f"{family} {style}", "psName": identifier})
	if bad_english_name:
		# 英文记录保留损坏的问号名称，中文 Windows 记录是有效 UTF-16BE。
		builder.font["name"].setName(family, 1, 3, 1, 0x0804)
		builder.font["name"].setName(style, 2, 3, 1, 0x0804)
	builder.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200)
	builder.setupPost()
	builder.setupMaxp()
	return builder.font


def write_font(path: Path, family: str, style: str = "Regular", bad_english_name: bool = False, variable: bool = False) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	font = fixture_font(family, style, bad_english_name)
	if variable:
		# 真实 fvar/gvar 轴供 FreeType 查询；轮廓不变，避免把字形光栅差异混入族选择测试。
		builder = FontBuilder(font=font)
		builder.setupFvar([("wght", 200, 400, 800, "Weight")], [])
		builder.setupGvar({name: [] for name in font.getGlyphOrder()})
	try:
		font.save(path)
	finally:
		font.close()


def write_collection(path: Path, families: tuple[str, str]) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	collection = TTCollection()
	collection.fonts = [fixture_font(family, style) for family in families for style in ("Regular", "Book")]
	try:
		collection.save(path)
	finally:
		collection.close()


def create_scenario_fonts(directory: Path, name: str) -> list[tuple[str, tuple[str, ...]]]:
	profile = directory / "profile"
	data = directory / "data"
	if name == "downloaded-font-unicode-name":
		family = "Qm商店下载字体"
		write_font(profile / "fonts/fonts_store/downloaded-user.ttf", family, "Book", True)
		return [(family, ("Book",))]
	if name == "user-only-unicode-name":
		family = "Qm字体中文族"
		write_font(profile / "fonts/only-user.ttf", family, "Book", True)
		return [(family, ("Book",))]
	if name == "data-only-family-and-book":
		family = "Qm Data Font"
		for style in ("Regular", "Book"):
			write_font(data / f"fonts/data-only-{style.lower()}.ttf", family, style)
		return [(family, ("Regular", "Book"))]
	if name == "corrupt-user-shadow-falls-back":
		family = "Qm Shadow Recovery"
		relative = "fonts/shared-relative.ttf"
		write_font(data / relative, family)
		corrupt = profile / relative
		corrupt.parent.mkdir(parents=True, exist_ok=True)
		corrupt.write_bytes(b"corrupt user font must not block the bundled same-relative font")
		return [(family, ("Regular",))]
	if name == "collection-and-cross-directory-duplicates":
		families = ("Qm Collection One", "Qm Collection Two")
		collection = data / "fonts/multi-family.ttc"
		write_collection(collection, families)
		return [(family, ("Regular", "Book")) for family in families]
	if name == "bundled-typographic-families":
		# 使用真实发行资源，验证排版族与 TTC 区域面组织。
		shutil.copytree(REPO_ROOT / "data/fonts/Poppins", data / "fonts/Poppins", dirs_exist_ok=True)
		shutil.copy2(REPO_ROOT / "data/fonts/SourceHanSans.ttc", data / "fonts/SourceHanSans.ttc")
		return [("Poppins", ("Regular", "Light", "Medium", "Bold")), ("Source Han Sans", ("Regular", "K Regular", "SC Regular", "TC Regular", "HC Regular", "HW Regular", "HW K Regular", "HW SC Regular", "HW TC Regular", "HW HC Regular"))]
	if name == "canonical-alias-priority":
		shutil.copytree(REPO_ROOT / "data/fonts/Poppins", data / "fonts/Poppins", dirs_exist_ok=True)
		return [("Poppins", ("Regular", "Light", "Medium", "Bold"))]
	raise ValueError(f"unknown font scenario: {name}")


def probe_font(client: Process, request: str, family: str, style: str, codepoint: int = 0x4E2D, synthetic: bool = True) -> dict[str, object]:
	offset = len(client._lines)
	client.command(f'qm_font_diagnostics "{request}" {codepoint}')
	line = wait_new(client, offset, lambda value: "event=font_diagnostics " in value and f"request='{request}' " in value, f"resolved glyph for {request}")
	fields = dict((key, quoted if quoted else plain) for key, quoted, plain in re.findall(r"([a-z_]+)=(?:'([^']*)'|([^ ]+))", line))
	if fields.get("resolved") != "1" or fields.get("family") != family or fields.get("style") != style:
		raise AssertionError(f"font resolved to unexpected face: {line}")
	if int(fields.get("glyph", "0")) <= 0 or fields.get("load_error") != "0":
		raise AssertionError(f"real FreeType glyph load failed: {line}")
	expected_advance = 800 if style == "Book" else 600
	if synthetic and int(fields.get("advance", "0")) != expected_advance:
		raise AssertionError(f"glyph came from wrong style: {line}")
	current = client._lines[offset:]
	selection = next((raw for raw in current if "event=font_selection " in raw and f"request='{request}' " in raw), "")
	if f"family='{family}'" not in selection or f"style='{style}'" not in selection or "resolved=1" not in selection:
		raise AssertionError(f"UI selection API disagrees with actual glyph face: {selection}")
	families = [match.group(1) for raw in current if (match := re.search(r"event=font_family family='([^']+)'", raw))]
	if families.count(family) != 1 or "???" in families or f"{family} Book" in families or f"{family} Regular" in families:
		raise AssertionError(f"family list has lost Unicode, duplicated family or style suffix: {families}")
	styles = [match.group(1) for raw in current if (match := re.search(r"event=font_style request='[^']+' face='([^']+)'", raw))]
	return {"request": request, "family": family, "style": style, "glyph": int(fields["glyph"]), "advance": int(fields["advance"]), "families_count": int(fields["families"]), "faces_count": int(fields["faces"]), "families": families, "styles": styles}


def probe_family(client: Process, family: str, expected_config: str | None, expected_styles: set[str], cjk: bool = False, weight_range: tuple[int, int] | None = None) -> str:
	offset = len(client._lines)
	client.command(f'qm_font_diagnostics "{family}" 65')
	wait_new(client, offset, lambda value: "event=font_diagnostics " in value and f"request='{family}' " in value, f"family selection for {family}")
	current = client._lines[offset:]
	line = next((raw for raw in current if "event=font_family_selection " in raw and f"request='{family}' " in raw), "")
	fields = dict((key, quoted if quoted else plain) for key, quoted, plain in re.findall(r"([a-z_]+)=(?:'([^']*)'|([^ ]+))", line))
	if fields.get("resolved") != ("1" if expected_config is not None else "0") or fields.get("config", "") != (expected_config or ""):
		raise AssertionError(f"explicit family query resolved to a face alias: {line}")
	if fields.get("cjk") != str(int(cjk)):
		raise AssertionError(f"CJK eligibility sampled the wrong family face: {line}")
	if fields.get("variable") != ("1" if weight_range is not None else "0"):
		raise AssertionError(f"variable axis query sampled the wrong family face: {line}")
	if weight_range is not None and (int(fields.get("min", "0")), int(fields.get("max", "0"))) != weight_range:
		raise AssertionError(f"variable axis limits came from another face: {line}")
	styles = [match.group(1) for raw in current if (match := re.search(r"event=font_family_style request='[^']+' face='([^']+)'", raw))]
	if set(styles) != expected_styles or len(styles) != len(expected_styles):
		raise AssertionError(f"family style query used full-face parsing: {styles}")
	return fields.get("config", "")


def run_font_scenario(source: Path, workspace: Path, name: str, proxy: str) -> dict[str, object]:
	directory = workspace / name
	prepare_client(source, directory)
	expected = create_scenario_fonts(directory, name)
	cwd = directory / "foreign-cwd"
	cwd.mkdir()
	environment = {key: proxy for key in ("http_proxy", "https_proxy", "all_proxy", "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY")}
	environment.update({"no_proxy": "", "NO_PROXY": "", "DDNET_DRIVER": "OpenGL"})
	arguments = [
		str(directory / source.name),
		"gfx_fullscreen 2",
		"gfx_screen_width 960",
		"gfx_screen_height 540",
		"gfx_backgroundrender 1",
		"gfx_backend OpenGL",
		"qm_graphics_mode -1",
		"cl_show_welcome 0",
		"cl_skip_start_menu 1",
		"ui_page 16",
		"stdout_output_level 1",
		"cl_save_settings 0",
		"qm_auto_update 0",
		"qm_steam_auto_launch 0",
		"qm_graphics_trace 1",
		"cl_languagefile languages/simplified_chinese.txt",
	]
	client = Process("client", arguments, cwd, fifo_command="cl_input_fifo", pipe_prefix=f"qm_fonts_{uuid.uuid4().hex}_", env=environment)
	try:
		client.wait_for(lambda line: line.startswith("client: version"), "client startup", 30)
		client.command("echo qm_font_loop_ready")
		client.wait_for(lambda line: "qm_font_loop_ready" in line, "main menu loop", 30)
		if name == "downloaded-font-unicode-name":
			# 旧目录应完全不可见；移动到正式商店目录后，生产重扫才能识别。
			for legacy_index, legacy_directory in enumerate(("qmclient/fonts", "fonts/downloaded_fonts")):
				family = f"Qm旧商店目录字体{legacy_index}"
				old = directory / "profile" / legacy_directory / "old-only.ttf"
				write_font(old, family, "Regular")
				offset = len(client._lines)
				client.command(f'qm_font_diagnostics "{family}" 65')
				line = wait_new(client, offset, lambda value: "event=font_diagnostics " in value and f"request='{family}' " in value, "old directory excluded")
				if "resolved=0" not in line:
					raise AssertionError(f"obsolete font directory was scanned: {line}")
				current = directory / "profile/fonts/fonts_store" / f"moved-{legacy_directory.replace('/', '-')}.ttf"
				old.replace(current)
				probe_font(client, family, family, "Regular", 65)

		if any("added path '$USERDIR'" in line for line in client._lines):
			raise AssertionError("client registered real user storage; use a dedicated portable build")
		observations = []
		baseline_counts = None
		for family, styles in expected:
			for style in styles:
				request = f"{family} {style}"
				first = probe_font(client, request, family, style, 65 if name in ("bundled-typographic-families", "canonical-alias-priority") else 0x4E2D, name not in ("bundled-typographic-families", "canonical-alias-priority"))
				expected_styles = {f"{family} {item}" for item in styles}
				if set(first["styles"]) != expected_styles or len(first["styles"]) != len(expected_styles):
					raise AssertionError(f"style inventory duplicated or incomplete: {first}")
				if name == "collection-and-cross-directory-duplicates" and baseline_counts is None:
					# 首次查询记录单份集合的池大小，再把同内容放入用户目录并让生产接口重扫。
					duplicate = directory / "profile/fonts/fonts_store/multi-family.ttc"
					duplicate.parent.mkdir(parents=True, exist_ok=True)
					shutil.copy2(directory / "data/fonts/multi-family.ttc", duplicate)
				second = probe_font(client, request, family, style, 65, name not in ("bundled-typographic-families", "canonical-alias-priority"))
				counts = (second["families_count"], second["faces_count"])
				if counts != (first["families_count"], first["faces_count"]):
					raise AssertionError(f"repeat scan added duplicate faces: {first} -> {second}")
				if baseline_counts is not None and baseline_counts != counts:
					raise AssertionError(f"queries changed font pool size: {baseline_counts} -> {counts}")
				baseline_counts = counts
				observations.extend((first, second))
		# 验证旧配置保存的紧凑拼接 family+style 仍解析为同一真实面。
		family, styles = expected[0]
		compact_style = styles[-1]
		observations.append(probe_font(client, family + compact_style, family, compact_style, 65 if name in ("bundled-typographic-families", "canonical-alias-priority") else 0x4E2D, name not in ("bundled-typographic-families", "canonical-alias-priority")))
		if name == "bundled-typographic-families":
			for request, family, style in (("Poppins Light Regular", "Poppins", "Light"), ("Poppins Medium Regular", "Poppins", "Medium"), ("Source Han Sans SC", "Source Han Sans", "SC Regular"), ("SourceHanSansHWSC", "Source Han Sans", "HW SC Regular")):
				observations.append(probe_font(client, request, family, style, 65, False))
			for observation in observations:
				if any(value.startswith("Poppins ") or value.startswith("Source Han Sans ") for value in observation["families"]):
					raise AssertionError(f"face variants escaped into family list: {observation}")
		if name == "canonical-alias-priority":
			# 先确认旧名命中并进入缓存，再安装与其冲突的真实 canonical family/style。
			observations.append(probe_font(client, "Poppins Light Regular", "Poppins", "Light", 65, False))
			probe_family(client, "Poppins Light", None, set())
			write_font(directory / "profile/fonts/zz-canonical-collision.ttf", "Poppins Light", "Regular", variable=True)
			observations.append(probe_font(client, "Poppins Light Regular", "Poppins Light", "Regular", 65))
			config = probe_family(client, "Poppins Light", "Poppins Light Regular", {"Poppins Light Regular"}, True, (200, 800))
			observations.append(probe_font(client, config, "Poppins Light", "Regular", 65))
			probe_family(client, "Poppins Light", config, {"Poppins Light Regular"}, True, (200, 800))
			observations.append(probe_font(client, "Poppins Light", "Poppins", "Light", 65, False))
		return {"scenario": name, "status": "passed", "observations": observations}
	finally:
		client.stop()
		(directory / "client.log").write_text("\n".join(client._lines) + "\n", encoding="utf-8")


def smoke_downloaded_font_unicode_name(source: Path, workspace: Path, proxy: str) -> dict[str, object]:
	return run_font_scenario(source, workspace, "downloaded-font-unicode-name", proxy)


def smoke_user_only_unicode_name(source: Path, workspace: Path, proxy: str) -> dict[str, object]:
	return run_font_scenario(source, workspace, "user-only-unicode-name", proxy)


def smoke_data_only_family_and_book(source: Path, workspace: Path, proxy: str) -> dict[str, object]:
	return run_font_scenario(source, workspace, "data-only-family-and-book", proxy)


def smoke_corrupt_user_shadow_falls_back(source: Path, workspace: Path, proxy: str) -> dict[str, object]:
	return run_font_scenario(source, workspace, "corrupt-user-shadow-falls-back", proxy)


def smoke_collection_and_cross_directory_duplicates(source: Path, workspace: Path, proxy: str) -> dict[str, object]:
	return run_font_scenario(source, workspace, "collection-and-cross-directory-duplicates", proxy)


def smoke_bundled_typographic_families(source: Path, workspace: Path, proxy: str) -> dict[str, object]:
	return run_font_scenario(source, workspace, "bundled-typographic-families", proxy)


def smoke_canonical_alias_priority(source: Path, workspace: Path, proxy: str) -> dict[str, object]:
	return run_font_scenario(source, workspace, "canonical-alias-priority", proxy)


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--client", type=Path, required=True, help="dedicated QMCLIENT_PORTABLE executable")
	args = parser.parse_args()
	source = args.client.resolve()
	cache = source.parent / "CMakeCache.txt"
	if os.name != "nt" or not source.is_file() or not cache.is_file() or "QMCLIENT_PORTABLE:BOOL=ON" not in cache.read_text(encoding="utf-8"):
		parser.error("--client must be a Windows QMCLIENT_PORTABLE=ON build")
	artifacts = REPO_ROOT / "tmp/font-resources-smoke"
	artifacts.mkdir(parents=True, exist_ok=True)
	workspace = Path(tempfile.mkdtemp(prefix="run_", dir=artifacts))
	results = []
	reservation = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	reservation.bind(("127.0.0.1", 0))
	proxy = f"http://127.0.0.1:{reservation.getsockname()[1]}"
	try:
		for run in (smoke_downloaded_font_unicode_name, smoke_user_only_unicode_name, smoke_data_only_family_and_book, smoke_corrupt_user_shadow_falls_back, smoke_collection_and_cross_directory_duplicates, smoke_bundled_typographic_families, smoke_canonical_alias_priority):
			result = run(source, workspace, proxy)
			results.append(result)
			print(f"PASS {result['scenario']}", flush=True)
		return 0
	except Exception as error:
		results.append({"status": "failed", "error": str(error)})
		print(f"FAIL {error}", flush=True)
		return 1
	finally:
		reservation.close()
		(workspace / "results.json").write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
		print(f"artifacts: {workspace}", flush=True)


if __name__ == "__main__":
	raise SystemExit(main())
