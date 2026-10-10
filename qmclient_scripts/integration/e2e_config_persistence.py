#!/usr/bin/env python3
"""使用 Windows 隔离测试客户端验证多开冲突和坏配置保护。"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import uuid

try:
	from qmclient_scripts.integration.process_harness import Process
except ModuleNotFoundError:
	from process_harness import Process


REPO_ROOT = Path(__file__).resolve().parents[2]
MANAGED_PATHS = (
	"qmclient/settings.cfg",
	"qmclient/qmclient_profiles.cfg",
	"qmclient/qmclient_chatbinds.cfg",
	"qmclient/qmclient_warlist.cfg",
)


def start_client(binary: Path, root: Path, name: str) -> Process:
	root.mkdir(parents=True, exist_ok=True)
	args = [
		str(binary), "gfx_fullscreen 0", "gfx_screen_width 960", "gfx_screen_height 540",
		"gfx_backend OpenGL", "qm_graphics_mode 0", "cl_show_welcome 0",
		"qm_auto_update 0", "qm_steam_auto_launch 0", "cl_save_settings 1",
	]
	return Process(
		name, args, binary.parent, fifo_command="cl_input_fifo",
		pipe_prefix=f"qmconfig_{uuid.uuid4().hex}_",
		env={"QMCLIENT_TEST_STORAGE_ROOT": str(root)},
	)


def scenario_stale_client_preserves_newer_settings(binary: Path, workspace: Path) -> None:
	root = workspace / "shared"
	settings = root / MANAGED_PATHS[0]
	settings.parent.mkdir(parents=True)
	settings.write_text('player_name "CfgSeed"\nqm_sponsor_nudge_at 100000000\n', encoding="utf-8")
	first = start_client(binary, root, "first")
	stale = None
	try:
		first.wait_for(lambda line: line.startswith("client: version"), "first client startup", 30)
		stale = start_client(binary, root, "stale")
		stale.wait_for(lambda line: line.startswith("client: version"), "second client startup", 30)
		for client, player in ((first, "CfgWriterA"), (stale, "CfgWriterB")):
			client.command(f"player_name {player}")
			client.command(f"echo cfg_ready_{player}")
			client.wait_for(lambda line: f"cfg_ready_{player}" in line, "name update", 10)
		first.command("quit")
		if first.wait_for_exit(30) != 0:
			raise AssertionError("first client did not exit normally")
		saved = {path: (root / path).read_bytes() for path in MANAGED_PATHS}
		if b"CfgWriterA" not in saved[MANAGED_PATHS[0]]:
			raise AssertionError("first client's settings were not saved")
		stale.command("quit")
		stale.wait_for(lambda line: "refusing to overwrite" in line, "stale save rejection", 20)
		if not stale.is_alive():
			raise AssertionError("stale client exited unexpectedly instead of reporting its save failure")
		for path, content in saved.items():
			if (root / path).read_bytes() != content:
				raise AssertionError(f"stale client modified {path}")
		# 保存错误弹窗等待玩家确认；验证失败日志与文件后只结束本场景的实例。
		stale.kill()
	finally:
		first.stop()
		if stale is not None:
			stale.stop()


def scenario_invalid_utf8_preserves_original_config(binary: Path, workspace: Path) -> None:
	root = workspace / "invalid"
	settings = root / MANAGED_PATHS[0]
	settings.parent.mkdir(parents=True)
	original = b'player_name "KeepMe"\nplayer_name "bad\xff"\n'
	settings.write_bytes(original)
	client = start_client(binary, root, "invalid")
	try:
		client.wait_for(lambda line: "Failed to load config" in line, "invalid config rejection", 20)
		if not client.is_alive():
			raise AssertionError("client exited unexpectedly before showing the config error")
		if any("client: version" in line for line in client._lines):
			raise AssertionError("client continued startup with partially loaded settings")
		if settings.read_bytes() != original:
			raise AssertionError("invalid config was overwritten")
		client.kill()
	finally:
		client.stop()


def main() -> None:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("build_dir", type=Path, help="DEV=ON、QMCLIENT_TEST_STORAGE=ON 的 Windows 构建")
	args = parser.parse_args()
	if os.name != "nt":
		parser.error("this runner requires the Windows isolated test client")
	binary = args.build_dir.resolve() / "DDNet.exe"
	if not binary.is_relative_to(REPO_ROOT):
		parser.error("client executable must be built inside this workspace")
	if not binary.is_file():
		parser.error(f"client executable does not exist: {binary}")
	workspace = REPO_ROOT / "tmp" / f"config-persistence-{uuid.uuid4().hex}"
	workspace.mkdir(parents=True)
	scenario_stale_client_preserves_newer_settings(binary, workspace)
	scenario_invalid_utf8_preserves_original_config(binary, workspace)
	print(f"configuration persistence scenarios passed; artifacts: {workspace}")


if __name__ == "__main__":
	main()
