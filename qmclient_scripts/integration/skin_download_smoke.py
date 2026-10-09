#!/usr/bin/env python3
"""真实客户端与本机 HTTP 协作验证缓存先显示及社区 404 回退，不访问用户资料。"""

from __future__ import annotations

import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import shutil
import sys
import threading
import time

try:
	from qmclient_scripts.integration.startup_presentation_smoke import StartupEnvironment
except ModuleNotFoundError:
	from startup_presentation_smoke import StartupEnvironment


def is_upload_done(line: str, skin: str) -> bool:
	start = line.find("{")
	if start < 0:
		return False
	try:
		event = json.loads(line[start:])
	except json.JSONDecodeError:
		return False
	return event.get("system") == "perf/settings-skin-source" and event.get("event") == "upload_done" and event.get("skin") == skin


def upload_count(env: StartupEnvironment, skin: str) -> int:
	return sum(is_upload_done(line, skin) for path in (env.temp_dir / "dumps/QmClient_Perf").glob("*.log") for line in path.read_text(encoding="utf-8").splitlines())


def wait_uploads(env: StartupEnvironment, skin: str, count: int, timeout: float = 8) -> None:
	deadline = time.monotonic() + timeout
	while upload_count(env, skin) < count:
		if env.client is None or not env.client.is_alive() or time.monotonic() >= deadline:
			raise AssertionError(f"timed out waiting for {count} actual uploads of {skin}")
		time.sleep(0.02)


def smoke_cache_before_network_and_community_fallback(build_dir: Path) -> None:
	env = StartupEnvironment(build_dir)
	image = (build_dir / "data/skins/default.png").read_bytes()
	cache_dir = env.temp_dir / "downloadedskins"
	cache_dir.mkdir()
	shutil.copyfile(build_dir / "data/skins/default.png", cache_dir / "qm_skin_cache.png")
	release = threading.Event()
	requested = threading.Event()
	paths: list[str] = []

	class Handler(BaseHTTPRequestHandler):
		def log_message(self, *_args: object) -> None:
			pass

		def do_GET(self) -> None:
			paths.append(self.path)
			if self.path == "/official/qm_skin_cache.png":
				requested.set()
				if not release.wait(30):
					self.send_error(504)
					return
			elif self.path == "/official/qm_skin_fallback.png":
				self.send_error(404)
				return
			elif self.path != "/community/qm_skin_fallback.png":
				self.send_error(404)
				return
			self.send_response(200)
			self.send_header("Content-Type", "image/png")
			self.send_header("Content-Length", str(len(image)))
			self.end_headers()
			try:
				self.wfile.write(image)
			except (BrokenPipeError, ConnectionResetError):
				pass

	server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
	thread = threading.Thread(target=server.serve_forever, daemon=True)
	thread.start()
	try:
		base = f"http://127.0.0.1:{server.server_port}"
		client = env.start([
			"http_allow_insecure 1",
			"cl_download_skins 1",
			"cl_download_community_skins 1",
			f"cl_skin_download_url {base}/official/",
			f"cl_skin_community_download_url {base}/community/",
			"player_skin qm_skin_cache",
			"dummy_skin qm_skin_fallback",
			"player_skin",
			"dummy_skin",
			"qm_perf_debug 1",
			"qm_perf_logfile 1",
			"qm_perf_debug_threshold_ms 1000",
			"stdout_output_level 1",
		])
		if sys.platform == "win32":
			# 只最小化本测试启动的窗口；后台渲染保持开启，以便验证纹理上传。
			import ctypes
			from ctypes import wintypes

			client.wait_for(lambda line: "event=startup_show" in line, "test window created", 20)
			user32 = ctypes.windll.user32
			callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

			def minimize_owned_window(hwnd: int, _param: int) -> bool:
				pid = wintypes.DWORD()
				user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
				if pid.value == client._process.pid:
					user32.ShowWindow(hwnd, 6)
				return True

			user32.EnumWindows(callback_type(minimize_owned_window), 0)
		client.wait_for(lambda line: "Value: qm_skin_cache" in line, "configured player skin", 10)
		client.wait_for(lambda line: "Value: qm_skin_fallback" in line, "configured dummy skin", 10)

		wait_uploads(env, "qm_skin_cache", 1)
		if not requested.wait(5):
			raise AssertionError("cache refresh request did not start")
		if release.is_set():
			raise AssertionError("network response was released before cached upload")
		wait_uploads(env, "qm_skin_fallback", 1)
		if paths.count("/official/qm_skin_fallback.png") != 1 or paths.count("/community/qm_skin_fallback.png") != 1:
			raise AssertionError(f"unexpected fallback requests: {paths}")
		release.set()
		wait_uploads(env, "qm_skin_cache", 2, 20)
		env.quit()
		if (cache_dir / "qm_skin_fallback.png").read_bytes() != image:
			raise AssertionError("downloaded cache differs")
		print("cache_before_network_and_community_fallback: passed")
	finally:
		release.set()
		if env.client is not None:
			(env.temp_dir / "skin-client.log").write_text("\n".join(env.client._lines), encoding="utf-8")
		env.close()
		server.shutdown()
		server.server_close()
		thread.join(timeout=5)
		print(f"artifacts: {env.temp_dir}")


