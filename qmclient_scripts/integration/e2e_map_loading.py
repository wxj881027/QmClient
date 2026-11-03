"""地图资源准备未结束时再次切图的真实进程回归。"""

from __future__ import annotations

import json
from pathlib import Path
import struct
import zlib

try:
	from qmclient_scripts.integration.process_harness import ProcessEnvironment
except ModuleNotFoundError:
	from process_harness import ProcessEnvironment  # type: ignore[no-redef]


def _write_loading_map(path: Path) -> None:
	# 空四边形层仍逐帧初始化；将游戏层放在最后，让外部命令有足够时间中断准备。
	layer_count = 512
	# 名称按每字节偏移 128 的整数编码保存，最后一个字节保留为零。
	empty_name = list(struct.unpack(">3i", b"\x80" * 11 + b"\x00"))
	items = [
		(0, 0, [1]),
		(1, 0, [1, -1, -1, -1, -1]),
		(4, 0, [3, 0, 0, 100, 100, 0, layer_count, 0, 0, 0, 0, 0, *empty_name]),
		(4, 1, [3, 0, 0, 100, 100, layer_count, 1, 0, 0, 0, 0, 0, *empty_name]),
	]
	items.extend((5, index, [0, 3, 0, 2, 0, -1, -1, *empty_name]) for index in range(layer_count))
	items.append((5, layer_count, [0, 2, 0, 3, 2, 2, 1, 255, 255, 255, 255, -1, 0, -1, 0, *empty_name, -1, -1, -1, -1, -1]))
	item_data = bytearray()
	item_offsets = []
	for item_type, item_id, values in items:
		item_offsets.append(len(item_data))
		payload = struct.pack(f"<{len(values)}i", *values)
		item_data.extend(struct.pack("<ii", (item_type << 16) | item_id, len(payload)))
		item_data.extend(payload)
	item_types = b"".join(struct.pack("<iii", *entry) for entry in ((0, 0, 1), (1, 1, 1), (4, 2, 2), (5, 4, layer_count + 1)))
	# v4 datafile：条目区与整数索引参与换序，唯一的原始数据块使用 zlib 压缩。
	tiles = bytes(16)
	compressed = zlib.compress(tiles)
	metadata = item_types + struct.pack(f"<{len(item_offsets)}i", *item_offsets) + struct.pack("<ii", 0, len(tiles)) + item_data
	swap_size = 36 + len(metadata)
	header = struct.pack("<4s8i", b"DATA", 4, swap_size + len(compressed) - 16, swap_size - 16, 4, len(items), 1, len(item_data), len(compressed))
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_bytes(header + metadata + compressed)


def scenario_map_change_during_resource_preparation(env: ProcessEnvironment) -> None:
	"""旧地图尚未准备完成时切图，新地图仍能进服、聊天并再次切图。"""
	map_name = "qm_e2e_loading"
	_write_loading_map(env.path("maps", f"{map_name}.map"))
	env.start_server()
	env.server.command("stdout_output_level 2")
	env.server.command(f"sv_map {map_name}")
	env.server.wait_for(lambda line: f"{map_name}.map sha256 is " in line, "server loaded the preparation fixture", 15)
	env.start_client(
		[
			"stdout_output_level 2",
			"qm_steam_auto_launch 0",
			"qm_perf_debug 1",
			"cl_auto_demo_record 1",
			"cl_refresh_rate 20",
			"cl_refresh_rate_inactive 20",
			"minimize",
		],
		env={"QMCLIENT_TEST_HIDE_DIALOG": "1"},
		startup_timeout=60,
	)
	env.client.wait_for(
		lambda line: line.startswith("perf/connect: ") and json.loads(line.removeprefix("perf/connect: ")).get("stage") == "connected_total",
		"map preparation started",
		60,
	)
	entered_before = sum("server: player has entered the game" in line for line in env.server._lines)
	env.server.command("sv_map Tutorial")
	# 正向观察中断，避免场景在旧地图已完成准备后退化成普通切图而误报通过。
	env.client.wait_for(
		lambda line: line == f"client/map: canceled pending resource preparation for '{map_name}'",
		"previous preparation canceled while still loading",
		15,
	)
	for current_map in ("Tutorial", "ctf1"):
		if current_map == "ctf1":
			entered_before = sum("server: player has entered the game" in line for line in env.server._lines)
			env.server.command("sv_map ctf1")
		env.client.wait_for(lambda line: f"Recording to 'demos/auto/{current_map}_" in line, f"{current_map} connection completed", 30)
		env.server.wait_for(
			lambda line: line.startswith("server: player has entered the game")
			and sum("server: player has entered the game" in entry for entry in env.server._lines) > entered_before,
			f"player entered {current_map} after resource preparation",
			30,
		)
		marker = f"qm_map_reentry_chat_{current_map}"
		env.client.command(f"say {marker}")
		env.server.wait_for(lambda line: marker in line, "chat received by server after map change", 15)
		env.client.wait_for(lambda line: "chat/all:" in line and marker in line, "chat returned to client after map change", 15)
	env.client.command("quit")
	code = env.client.wait_for_exit(15)
	if code != 0:
		raise AssertionError(f"client exited with {code} after interrupting map preparation")
	reports = [path for path in env.path("dumps", "QmClient_Crash").glob("*") if path.is_file()]
	if reports:
		raise AssertionError(f"client produced crash diagnostics: {reports}")
