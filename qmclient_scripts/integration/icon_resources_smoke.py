#!/usr/bin/env python3
"""真实便携客户端的图标资源冒烟：隔离 profile、用户覆盖与 MTSDF 失败回退。"""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
from collections.abc import Callable
import json
import os
from pathlib import Path
import re
import shutil
import socket
import tempfile
import time
import uuid

try:
	from qmclient_scripts.integration.process_harness import Process, log_message
except ModuleNotFoundError:
	from process_harness import Process, log_message

REPO_ROOT = Path(__file__).resolve().parents[2]
STYLES = ("Regular", "Bold", "Light", "Fill")
WEIGHTS = ("regular", "bold", "light", "fill", "light", "bold")


def wait_new(client: Process, offset: int, predicate: Callable[[str], bool], description: str, timeout: float = 30.0) -> str:
	# harness 会扫描历史日志；只接受本次命令之后出现的观察点，避免重复 weight 命中旧 ready。
	return client.wait_for(lambda line: predicate(line) and any(log_message(raw) == line for raw in client._lines[offset:]), description, timeout)


def perf_snapshot(profile: Path) -> dict[Path, int]:
	return {path: path.stat().st_size for path in profile.glob("dumps/QmClient_Perf/*.log")}


def wait_perf_summary(client: Process, profile: Path, offsets: dict[Path, int], predicate: Callable[[str], bool], description: str, timeout: float = 30.0) -> str:
	# perf/ 被生产 logger 路由到专用文件，读取命令之后的记录，不依赖 stdout。
	deadline = time.monotonic() + timeout
	while time.monotonic() < deadline:
		for path in profile.glob("dumps/QmClient_Perf/*.log"):
			with path.open("rb") as source:
				source.seek(offsets.get(path, 0))
				for line in source.read().decode("utf-8", errors="replace").splitlines():
					if predicate(line):
						return line
		if not client.is_alive():
			raise RuntimeError(f"client exited while waiting for {description}")
		time.sleep(0.025)
	raise TimeoutError(f"timed out waiting for {description} in isolated perf log")


def focus_client_window(client: Process) -> bool:
	# 仅操作本脚本创建的工作区实例；正式客户端窗口不会进入候选集合。
	user32 = ctypes.WinDLL("user32", use_last_error=True)
	callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
	user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
	user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
	user32.IsWindowVisible.argtypes = [wintypes.HWND]
	user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
	user32.SetForegroundWindow.argtypes = [wintypes.HWND]
	user32.GetForegroundWindow.restype = wintypes.HWND
	windows = []

	@callback_type
	def visit(window: int, _: int) -> bool:
		owner = wintypes.DWORD()
		user32.GetWindowThreadProcessId(window, ctypes.byref(owner))
		if owner.value == client._process.pid:
			windows.append(window)
		return True

	user32.EnumWindows(visit, 0)
	for window in windows:
		user32.ShowWindow(window, 9)
		user32.SetForegroundWindow(window)
		if user32.GetForegroundWindow() == window:
			return True
	return False


def wait_screenshot(client: Process, profile: Path, timeout: float = 20.0) -> None:
	deadline = time.monotonic() + timeout
	while time.monotonic() < deadline:
		if any(profile.rglob("screenshot*.png")):
			return
		if not client.is_alive():
			raise RuntimeError("client exited before screenshot was saved")
		if focus_client_window(client):
			client.command("screenshot")
		time.sleep(0.1)
	raise TimeoutError("screenshot artifact was not saved")


def icon_summary(line: str) -> dict[str, object] | None:
	# 性能日志通过统一 logger 输出 JSON；其前方可以带时间、级别和 system 前缀。
	start = line.find("{")
	if start < 0:
		return None
	try:
		payload = json.loads(line[start:])
	except (json.JSONDecodeError, ValueError):
		return None
	if not isinstance(payload, dict) or payload.get("event") != "icon_summary":
		return None
	return payload


def icon_draw_count(line: str, key: str) -> int | None:
	payload = icon_summary(line)
	value = payload.get(key) if payload is not None else None
	# bool 也是 Python int，但不代表真实绘制计数；字符串、负数同样不能证明行为。
	return value if type(value) is int and value >= 0 else None


def msdf_draws(line: str) -> int | None:
	return icon_draw_count(line, "msdf_draws")


def font_fallback_draws(line: str) -> int | None:
	return icon_draw_count(line, "font_fallback_draws")


