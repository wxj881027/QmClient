"""GameClient 语言与图集加载的真实进程回归。"""

from __future__ import annotations

from pathlib import Path
import struct
import zlib

try:
	from qmclient_scripts.integration.process_harness import ProcessEnvironment
except ModuleNotFoundError:
	from process_harness import ProcessEnvironment  # type: ignore[no-redef]


def scenario_missing_language_preserves_previous_selection(env: ProcessEnvironment) -> None:
	"""语言文件暂时不可读时保留原配置和译文，文件恢复后可重选同一路径。"""
	active_file = "languages/qm_e2e_active.txt"
	pending_file = "languages/qm_e2e_pending.txt"
	active_path = env.path(active_file)
	active_path.parent.mkdir(parents=True, exist_ok=True)
	key = "Rank ghost: load a ghost first (Rank 1 page or qm_rank_ghost)"
	active_text = "Translation from the previous language"
	pending_text = "Translation from the recovered language"
	active_path.write_text(f"{key}\n== {active_text}\n", encoding="utf-8")

	client = env.start_client(
		["qm_steam_auto_launch 0", "cl_show_welcome 0",
		 "qm_echo_merge_window_ms 0", f'cl_languagefile "{active_file}"'],
		connect=False,
	)
	client.wait_for(lambda line: line == f"localization: loaded '{active_file}'", "initial language loaded", 15)
	client.command(f'cl_languagefile "{pending_file}"')
	client.wait_for(lambda line: line == f"localization: Couldn't open language file '{pending_file}'", "missing language rejected", 15)

	client.command("cl_languagefile")
	client.wait_for(lambda line: line == f"config: Value: {active_file}", "previous language still selected", 10)
	client.command("qm_rank_ghost_view")
	client.wait_for(lambda line: line.startswith("chat/client:") and line.endswith(active_text), "previous translation still used", 10)

	env.path(pending_file).write_text(f"{key}\n== {pending_text}\n", encoding="utf-8")
	client.command(f'cl_languagefile "{pending_file}"')
	client.wait_for(lambda line: line == f"localization: loaded '{pending_file}'", "recovered language loaded", 15)
	client.command("qm_rank_ghost_view")
	client.wait_for(lambda line: line.startswith("chat/client:") and line.endswith(pending_text), "new translation used", 10)
	client.command("quit")
	code = client.wait_for_exit(15)
	if code != 0:
		raise AssertionError(f"client exited with {code}")


def _write_atlas(path: Path, rgba: bool) -> None:
	"""写入普通 RGB 图集，或用作默认图集的透明 RGBA PNG。"""
	def chunk(kind: bytes, data: bytes) -> bytes:
		return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

	size = 128  # 可被 HUD 的 16 格及表情的 4 格整除。
	channels = 4 if rgba else 3
	color_type = 6 if rgba else 2
	header = struct.pack(">IIBBBBB", size, size, 8, color_type, 0, 0, 0)
	pixels = bytes((1 + size * channels) * size)
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))


def _assert_invalid_atlas_loads_default(env: ProcessEnvironment, asset: str, expected_warning: str) -> None:
	_write_atlas(env.path("assets", asset, "qm_e2e_rgb.png"), rgba=False)
	_write_atlas(env.path(f"{asset}.png"), rgba=True)

	client = env.start_client(
		["qm_steam_auto_launch 0", "cl_show_welcome 0",
		 "qm_blank_asset_fallback 0", f"cl_asset_{asset} qm_e2e_rgb"],
		connect=False,
	)
	# 已有透明格诊断只在有效图集的材质加载分支执行，本场景的透明格来自默认图集。
	client.wait_for(lambda line: line == expected_warning, "default atlas decoded and used", 15)
	client.command("quit")
	code = client.wait_for_exit(15)
	if code != 0:
		raise AssertionError(f"client exited with {code}")


def scenario_invalid_hud_atlas_loads_default(env: ProcessEnvironment) -> None:
	_assert_invalid_atlas_loads_default(env, "hud", "graphics: HUD asset 'hud_airjump' is empty and stays blank")


def scenario_invalid_emoticons_atlas_loads_default(env: ProcessEnvironment) -> None:
	_assert_invalid_atlas_loads_default(env, "emoticons", "graphics: Emoticon asset 'oop' is empty and stays blank")
