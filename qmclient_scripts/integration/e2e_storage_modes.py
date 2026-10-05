#!/usr/bin/env python3
"""从真实客户端验证便携存储、目录搬家和失败时不切换存储位置。"""

from __future__ import annotations

import argparse
import shutil
import uuid
from pathlib import Path

try:
	from qmclient_scripts.integration.process_harness import Process
except ModuleNotFoundError:
	from process_harness import Process

REPO_ROOT = Path(__file__).resolve().parents[2]


def start_client(directory: Path, cwd: Path) -> Process:
	args = [str(directory / "DDNet.exe"), "gfx_fullscreen 0", "gfx_screen_width 960", "gfx_screen_height 540", "gfx_backend OpenGL", "qm_graphics_mode 0", "cl_show_welcome 0", "qm_auto_update 0", "qm_steam_auto_launch 0", "cl_save_settings 1"]
	return Process("client", args, cwd, fifo_command="cl_input_fifo", pipe_prefix=f"qmstorage_{uuid.uuid4().hex}_")


def run_and_save(directory: Path, cwd: Path, set_name: bool) -> None:
	client = start_client(directory, cwd)
	try:
		client.wait_for(lambda line: line.startswith("client: version"), "client startup", 30)
		if set_name:
			client.command("echo qm_profile_ready")
			client.wait_for(lambda line: "qm_profile_ready" in line, "automatic configuration loaded", 10)
			if any("qm_forbidden_config" in line for line in client._lines):
				raise AssertionError("client loaded configuration outside profile")
			if any("added path '$USERDIR'" in line for line in client._lines):
				raise AssertionError("portable client registered user-directory storage")
			client.command("player_name QmPortableTest")
		else:
			client.command("player_name")
			client.wait_for(lambda line: "QmPortableTest" in line, "restored profile name", 10)
		client.command("quit")
		if client.wait_for_exit(20) != 0:
			raise AssertionError("client did not exit normally")
	finally:
		client.stop()


def scenario_portable_profile_moves_with_executable(directory: Path, cwd: Path, workspace: Path) -> Path:
	run_and_save(directory, cwd, True)
	saved = directory / "profile/qmclient/settings.cfg"
	if "QmPortableTest" not in saved.read_text(encoding="utf-8"):
		raise AssertionError("client did not save settings beside its executable")
	moved = workspace / "搬家后的便携版"
	if not directory.resolve().is_relative_to(workspace.resolve()) or not moved.resolve().is_relative_to(workspace.resolve()):
		raise AssertionError("move must remain inside the isolated workspace")
	shutil.move(directory, moved)
	run_and_save(moved, cwd, False)
	if (cwd / "qmclient/settings.cfg").exists():
		raise AssertionError("foreign working directory received client settings")
	return moved


def scenario_unavailable_profile_does_not_fallback(moved: Path, cwd: Path, workspace: Path) -> None:
	# 首路径被文件占用时初始化必须失败，不能退回用户目录或工作目录。
	alternate = workspace / "blocked-profile.exe-dir"
	alternate.mkdir()
	shutil.copy2(moved / "DDNet.exe", alternate / "DDNet.exe")
	for dll in moved.glob("*.dll"):
		shutil.copy2(dll, alternate / dll.name)
	(alternate / "storage.cfg").write_text(f"add_path $EXEDIR/profile\nadd_path {moved / 'data'}\n", encoding="utf-8")
	(alternate / "profile").write_bytes(b"occupied")
	client = start_client(alternate, cwd)
	try:
		client.wait_for(lambda line: "Failed to initialize the storage location" in line, "portable storage failure", 10)
		if any("client: version" in line for line in client._lines):
			raise AssertionError("client continued startup after portable storage failed")
		# 错误弹窗等待玩家确认；测试只结束本次隔离进程。
		client.kill()
	finally:
		client.stop()
	if (cwd / "qmclient/settings.cfg").exists():
		raise AssertionError("portable storage failure fell back to the working directory")


def scenario_normal_build_rejects_test_override(build: Path, cwd: Path, workspace: Path) -> None:
	# 用显式测试保护验证普通构建，不能让此场景接触真实玩家存档。
	directory = workspace / "normal-client"
	directory.mkdir()
	shutil.copy2(build / "DDNet.exe", directory / "DDNet.exe")
	for dll in build.glob("*.dll"):
		shutil.copy2(dll, directory / dll.name)
	(directory / "storage.cfg").write_text(f"add_path {cwd}\n", encoding="utf-8")
	client = Process("client", [str(directory / "DDNet.exe")], cwd, env={"QMCLIENT_TEST_STORAGE_ROOT": str(cwd)})
	try:
		client.wait_for(lambda line: "isolated process tests require a QMCLIENT_TEST_STORAGE build" in line, "normal client refuses a runtime storage override", 10)
		client.wait_for(lambda line: "Failed to initialize the storage location" in line, "normal storage guard", 10)
		client.kill()
	finally:
		client.stop()
	if (cwd / "qmclient/settings.cfg").exists():
		raise AssertionError("normal client accepted a runtime storage override")


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("build_dir", type=Path)
	parser.add_argument("--normal-build", type=Path)
	args = parser.parse_args()
	build = args.build_dir.resolve(strict=True)
	workspace = REPO_ROOT / "tmp" / f"portable-storage-{uuid.uuid4().hex}"
	directory = workspace / "便携目录"
	cwd = workspace / "other-working-directory"
	directory.mkdir(parents=True)
	cwd.mkdir()
	shutil.copy2(build / "DDNet.exe", directory / "DDNet.exe")
	for dll in build.glob("*.dll"):
		shutil.copy2(dll, directory / dll.name)
	shutil.copytree(build / "data", directory / "data")
	(directory / "data/autoexec_client.cfg").write_text("echo qm_forbidden_config_data\n", encoding="utf-8")
	(cwd / "autoexec_client.cfg").write_text("echo qm_forbidden_config_cwd\n", encoding="utf-8")
	(directory / "autoexec_client.cfg").write_text("echo qm_forbidden_config_exe\n", encoding="utf-8")
	(directory / "storage.cfg").write_text(f"add_path {cwd}\n", encoding="utf-8")
	(cwd / "storage.cfg").write_text(f"add_path {cwd}\nadd_path {directory / 'data'}\n", encoding="utf-8")
	print(f"artifacts: {workspace}", flush=True)
	moved = scenario_portable_profile_moves_with_executable(directory, cwd, workspace)
	scenario_unavailable_profile_does_not_fallback(moved, cwd, workspace)
	if args.normal_build is not None:
		scenario_normal_build_rejects_test_override(args.normal_build.resolve(strict=True), cwd, workspace)
	print("portable profile relocation and failure isolation: passed")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
