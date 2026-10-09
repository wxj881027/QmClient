#!/usr/bin/env python3
"""真实客户端通过本机 WebSocket 验证压缩名单、恢复、重连及旧格式兼容。"""

from __future__ import annotations

import argparse
import json
import struct
import tempfile
import threading
import time
from pathlib import Path
import zlib

from websockets.exceptions import ConnectionClosed
from websockets.sync.server import serve

try:
	from qmclient_scripts.integration.process_build import prepare_process_build
	from qmclient_scripts.integration.process_harness import EXE_SUFFIX, Process
except ModuleNotFoundError:
	from process_build import prepare_process_build
	from process_harness import EXE_SUFFIX, Process


def encoded(revision: int, base: int, users: int | None = None) -> bytes:
	data = {
		"server_address": "",
		"revision": revision,
		"base_revision": base,
		"full": base == 0,
		"lease_seconds": 20,
		"servers": [["127.0.0.1:8303", users, 1]] if users is not None else [],
		"players": [],
		"removed_servers": [],
		"removed_players": [],
	}
	message = json.dumps({"type": "users_sync", "v": 2, "data": data}).encode()
	return b"QMU1" + struct.pack(">I", len(message)) + zlib.compress(message, 1)


def receive(socket, expected: str) -> dict:
	deadline = time.monotonic() + 10
	while time.monotonic() < deadline:
		message = json.loads(socket.recv(timeout=max(0.01, deadline - time.monotonic())))
		if message.get("type") == expected:
			return message
		if message.get("type") == "subscribe_users":
			raise AssertionError("有效完整快照或增量触发了额外恢复请求")
	raise TimeoutError(f"等待 {expected} 超时")


def barrier(socket) -> None:
	socket.send(json.dumps({"type": "ping"}))
	receive(socket, "pong")


def run_client(build_dir: Path, legacy: bool) -> None:
	name = "legacy" if legacy else "compressed_recovery"
	temp_root = Path(__file__).resolve().parents[2] / "tmp"
	temp_root.mkdir(exist_ok=True)
	artifacts = Path(tempfile.mkdtemp(prefix=f"realtime_{name}_", dir=temp_root))
	(artifacts / "storage.cfg").write_text(f"add_path .\nadd_path {build_dir / 'data'}\n", encoding="utf-8")
	completed = threading.Event()
	errors: list[str] = []
	connections = 0
	client: Process | None = None

	def handler(socket) -> None:
		nonlocal connections
		try:
			hello = receive(socket, "hello")
			assert "users-sync-zlib-v1" in hello.get("capabilities", []), "客户端未声明压缩同步能力"
			connections += 1
			if legacy:
				# 旧服务忽略能力声明；仍然返回已支持的全局 users 文本格式。
				rows = [{"server_address": "127.0.0.1:8303", "player_name": f"player-{i}"} for i in range(4)]
				rows[3]["dummy"] = True
				socket.send(json.dumps({"type": "users", "v": 2, "data": {"server_address": "users", "users": rows, "lease_seconds": 20}}))
				barrier(socket)
			elif connections == 1:
				socket.send(encoded(1, 0, 3))
				barrier(socket)
				socket.send(encoded(2, 1, 5))
				socket.send(encoded(4, 3))
				receive(socket, "subscribe_users")
				socket.send(encoded(5, 0, 7))
				socket.send(encoded(6, 5, 8))
				barrier(socket)
				socket.close(1001, "smoke reconnect")
				return
			else:
				assert connections == 2, "客户端发生了额外重连"
				# 新连接从版本 1 开始，不能沿用上一连接的版本 6。
				socket.send(encoded(1, 0, 6))
				socket.send(encoded(2, 1, 9))
				barrier(socket)
			completed.set()
			while True:
				message = json.loads(socket.recv(timeout=15))
				if message.get("type") == "subscribe_users":
					raise AssertionError("完成同步后仍反复请求恢复")
				if message.get("type") == "stop":
					socket.send(json.dumps({"type": "playtime", "v": 2, "data": {"action": "stop", "last_stop_at": message.get("stop_at", 0), "playtime_seconds": 0}}))
		except ConnectionClosed:
			if not completed.is_set():
				errors.append("同步完成前连接关闭")
				completed.set()
		except Exception as error:
			errors.append(str(error))
			completed.set()

	with serve(handler, "127.0.0.1", 0, subprotocols=["qmclient-json"], compression=None, close_timeout=2) as server:
		thread = threading.Thread(target=server.serve_forever, daemon=True)
		thread.start()
		port = server.socket.getsockname()[1]
		try:
			client = Process(
				"realtime-client",
				[str(build_dir / f"DDNet{EXE_SUFFIX}"), "gfx_fullscreen 0", "gfx_screen_width 640", "gfx_screen_height 480", "snd_enable 0", "cl_show_welcome 0", "cl_save_settings 0", "qm_auto_update 0", "qm_steam_auto_launch 0", "qm_websocket_log 1", f"qm_websocket_url ws://127.0.0.1:{port}/ws"],
				artifacts,
				fifo_command="cl_input_fifo",
				pipe_prefix="realtime_users_",
				env={"QMCLIENT_TEST_STORAGE_ROOT": str(artifacts), "QMCLIENT_TEST_HIDE_DIALOG": "1"},
			)
			client.wait_for(lambda line: "distribution parse_ok: users=3 dummies=1" in line, "初始名单实际应用", 45)
			if not completed.wait(timeout=30):
				raise TimeoutError("压缩同步与恢复流程未完成")
			if errors:
				raise AssertionError("; ".join(errors))
			client.command("quit")
			if client.wait_for_exit(15) != 0:
				raise AssertionError("客户端退出失败")
			if errors:
				raise AssertionError("; ".join(errors))
			print(f"{name}: passed; connections={connections}; artifacts={artifacts}", flush=True)
		finally:
			if client:
				client.stop()
				(artifacts / "client.log").write_text("\n".join(client._lines), encoding="utf-8")
			server.shutdown()
			thread.join(timeout=5)


def smoke_compressed_recovery(build_dir: Path) -> None:
	run_client(build_dir, legacy=False)


def smoke_legacy_compatibility(build_dir: Path) -> None:
	run_client(build_dir, legacy=True)


def main() -> None:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("build_dir", type=Path)
	args = parser.parse_args()
	build_dir = prepare_process_build(args.build_dir)
	smoke_compressed_recovery(build_dir)
	smoke_legacy_compatibility(build_dir)


if __name__ == "__main__":
	main()
