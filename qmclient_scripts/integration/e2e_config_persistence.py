#!/usr/bin/env python3
"""使用 Windows 隔离测试客户端验证配置写入和坏配置保护。"""

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
	"qmclient/config/qmclient_profiles.cfg",
	"qmclient/config/qmclient_chatbinds.cfg",
	"qmclient/config/qmclient_warlist.cfg",
)


def start_client(binary: Path, root: Path, name: str, *overrides: str) -> Process:
	root.mkdir(parents=True, exist_ok=True)
	args = [
		str(binary), "gfx_fullscreen 0", "gfx_screen_width 960", "gfx_screen_height 540",
		"gfx_backend OpenGL", "qm_graphics_mode 0", "cl_show_welcome 0",
		"qm_steam_auto_launch 0", "cl_save_settings 1",
		*overrides,
	]
	return Process(
		name, args, binary.parent, fifo_command="cl_input_fifo",
		pipe_prefix=f"qmconfig_{uuid.uuid4().hex}_",
		env={"QMCLIENT_TEST_STORAGE_ROOT": str(root)},
	)


def scenario_sequential_clients_save_last_writer(binary: Path, workspace: Path, reverse_order: bool = False) -> None:
	root = workspace / ("reverse" if reverse_order else "shared")
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
		writers = ((first, "CfgWriterA"), (stale, "CfgWriterB"))
		if reverse_order:
			writers = tuple(reversed(writers))
		for client, player in writers:
			client.command("quit")
			if client.wait_for_exit(30) != 0:
				raise AssertionError(f"{player} did not exit normally")
			if f'player_name "{player}"' not in settings.read_text(encoding="utf-8"):
				raise AssertionError(f"{player}'s settings were not saved")
			for path in MANAGED_PATHS:
				(root / path).read_text(encoding="utf-8")
			if any("refusing to overwrite" in line or "Error saving settings" in line for line in client._lines):
				raise AssertionError(f"{player} reported a sequential save failure")
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


def scenario_profile_load_preserves_default_eyes_for_invalid_emote(binary: Path, workspace: Path) -> None:
	root = workspace / "profile-invalid-emote"
	settings = root / MANAGED_PATHS[0]
	profiles = root / MANAGED_PATHS[1]
	settings.parent.mkdir(parents=True)
	settings.write_text(
		'player_name "BeforeLoad"\nplayer_default_eyes 2\n'
		"qm_profile_name 1\nqm_profile_emote 1\nqm_sponsor_nudge_at 100000000\n",
		encoding="utf-8",
	)
	profiles.write_text('add_profile -1 -1 -2 6 "" "AfterLoad" ""\n', encoding="utf-8")
	client = start_client(binary, root, "profile-invalid-emote")
	try:
		client.wait_for(lambda line: line.startswith("client: version"), "client startup", 30)
		client.command("qm_profile_queue 1")
		client.command("quit")
		if client.wait_for_exit(30) != 0:
			raise AssertionError("client did not exit normally after applying the profile")
		saved = settings.read_text(encoding="utf-8").splitlines()
		if 'player_name "AfterLoad"' not in saved:
			raise AssertionError("profile did not apply its valid name")
		if "player_default_eyes 2" not in saved:
			raise AssertionError("invalid profile emote replaced the previous default eyes")
	finally:
		client.stop()


def scenario_malformed_bind_preserves_existing_binding(binary: Path, workspace: Path) -> None:
	root = workspace / "malformed-bind"
	settings = root / MANAGED_PATHS[0]
	settings.parent.mkdir(parents=True)
	existing_bind = 'bind ctrl+shift+f6 "echo original_binding"'
	settings.write_text(
		f"{existing_bind}\nbind ctrl+shift++f6 f6\nqm_sponsor_nudge_at 100000000\n",
		encoding="utf-8",
	)
	client = start_client(binary, root, "malformed-bind")
	try:
		client.wait_for(lambda line: line.startswith("client: version"), "client startup", 30)
		client.command("quit")
		if client.wait_for_exit(30) != 0:
			raise AssertionError("client did not exit normally after loading its bindings")
		saved = settings.read_text(encoding="utf-8").splitlines()
		if "unbindall" not in saved:
			raise AssertionError("client did not save its bindings")
		if existing_bind not in saved:
			raise AssertionError("the malformed key changed the existing binding")
	finally:
		client.stop()


def scenario_multiline_bind_preserves_existing_config_on_save(binary: Path, workspace: Path) -> None:
	root = workspace / "multiline-bind"
	settings = root / MANAGED_PATHS[0]
	settings.parent.mkdir(parents=True)
	original = b'bind f6 "echo original_binding"\nqm_sponsor_nudge_at 100000000\n'
	settings.write_bytes(original)
	client = start_client(
		binary, root, "multiline-bind",
		"stdout_output_level 2", 'bind f6 "echo first line\necho second line"',
	)
	try:
		client.wait_for(lambda line: line.startswith("client: version"), "client startup", 30)
		client.command("quit")
		client.wait_for(
			lambda line: "event=shutdown_step step=window_hidden" in line,
			"client finished saving settings",
			30,
		)
		if settings.read_bytes() != original:
			raise AssertionError("saving a multiline binding replaced the existing config")
		if not any("could not serialize binding f6" in line for line in client._lines):
			raise AssertionError("client did not report the unserializable binding")
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
	cache = args.build_dir.resolve() / "CMakeCache.txt"
	cache_lines = cache.read_text(encoding="utf-8").splitlines() if cache.is_file() else []
	if "DEV:BOOL=ON" not in cache_lines or "QMCLIENT_TEST_STORAGE:BOOL=ON" not in cache_lines:
		parser.error("requires DEV=ON and QMCLIENT_TEST_STORAGE=ON; refusing to use real user storage")
	workspace = REPO_ROOT / "tmp" / f"config-persistence-{uuid.uuid4().hex}"
	workspace.mkdir(parents=True)
	scenario_sequential_clients_save_last_writer(binary, workspace)
	scenario_sequential_clients_save_last_writer(binary, workspace, reverse_order=True)
	scenario_invalid_utf8_preserves_original_config(binary, workspace)
	scenario_profile_load_preserves_default_eyes_for_invalid_emote(binary, workspace)
	scenario_malformed_bind_preserves_existing_binding(binary, workspace)
	scenario_multiline_bind_preserves_existing_config_on_save(binary, workspace)
	print(f"configuration persistence scenarios passed; artifacts: {workspace}")


if __name__ == "__main__":
	main()
