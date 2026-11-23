"""地图资源准备未结束时再次切图的真实进程回归。"""

from __future__ import annotations

import json
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import re
import struct
import threading
import time
import zlib

try:
	from qmclient_scripts.integration.process_harness import ProcessEnvironment
except ModuleNotFoundError:
	from process_harness import ProcessEnvironment  # type: ignore[no-redef]


def _write_loading_map(path: Path, sound_data: bytes) -> None:
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
	items.append((7, 0, [1, 0, -1, 1, 0]))
	item_data = bytearray()
	item_offsets = []
	for item_type, item_id, values in items:
		item_offsets.append(len(item_data))
		payload = struct.pack(f"<{len(values)}i", *values)
		item_data.extend(struct.pack("<ii", (item_type << 16) | item_id, len(payload)))
		item_data.extend(payload)
	item_types = b"".join(struct.pack("<iii", *entry) for entry in ((0, 0, 1), (1, 1, 1), (4, 2, 2), (5, 4, layer_count + 1), (7, 5 + layer_count, 1)))
	# v4 datafile：每个原始数据块独立压缩，偏移指向文件压缩区。
	tiles = bytes(16)
	compressed_tiles = zlib.compress(tiles)
	compressed_sound = zlib.compress(sound_data)
	compressed = compressed_tiles + compressed_sound
	data_index = struct.pack("<iiii", 0, len(compressed_tiles), len(tiles), len(sound_data))
	metadata = item_types + struct.pack(f"<{len(item_offsets)}i", *item_offsets) + data_index + item_data
	swap_size = 36 + len(metadata)
	header = struct.pack("<4s8i", b"DATA", 4, swap_size + len(compressed) - 16, swap_size - 16, 5, len(items), 2, len(item_data), len(compressed))
	path.parent.mkdir(parents=True, exist_ok=True)
	path.write_bytes(header + metadata + compressed)


def _wait_for_perf_connect_stage(env: ProcessEnvironment, stage: str, description: str, timeout: float) -> str:
	# perf/ 日志被生产 logger 路由到专用文件，不能等 stdout。
	deadline = time.monotonic() + timeout
	marker = "perf/connect: "
	while time.monotonic() < deadline:
		for path in env.temp_dir.glob("dumps/QmClient_Perf/qm_perf_*.log"):
			try:
				lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
			except OSError:
				continue
			for line in lines:
				if marker not in line:
					continue
				try:
					payload = json.loads(line.split(marker, 1)[1])
				except json.JSONDecodeError:
					continue
				if payload.get("stage") == stage:
					return line
		if not env.client.is_alive():
			raise RuntimeError(f"client exited while waiting for {description}")
		time.sleep(0.05)
	raise TimeoutError(f"timed out waiting for {description}")


def _wait_for_perf_connect_event(env: ProcessEnvironment, name: str, description: str, timeout: float) -> str:
	deadline = time.monotonic() + timeout
	marker = "perf/connect: "
	while time.monotonic() < deadline:
		for path in env.temp_dir.glob("dumps/QmClient_Perf/qm_perf_*.log"):
			try:
				lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
			except OSError:
				continue
			for line in lines:
				if marker not in line:
					continue
				try:
					payload = json.loads(line.split(marker, 1)[1])
				except json.JSONDecodeError:
					continue
				if payload.get("event") == name:
					return line
		if not env.client.is_alive():
			raise RuntimeError(f"client exited while waiting for {description}")
		time.sleep(0.05)
	raise TimeoutError(f"timed out waiting for {description}")


def _wait_for_perf_map_event(env: ProcessEnvironment, name: str, description: str, timeout: float, *, from_download: bool = False) -> str:
	deadline = time.monotonic() + timeout
	marker = "perf/map: "
	while time.monotonic() < deadline:
		for path in env.temp_dir.glob("dumps/QmClient_Perf/qm_perf_*.log"):
			try:
				lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
			except OSError:
				continue
			for line in lines:
				if marker not in line:
					continue
				try:
					payload = json.loads(line.split(marker, 1)[1])
				except json.JSONDecodeError:
					continue
				if payload.get("event") == name and (not from_download or payload.get("from_download") == 1):
					return line
		if not env.client.is_alive():
			raise RuntimeError(f"client exited while waiting for {description}")
		time.sleep(0.05)
	raise TimeoutError(f"timed out waiting for {description}")


