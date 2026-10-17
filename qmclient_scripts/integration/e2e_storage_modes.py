#!/usr/bin/env python3
"""从真实客户端验证便携存储、目录搬家和失败时不切换存储位置。"""

from __future__ import annotations

import argparse
import shutil
import uuid
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path

try:
	from qmclient_scripts.integration.process_harness import Process
except ModuleNotFoundError:
	from process_harness import Process

REPO_ROOT = Path(__file__).resolve().parents[2]


def start_client(directory: Path, cwd: Path) -> Process:
	args = [str(directory / "DDNet.exe"), "gfx_fullscreen 0", "gfx_screen_width 960", "gfx_screen_height 540", "gfx_backend OpenGL", "qm_graphics_mode 0", "cl_show_welcome 0", "qm_auto_update 0", "qm_steam_auto_launch 0", "cl_save_settings 1"]
	return Process("client", args, cwd, fifo_command="cl_input_fifo", pipe_prefix=f"qmstorage_{uuid.uuid4().hex}_")


def run_and_save(directory: Path, cwd: Path, set_name: bool, scripts: tuple[tuple[str, str], ...] = ()) -> None:
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
		for filename, marker in scripts:
			client.command(f"exec {filename}")
			client.wait_for(lambda line: marker in line, f"storage search: {filename}", 10)
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
	# 首次启动缺少 storage.cfg；搬家后使用随包的注释模板，两者均使用便携默认目录。
	shutil.copy2(REPO_ROOT / "qmclient_scripts/portable/storage.cfg", directory / "storage.cfg")
	moved = workspace / "搬家后的便携版"
	if not directory.resolve().is_relative_to(workspace.resolve()) or not moved.resolve().is_relative_to(workspace.resolve()):
		raise AssertionError("move must remain inside the isolated workspace")
	shutil.move(directory, moved)
	run_and_save(moved, cwd, False)
	if (cwd / "qmclient/settings.cfg").exists():
		raise AssertionError("foreign working directory received client settings")
	return moved


@contextmanager
def storage_config(directory: Path, content: str | None) -> Iterator[None]:
	path = directory / "storage.cfg"
	previous = path.read_bytes() if path.exists() else None
	try:
		if content is None:
			path.unlink(missing_ok=True)
		else:
			path.write_text(content, encoding="utf-8")
		yield
	finally:
		if previous is None:
			path.unlink(missing_ok=True)
		else:
			path.write_bytes(previous)


def scenario_executable_config_controls_save_and_search_order(directory: Path, workspace: Path) -> None:
	root = workspace / "explicit-storage"
	save = root / "自定义存档"
	resources = root / "extra-resources"
	foreign = root / "foreign-storage"
	cwd = root / "working-directory"
	for path in (save, resources, foreign, cwd):
		path.mkdir(parents=True)
	(save / "search-order.cfg").write_text("echo qm_first_storage_path\n", encoding="utf-8")
	(resources / "search-order.cfg").write_text("echo qm_second_storage_path\n", encoding="utf-8")
	(resources / "secondary-only.cfg").write_text("echo qm_secondary_resource_loaded\n", encoding="utf-8")
	(cwd / "storage.cfg").write_text(f"add_path {foreign.as_posix()}\nadd_path $DATADIR\n", encoding="utf-8")
	content = f"add_path {save.as_posix()}\nadd_path {resources.as_posix()}\nadd_path $DATADIR\n"
	with storage_config(directory, content):
		run_and_save(directory, cwd, True, (("search-order.cfg", "qm_first_storage_path"), ("secondary-only.cfg", "qm_secondary_resource_loaded")))
		run_and_save(directory, cwd, False)
	if "QmPortableTest" not in (save / "qmclient/settings.cfg").read_text(encoding="utf-8"):
		raise AssertionError("explicit storage.cfg did not select the save directory")
	if (resources / "qmclient/settings.cfg").exists() or (foreign / "qmclient/settings.cfg").exists():
		raise AssertionError("client saved outside the first executable storage.cfg path")


def scenario_working_directory_config_is_used_without_executable_config(directory: Path, workspace: Path) -> None:
	root = workspace / "working-directory-storage"
	cwd = root / "cwd"
	save = root / "save"
	cwd.mkdir(parents=True)
	save.mkdir()
	(cwd / "storage.cfg").write_text(f"add_path {save.as_posix()}\nadd_path $DATADIR\n", encoding="utf-8")
	with storage_config(directory, None):
		run_and_save(directory, cwd, True)
	if "QmPortableTest" not in (save / "qmclient/settings.cfg").read_text(encoding="utf-8"):
		raise AssertionError("client did not load storage.cfg from the working directory")


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
	print(f"artifacts: {workspace}", flush=True)
	moved = scenario_portable_profile_moves_with_executable(directory, cwd, workspace)
	scenario_executable_config_controls_save_and_search_order(moved, workspace)
	scenario_working_directory_config_is_used_without_executable_config(moved, workspace)
	scenario_unavailable_profile_does_not_fallback(moved, cwd, workspace)
	if args.normal_build is not None:
		scenario_normal_build_rejects_test_override(args.normal_build.resolve(strict=True), cwd, workspace)
	print("portable defaults, explicit storage paths, relocation and failure isolation: passed")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