def prepare_client(source: Path, directory: Path, atlas: bool) -> None:
	directory.mkdir()
	shutil.copy2(source, directory / source.name)
	for dll in source.parent.glob("*.dll"):
		shutil.copy2(dll, directory / dll.name)

	def ignore(path: str, names: list[str]) -> list[str]:
		return ["icons"] if not atlas and Path(path) == REPO_ROOT / "data/qmclient" and "icons" in names else []

	# 必须从干净源码复制资源，不能让 build/data 残留的已删除字体掩盖回归。
	shutil.copytree(REPO_ROOT / "data", directory / "data", ignore=ignore)
	# Windows shader 由构建生成；只叠加这类生成依赖，不叠加字体或图集。
	shaders = source.parent / "data/shader"
	if shaders.is_dir():
		shutil.copytree(shaders, directory / "data/shader", dirs_exist_ok=True)
	(directory / "profile").mkdir()


def poison_user_resources(profile: Path) -> None:
	for relative in ("fonts/index.json", *(f"fonts/Phosphor/Phosphor-{style}.ttf" for style in STYLES), *(f"qmclient/icons/qm_icons_{style.lower()}_msdf.{suffix}" for style in STYLES for suffix in ("json", "png"))):
		file = profile / relative
		file.parent.mkdir(parents=True, exist_ok=True)
		file.write_bytes(b"corrupt user override must never shadow bundled resources")
	# 旧版本曾复制到此目录：保留残留，不能因为它存在而恢复图标。
	old = profile / "qmclient/fonts/Phosphor/Phosphor-Regular.ttf"
	old.parent.mkdir(parents=True, exist_ok=True)
	old.write_bytes(b"legacy missing font")


def assert_bundled_fonts(client: Process, directory: Path) -> None:
	for style in STYLES:
		line = client.wait_for(lambda value, style=style: "Bundled Phosphor font:" in value and f"style={style} " in value, f"bundled {style} face diagnostic", 30)
		match = re.search(r"source='([^']+)' missing_glyphs=(\d+)", line)
		if match is None or int(match.group(2)) != 0:
			raise AssertionError(f"bundled {style} lacks registered glyphs: {line}")
		actual = Path(match.group(1)).resolve()
		expected = (directory / f"data/fonts/Phosphor/Phosphor-{style}.ttf").resolve()
		if actual != expected:
			raise AssertionError(f"{style} loaded outside isolated bundled data: {actual}")