SMOKE_TESTS = {"cache_before_network_and_community_fallback": smoke_cache_before_network_and_community_fallback}


def smoke_refresh_cancels_loaded_cache_request(build_dir: Path) -> None:
	env = StartupEnvironment(build_dir)
	image = (build_dir / "data/skins/default.png").read_bytes()
	cache_dir = env.temp_dir / "downloadedskins"
	cache_dir.mkdir()
	(cache_dir / "qm_skin_refresh.png").write_bytes(image)
	requested, release, responded = threading.Event(), threading.Event(), threading.Event()
	community_requested = threading.Event()

	class Handler(BaseHTTPRequestHandler):
		def log_message(self, *_args: object) -> None:
			pass

		def do_GET(self) -> None:
			if self.path == "/official/qm_skin_refresh.png":
				requested.set()
				if release.wait(20):
					self.send_error(404)
				responded.set()
			else:
				community_requested.set()
				self.send_error(404)

	server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
	thread = threading.Thread(target=server.serve_forever, daemon=True)
	thread.start()
	try:
		base = f"http://127.0.0.1:{server.server_port}"
		client = env.start([
			"http_allow_insecure 1",
			"cl_download_skins 1",
			"cl_download_community_skins 1",
			f"cl_skin_download_url {base}/official/",
			f"cl_skin_community_download_url {base}/community/",
			"player_skin qm_skin_refresh",
			"qm_perf_debug 1",
			"qm_perf_logfile 1",
			"qm_perf_debug_threshold_ms 1000",
		])
		wait_uploads(env, "qm_skin_refresh", 1)
		if not requested.wait(3):
			raise AssertionError("old request did not begin")
		client.command("cl_download_community_skins 0")
		client.command("echo qm_skin_refresh_applied")
		client.wait_for(lambda line: "qm_skin_refresh_applied" in line, "source setting refresh applied", 3)
		release.set()
		if not responded.wait(3):
			raise AssertionError("held old request was not released")
		client.command("echo qm_skin_refresh_checked")
		client.wait_for(lambda line: "qm_skin_refresh_checked" in line, "post-response main loop", 3)
		env.quit()
		if community_requested.is_set():
			raise AssertionError("cancelled old session performed community fallback after disabling it")
		if (cache_dir / "qm_skin_refresh.png").read_bytes() != image:
			raise AssertionError("cancelled source request replaced existing cache")
		if upload_count(env, "qm_skin_refresh") != 1:
			raise AssertionError("cancelled old source delivered another upload")
		print("refresh_cancels_loaded_cache_request: passed")
	finally:
		release.set()
		server.shutdown()
		server.server_close()
		thread.join(5)
		env.close()
		print(f"artifacts: {env.temp_dir}")


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("build_dir", type=Path)
	args = parser.parse_args()
	try:
		smoke_cache_before_network_and_community_fallback(args.build_dir.resolve())
		smoke_refresh_cancels_loaded_cache_request(args.build_dir.resolve())
	except Exception as exc:
		print(f"skin_download: FAILED: {exc}", file=sys.stderr)
		return 1
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
