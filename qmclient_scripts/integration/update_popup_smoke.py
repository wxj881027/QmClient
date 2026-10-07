#!/usr/bin/env python3
"""真实便携客户端更新弹窗、断网失败和取消的进程冒烟。"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import socket
import tempfile

from qmclient_scripts.integration.icon_resources_smoke import prepare_client, wait_new, focus_client_window, wait_screenshot
from qmclient_scripts.integration.process_harness import Process

REPO_ROOT = Path(__file__).resolve().parents[2]


def smoke_update_popup_failure_cancel(source: Path, workspace: Path, proxy: str) -> dict[str, object]:
	directory = workspace / "isolated-client"
	prepare_client(source, directory)
	profile = directory / "profile"
	environment = {name: proxy for name in ("http_proxy", "https_proxy", "all_proxy", "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY")}
	environment.update({"NO_PROXY": "", "no_proxy": "", "DDNET_DRIVER": "OpenGL"})
	arguments = [
		str(directory / source.name),
		"gfx_fullscreen 0",
		"gfx_screen_width 960",
		"gfx_screen_height 540",
		"gfx_backend OpenGL",
		"qm_graphics_mode 0",
		"gfx_backgroundrender 1",
		"cl_show_welcome 0",
		"cl_save_settings 0",
		"qm_auto_update 0",
		"qm_steam_auto_launch 0",
		"stdout_output_level 1",
		"cl_languagefile languages/simplified_chinese.txt",
	]
	client = Process("client", arguments, directory, fifo_command="cl_input_fifo", pipe_prefix="qm_update_smoke_", env=environment)
	try:
		client.command("echo qm_update_smoke_ready")
		client.wait_for(lambda line: "qm_update_smoke_ready" in line, "main loop ready", 30)
		if any("added path '$USERDIR'" in line for line in client._lines):
			raise AssertionError("portable smoke accessed a real user directory")
		offset = len(client._lines)
		client.command("qm_update check")
		wait_new(client, offset, lambda line: "All approved metadata sources failed" in line, "bounded all-source failure", 40)
		wait_new(client, offset, lambda line: "popup_state=4" in line, "dedicated failure popup rendered", 15)
		error_offset = len(client._lines)
		client.command("qm_update status")
		wait_new(client, error_offset, lambda line: "state=failed network_error=1" in line, "transport failures classified as network error", 10)
		for scale in (100, 175):
			client.command(f"qm_ui_scale {scale}")
			client.command("echo qm_update_scale_ready")
			client.wait_for(lambda line: "qm_update_scale_ready" in line, "scale command", 10)
			if focus_client_window(client):
				client.command("screenshot")
				wait_screenshot(client, profile, 5)
		# 同步取消命令先于下一帧 Poll，不应再产生下一源请求。
		offset = len(client._lines)
		client.command("qm_update check")
		client.command("qm_update cancel")
		client.command("qm_update status")
		wait_new(client, offset, lambda line: "Update cancelled" in line, "cancel acknowledged", 10)
		wait_new(client, offset, lambda line: "state=idle" in line or "state=checked" in line, "cancelled request detached", 10)
		client.command("echo qm_update_smoke_cancel_barrier")
		wait_new(client, offset, lambda line: "qm_update_smoke_cancel_barrier" in line, "cancel frame barrier", 10)
		lines = client._lines[offset:]
		cancel = next(i for i, line in enumerate(lines) if "Update cancelled" in line)
		if any("Checking approved metadata source:" in line for line in lines[cancel + 1 :]):
			raise AssertionError("cancel triggered another source request")
		client.command("quit")
		if client.wait_for_exit(15) != 0:
			raise AssertionError("client did not exit normally")
		return {"status": "passed", "screenshots": [str(path) for path in profile.rglob("screenshot*.png")]}
	finally:
		client.stop()
		(directory / "client.log").write_text("\n".join(client._lines) + "\n", encoding="utf-8")


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--client", type=Path, required=True)
	args = parser.parse_args()
	source = args.client.resolve()
	if os.name != "nt" or not source.is_file():
		parser.error("a workspace Windows client is required")
	cache = source.parent / "CMakeCache.txt"
	if not cache.is_file() or "QMCLIENT_PORTABLE:BOOL=ON" not in cache.read_text(encoding="utf-8"):
		parser.error("client must use an isolated QMCLIENT_PORTABLE=ON build")
	artifacts = REPO_ROOT / "tmp/update-popup-smoke"
	artifacts.mkdir(parents=True, exist_ok=True)
	workspace = Path(tempfile.mkdtemp(prefix="run_", dir=artifacts))
	reservation = socket.socket()
	reservation.bind(("127.0.0.1", 0))
	try:
		result = smoke_update_popup_failure_cancel(source, workspace, f"http://127.0.0.1:{reservation.getsockname()[1]}")
		print("PASS update_popup_failure_cancel")
		return 0
	except Exception as error:
		result = {"status": "failed", "error": str(error)}
		print(f"FAIL: {error}")
		return 1
	finally:
		reservation.close()
		(workspace / "results.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
		print(f"artifacts: {workspace}")


if __name__ == "__main__":
	raise SystemExit(main())