def scenario(source: Path, workspace: Path, name: str, atlas: bool, hostile: bool, proxy: str, backend: str = "OpenGL", require_screenshot: bool = False) -> dict[str, object]:
	directory = workspace / name
	prepare_client(source, directory, atlas)
	profile = directory / "profile"
	if hostile:
		poison_user_resources(profile)
	cwd = directory / "foreign-cwd"
	cwd.mkdir()
	# 全部 HTTP 自动请求经不可用的本机代理立即失败；不依赖互联网、服务端或真实玩家目录。
	environment = {key: proxy for key in ("http_proxy", "https_proxy", "all_proxy", "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY")}
	environment.update({"no_proxy": "", "NO_PROXY": "", "QMCLIENT_TEST_STORAGE_ROOT": str(profile), "DDNET_DRIVER": backend})
	arguments = [
		str(directory / source.name),
		"gfx_fullscreen 2",
		"gfx_screen_width 960",
		"gfx_screen_height 540",
		f"gfx_backend {backend}",
		"qm_graphics_mode -1",
		"gfx_backgroundrender 1",
		"cl_show_welcome 0",
		"cl_skip_start_menu 1",
		"ui_page 16",
		"ui_settings_page 11",
		"stdout_output_level 1",
		"cl_save_settings 0",
		"qm_auto_update 0",
		"qm_steam_auto_launch 0",
		"qm_perf_debug 1",
		"qm_perf_logfile 1",
		"cl_languagefile languages/simplified_chinese.txt",
		"tc_custom_font DejaVu Sans",
		"qm_ui_icon_weight 0",
	]
	client = Process("client", arguments, cwd, fifo_command="cl_input_fifo", pipe_prefix=f"qm_icons_{uuid.uuid4().hex}_", env=environment)
	try:
		client.wait_for(lambda line: line.startswith("client: version"), "client startup", 30)
		assert_bundled_fonts(client, directory)
		if backend == "Vulkan" and any("Created OpenGL" in raw for raw in client._lines):
			raise AssertionError("Vulkan scenario unexpectedly used OpenGL")
		client.command("echo qm_icon_main_loop_ready")
		client.wait_for(lambda line: "qm_icon_main_loop_ready" in line, "main menu loop", 30)
		if any("added path '$USERDIR'" in line for line in client._lines):
			raise AssertionError("client registered a real user-directory path; --client must be a portable build")
		observations: list[dict[str, object]] = []
		for weight, style in enumerate(WEIGHTS):
			offset = len(client._lines)
			perf_offsets = perf_snapshot(profile)
			client.command(f"qm_ui_icon_weight {weight}")
			client.command(f"echo qm_icon_weight_{weight}")
			wait_new(client, offset, lambda line, weight=weight: f"qm_icon_weight_{weight}" in line, "weight command processed")
			if atlas:
				if weight == 0:
					client.wait_for(lambda line: f"MTSDF icon atlas ready: weight={style} " in line, f"{style} atlas startup", 30)
				else:
					wait_new(client, offset, lambda line, style=style: f"MTSDF icon atlas ready: weight={style} " in line, f"{style} atlas reload")
				line = wait_perf_summary(client, profile, perf_offsets, lambda line: (msdf_draws(line) or 0) > 0, "actual MTSDF drawing")
			else:
				line = wait_perf_summary(client, profile, perf_offsets, lambda line: msdf_draws(line) == 0 and (font_fallback_draws(line) or 0) > 0, "actual font fallback drawing without atlas")
				if any("MTSDF icon atlas ready:" in raw for raw in client._lines):
					raise AssertionError("missing bundled atlas unexpectedly loaded from a foreign directory")
			observations.append({"weight": weight, "atlas_style": style, "diagnostic": line})
		client.command("qm_ui_scale 125")
		client.command("screenshot")
		client.command("echo qm_icon_capture_requested")
		client.wait_for(lambda line: "qm_icon_capture_requested" in line, "screenshot request processed", 10)
		capture_status = "captured"
		try:
			wait_screenshot(client, profile, timeout=5.0)
		except TimeoutError:
			if require_screenshot:
				raise
			capture_status = "unavailable: SDL window did not acquire input focus in this desktop session"
		client.command("quit")
		if client.wait_for_exit(20) != 0:
			raise AssertionError("icon smoke client did not exit normally")
		if (cwd / "qmclient/settings.cfg").exists():
			raise AssertionError("client wrote settings outside isolated profile")
		return {"scenario": name, "status": "passed", "atlas": atlas, "backend": backend, "weights": observations, "capture_status": capture_status, "screenshots": [str(file) for file in profile.rglob("screenshot*.png")]}
	finally:
		client.stop()
		(directory / "client.log").write_text("\n".join(client._lines) + "\n", encoding="utf-8")


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--client", type=Path, required=True, help="dedicated QMCLIENT_PORTABLE executable")
	parser.add_argument("--require-screenshot", action="store_true", help="fail when this desktop session cannot focus the test window for capture")
	args = parser.parse_args()
	source = args.client.resolve()
	if os.name != "nt":
		parser.error("this portable Windows resource scenario requires Windows")
	if not source.is_file():
		parser.error(f"missing client: {source}")
	cache = source.parent / "CMakeCache.txt"
	if not cache.is_file() or "QMCLIENT_PORTABLE:BOOL=ON" not in cache.read_text(encoding="utf-8"):
		parser.error("--client must come from a QMCLIENT_PORTABLE=ON build")
	artifacts = REPO_ROOT / "tmp/icon-resources-smoke"
	artifacts.mkdir(parents=True, exist_ok=True)
	workspace = Path(tempfile.mkdtemp(prefix="run_", dir=artifacts))
	results: list[dict[str, object]] = []
	# 动态保留一个不监听的本机端口，不依赖固定端口或另一个进程。
	proxy_reservation = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	proxy_reservation.bind(("127.0.0.1", 0))
	proxy = f"http://127.0.0.1:{proxy_reservation.getsockname()[1]}"
	try:
		for name, atlas, hostile, backend in (("clean-profile", True, False, "OpenGL"), ("hostile-user-resources", True, True, "OpenGL"), ("missing-atlas-fallback", False, True, "OpenGL"), ("vulkan-bundled-resources", True, True, "Vulkan")):
			results.append(scenario(source, workspace, name, atlas, hostile, proxy, backend, args.require_screenshot))
			print(f"PASS {name}", flush=True)
		return 0
	except Exception as error:
		results.append({"status": "failed", "error": str(error)})
		print(f"FAIL: {error}", flush=True)
		return 1
	finally:
		proxy_reservation.close()
		(workspace / "results.json").write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
		print(f"artifacts: {workspace}", flush=True)


if __name__ == "__main__":
	raise SystemExit(main())