class _QuietMapHttpHandler(SimpleHTTPRequestHandler):
	def log_message(self, format: str, *args: object) -> None:
		pass


def scenario_map_change_during_resource_preparation(env: ProcessEnvironment) -> None:
	"""旧地图尚未准备完成时切图，新地图仍能进服、聊天并再次切图。"""
	map_name = "qm_e2e_loading"
	SoundPath = Path(__file__).resolve().parents[2] / "data" / "audio" / "crash_music" / "qm_festive_haoyunlai.opus"
	_write_loading_map(env.path("maps", f"{map_name}.map"), SoundPath.read_bytes())
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
	_wait_for_perf_connect_stage(env, "connected_total", "map preparation started", 60)
	_wait_for_perf_map_event(env, "map_parse_worker_start", "map parser worker submission", 10)
	parse_result = _wait_for_perf_map_event(env, "map_parse_worker_complete", "map parser result publication", 30)
	if '"success":1' not in parse_result:
		raise AssertionError(f"map parser worker did not publish a valid map: {parse_result}")
	_wait_for_perf_connect_event(env, "map_sounds_decode_start", "map sound worker submission", 10)
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
			lambda line: line.startswith("server: player has entered the game") and sum("server: player has entered the game" in entry for entry in env.server._lines) > entered_before,
			f"player entered {current_map} after resource preparation",
			30,
		)
		marker = f"qm_map_reentry_chat_{current_map}"
		env.client.command(f"say {marker}")
		env.server.wait_for(lambda line: marker in line, "chat received by server after map change", 15)
		env.client.wait_for(lambda line: "chat/all:" in line and marker in line, "chat returned to client after map change", 15)
	env.client.command("cl_refresh_rate 300")
	env.server.command(f"sv_map {map_name}")
	_wait_for_perf_connect_event(env, "map_sounds_decode_complete", "map sound worker publication", 30)
	env.server.command("sv_map Tutorial")
	env.client.wait_for(lambda line: "Recording to 'demos/auto/Tutorial_" in line, "connection after sound map preparation", 30)
	env.client.command("quit")
	code = env.client.wait_for_exit(15)
	if code != 0:
		raise AssertionError(f"client exited with {code} after interrupting map preparation")
	reports = [path for path in env.path("dumps", "QmClient_Crash").glob("*") if path.is_file()]
	if reports:
		raise AssertionError(f"client produced crash diagnostics: {reports}")


