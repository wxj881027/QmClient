#!/usr/bin/env python3
"""Windows 真实进程验证：最终清理看门狗和本地服务器父进程死亡约束。"""

from __future__ import annotations
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import queue
import socket
import shutil
import subprocess
import tempfile
import threading
import time

from icon_resources_smoke import prepare_client
from process_harness import Process

REPO_ROOT = Path(__file__).resolve().parents[2]
SCENARIOS = ("cleanup-normal", "cleanup-stall", "owned-server-normal", "owned-server-fast-exit", "owned-server-parent-killed", "owned-server-creation-window")


def free_server_port_argument() -> str:
	with socket.socket(type=socket.SOCK_DGRAM) as reservation:
		reservation.bind(("127.0.0.1", 0))
		return f"sv_port {reservation.getsockname()[1]}"


def server_scenario(build: Path, directory: Path, name: str) -> None:
	prepare_client(build / "testrunner.exe", directory)
	shutil.copy2(build / "DDNet-Server.exe", directory / "DDNet-Server.exe")
	(directory / "storage.cfg").write_text("add_path $CURRENTDIR\nadd_path $DATADIR\n", encoding="utf-8")
	arguments = [str(directory / "testrunner.exe"), "--qm-test-owned-server", str(directory / "DDNet-Server.exe"), free_server_port_argument()]
	if name == "owned-server-creation-window":
		arguments.append("creation-gate")
	owner = subprocess.Popen(arguments, cwd=directory, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
	events: queue.Queue[str] = queue.Queue()

	def read_output() -> None:
		assert owner.stdout is not None
		for line in owner.stdout:
			events.put(line)

	threading.Thread(target=read_output, daemon=True).start()
	handle = None
	kernel = ctypes.WinDLL("kernel32", use_last_error=True)
	kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
	kernel.OpenProcess.restype = wintypes.HANDLE
	kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
	kernel.CloseHandle.argtypes = [wintypes.HANDLE]
	try:
		deadline = time.monotonic() + 10
		while time.monotonic() < deadline:
			line = events.get(timeout=max(0.01, deadline - time.monotonic()))
			if line.startswith("owned-server-pid="):
				server_pid = int(line.split("=", 1)[1])
				handle = kernel.OpenProcess(0x100001, False, server_pid)
				break
		if not handle or kernel.WaitForSingleObject(handle, 0) != 258:
			raise AssertionError("owned server was not alive before parent shutdown")
		# 证明实际服务器已完成监听和地图初始化，避免自然启动失败冒充父死亡清理。
		ready_deadline = time.monotonic() + 10
		logfile = directory / "qm-owned-server.log"
		while name != "owned-server-creation-window" and (not logfile.is_file() or "server name is" not in logfile.read_text(encoding="utf-8", errors="replace")):
			if time.monotonic() >= ready_deadline or kernel.WaitForSingleObject(handle, 0) != 258:
				raise AssertionError("owned server did not finish real initialization")
			time.sleep(0.025)
		if name in {"owned-server-parent-killed", "owned-server-creation-window"}:
			owner.kill()
		else:
			assert owner.stdin is not None
			owner.stdin.write("x" if name == "owned-server-fast-exit" else "q")
			owner.stdin.flush()
		code = owner.wait(timeout=10)
		if name not in {"owned-server-parent-killed", "owned-server-creation-window"} and code != 0:
			raise AssertionError(f"owner exited with {code}")
		if kernel.WaitForSingleObject(handle, 5000) != 0:
			raise AssertionError("server survived owner shutdown")
	finally:
		if owner.poll() is None:
			owner.kill()
		owner.wait(timeout=5)
		if handle:
			# 回归失败也只结束本场景创建的服务器，避免把故障复现变成真正残留。
			if kernel.WaitForSingleObject(handle, 0) == 258:
				kernel.TerminateProcess(ctypes.c_void_p(handle), 1)
				kernel.WaitForSingleObject(handle, 5000)
			kernel.CloseHandle(handle)


def smoke_owned_server_creation_window(build: Path, workspace: Path, proxy: str) -> None:
	server_scenario(build, workspace / "owned-server-creation-window", "owned-server-creation-window")


def client_scenario(build: Path, directory: Path, name: str, proxy: str) -> None:
	prepare_client(build / "DDNet.exe", directory)
	args = [
		str(directory / "DDNet.exe"),
		"cl_show_welcome 0",
		"cl_editor 0",
		"cl_languagefile languages/simplified_chinese.txt",
		"stdout_output_level 1",
		"gfx_fullscreen 2",
		"gfx_screen_width 960",
		"gfx_screen_height 540",
		"cl_save_settings 1",
		"player_name QmExitSaved",
		"qm_auto_update 0",
		"qm_steam_auto_launch 0",
	]
	if name == "cleanup-stall":
		args.append("--qm-test-shutdown-cleanup-stall")
	client = Process(name, args, directory, "cl_input_fifo", env={"HTTP_PROXY": proxy, "HTTPS_PROXY": proxy, "ALL_PROXY": proxy, "NO_PROXY": ""})
	try:
		client.command("echo qm_exit_ready")
		client.wait_for(lambda line: "qm_exit_ready" in line, "client initialization", 30)
		client.command("quit")
		if name == "cleanup-stall":
			client.wait_for(lambda line: "qm test final cleanup stalled" in line, "final cleanup injected stall", 10)
		if client.wait_for_exit(18) != 0:
			raise AssertionError("client did not exit successfully")
		settings = directory / "profile/qmclient/settings.cfg"
		if not settings.is_file() or 'player_name "QmExitSaved"' not in settings.read_text(encoding="utf-8-sig"):
			raise AssertionError("settings were not persisted before shutdown watchdog cleanup")
		lines = "\n".join(client._lines)
		if "shutdown watchdog armed" not in lines:
			raise AssertionError("shutdown watchdog was not armed")
		disarmed = "shutdown watchdog disarmed after cleanup completed" in lines
		if disarmed != (name == "cleanup-normal"):
			raise AssertionError("watchdog disarm did not match final cleanup completion")
	finally:
		client.stop()
		(directory / "process.log").write_text("\n".join(client._lines), encoding="utf-8")


def smoke_cleanup_normal(build: Path, workspace: Path, proxy: str) -> None:
	client_scenario(build, workspace / "cleanup-normal", "cleanup-normal", proxy)


def smoke_cleanup_stall(build: Path, workspace: Path, proxy: str) -> None:
	client_scenario(build, workspace / "cleanup-stall", "cleanup-stall", proxy)


def smoke_owned_server_normal(build: Path, workspace: Path, proxy: str) -> None:
	server_scenario(build, workspace / "owned-server-normal", "owned-server-normal")


def smoke_owned_server_fast_exit(build: Path, workspace: Path, proxy: str) -> None:
	server_scenario(build, workspace / "owned-server-fast-exit", "owned-server-fast-exit")


def smoke_owned_server_parent_killed(build: Path, workspace: Path, proxy: str) -> None:
	server_scenario(build, workspace / "owned-server-parent-killed", "owned-server-parent-killed")


RUNNERS = dict(zip(SCENARIOS, (smoke_cleanup_normal, smoke_cleanup_stall, smoke_owned_server_normal, smoke_owned_server_fast_exit, smoke_owned_server_parent_killed, smoke_owned_server_creation_window)))


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--build-dir", required=True, type=Path)
	parser.add_argument("--scenario", choices=SCENARIOS)
	args = parser.parse_args()
	if os.name != "nt":
		parser.error("Windows process lifecycle scenarios require Windows")
	build = args.build_dir.resolve()
	if not build.is_relative_to(REPO_ROOT):
		parser.error("only workspace development builds may be tested")
	if "QMCLIENT_PORTABLE:BOOL=ON" not in (build / "CMakeCache.txt").read_text(encoding="utf-8"):
		parser.error("use a dedicated portable build")
	artifacts = REPO_ROOT / "tmp/exit-lifecycle-smoke"
	artifacts.mkdir(parents=True, exist_ok=True)
	workspace = Path(tempfile.mkdtemp(prefix="run_", dir=artifacts))
	results = []
	reservation = socket.socket()
	reservation.bind(("127.0.0.1", 0))
	proxy = f"http://127.0.0.1:{reservation.getsockname()[1]}"
	try:
		for name in (args.scenario,) if args.scenario else SCENARIOS:
			RUNNERS[name](build, workspace, proxy)
			results.append({"scenario": name, "status": "passed"})
			print(f"PASS {name}", flush=True)
		return 0
	except Exception as error:
		results.append({"status": "failed", "error": str(error)})
		print(f"FAIL {error}", flush=True)
		return 1
	finally:
		reservation.close()
		(workspace / "results.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
		print(f"artifacts: {workspace}", flush=True)


if __name__ == "__main__":
	raise SystemExit(main())