def scenario_http_map_download_enters_game(env: ProcessEnvironment) -> None:
	"""首次 HTTP 下载只发布一次，客户端在原连接上进入游戏。"""
	map_name = "qm_e2e_http_map"
	map_path = env.path("maps", f"{map_name}.map")
	sound_path = Path(__file__).resolve().parents[2] / "data" / "audio" / "crash_music" / "qm_festive_haoyunlai.opus"
	_write_loading_map(map_path, sound_path.read_bytes())
	http_root = env.path("map-http")
	http_root.mkdir()
	with ThreadingHTTPServer(("127.0.0.1", 0), partial(_QuietMapHttpHandler, directory=str(http_root))) as http:
		http_thread = threading.Thread(target=http.serve_forever, daemon=True)
		http_thread.start()
		try:
			env.start_server()
			env.server.command("stdout_output_level 2")
			env.server.command(f"sv_maps_base_url http://127.0.0.1:{http.server_port}/")
			env.server.command(f"sv_map {map_name}")
			loaded = env.server.wait_for(lambda line: f"{map_name}.map sha256 is " in line, "server loaded the HTTP fixture", 15)
			sha256 = re.search(r"sha256 is ([0-9a-f]{64})$", loaded)
			if sha256 is None:
				raise AssertionError(f"server did not report the fixture SHA-256: {loaded}")
			filename = f"{map_name}_{sha256.group(1)}.map"
			map_data = map_path.read_bytes()
			(http_root / filename).write_bytes(map_data)
			map_path.unlink()
			client = env.start_client(["stdout_output_level 2", "http_allow_insecure 1", "qm_perf_debug 1", "qm_steam_auto_launch 0", "cl_auto_demo_record 1", "minimize"], env={"QMCLIENT_TEST_HIDE_DIALOG": "1", "NO_PROXY": "127.0.0.1"}, startup_timeout=60)
			client.wait_for(lambda line: f"task done: http://127.0.0.1:{http.server_port}/{filename}" in line, "HTTP map transfer succeeded", 20)
			result = _wait_for_perf_map_event(env, "map_parse_worker_complete", "downloaded map parsed", 20, from_download=True)
			if '"success":1' not in result:
				raise AssertionError(f"downloaded map could not be parsed: {result}")
			client.wait_for(lambda line: f"Recording to 'demos/auto/{map_name}_" in line, "client entered on the first connection", 30)
			if sum("client/network: download complete, loading map" in line for line in client._lines) != 1:
				raise AssertionError("the HTTP map completion was not consumed exactly once")
			map_events = []
			for path in env.temp_dir.glob("dumps/QmClient_Perf/qm_perf_*.log"):
				for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
					if "perf/map: " not in line:
						continue
					try:
						payload = json.loads(line.split("perf/map: ", 1)[1])
					except json.JSONDecodeError:
						continue
					if payload.get("from_download") == 1 and payload.get("event") in {"map_parse_worker_start", "map_parse_worker_complete"}:
						map_events.append(payload["event"])
			if map_events.count("map_parse_worker_start") != 1 or map_events.count("map_parse_worker_complete") != 1:
				raise AssertionError(f"download map job was consumed more than once or not completed: {map_events}")
			if env.path("downloadedmaps", filename).read_bytes() != map_data:
				raise AssertionError("the published map differs from the HTTP fixture")
			client.command("quit")
			if (code := client.wait_for_exit(15)) != 0:
				raise AssertionError(f"client exited with {code} after HTTP map loading")
		finally:
			http.shutdown()
			http_thread.join(timeout=5)


def scenario_current_map_entity_background_during_loading(env: ProcessEnvironment) -> None:
	"""切图握手等待时选择当前地图作实体背景，恢复接收后正常进服。"""
	env.start_server()
	env.server.command("stdout_output_level 2")
	env.server.command("sv_map Tutorial")
	env.server.wait_for(lambda line: "Tutorial.map sha256 is " in line, "server selected the initial map", 15)
	client = env.start_client(
		[
			"stdout_output_level 2",
			"cl_show_welcome 0",
			"qm_steam_auto_launch 0",
			"qm_auto_update 0",
			"qm_websocket_url disabled",
			"cl_auto_demo_record 1",
			'cl_background_entities ""',
			"minimize",
		],
		env={"QMCLIENT_TEST_HIDE_DIALOG": "1"},
		startup_timeout=60,
	)
	client.wait_for(lambda line: "Recording to 'demos/auto/Tutorial_" in line, "initial client connection", 30)

	# 丢包只作用于本地服务端的接收方向，切图通知仍会发出，握手会等到恢复接收。
	env.server.command("sv_net_fake_loss 100")
	env.server.command("sv_map ctf1")
	client.wait_for(lambda line: line == "client/map: loaded map 'ctf1'", "replacement map opened", 30)
	marker = "qm_e2e_current_background_selected"
	client.command(f'cl_background_entities "%current%"; echo {marker}')
	client.wait_for(lambda line: line == f"console: {marker}", "current map background command completed", 10)

	env.server.command("sv_net_fake_loss 0")
	client.wait_for(lambda line: "Recording to 'demos/auto/ctf1_" in line, "client entered the replacement map", 30)
	client.command("quit")
	if (code := client.wait_for_exit(15)) != 0:
		raise AssertionError(f"client exited with {code} after selecting the current map background")
