#!/usr/bin/env python3
"""使用隔离资料目录验证客户端的皮肤下载和预设保存。"""

from __future__ import annotations

import argparse
import hashlib
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
			"qm_perf 1",
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
				try:
					if release.wait(20):
						self.send_error(404)
				except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
					pass
				finally:
					responded.set()
			elif self.path == "/community/qm_skin_refresh.png":
				community_requested.set()
				self.send_error(404)
			else:
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
			"qm_perf 1",
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


def smoke_favorites_are_metadata_until_visible(build_dir: Path) -> None:
	env = StartupEnvironment(build_dir)
	requested = threading.Event()
	paths: list[str] = []
	image = (build_dir / "data/skins/default.png").read_bytes()

	class Handler(BaseHTTPRequestHandler):
		def log_message(self, *_args: object) -> None:
			pass

		def do_GET(self) -> None:
			if self.path == "/":
				self.send_error(404)
				return
			paths.append(self.path)
			requested.set()
			self.send_response(200)
			self.send_header("Content-Length", str(len(image)))
			self.end_headers()
			self.wfile.write(image)

	server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
	thread = threading.Thread(target=server.serve_forever, daemon=True)
	thread.start()
	try:
		base = f"http://127.0.0.1:{server.server_port}/"
		client = env.start([
			"http_allow_insecure 1",
			"cl_download_skins 1",
			"cl_download_community_skins 0",
			f"cl_skin_download_url {base}",
			"player_skin default",
			"dummy_skin default",
			"cl_skip_start_menu 1",
			"ui_page 21",
			*[f"add_favorite_skin qm_unseen_favorite_{index}" for index in range(40)],
		])
		env.finish_startup()
		# 异步列表重建完成后仍不可触发网络。这里观察有界时间内的实际 HTTP 请求，
		# 随后显式选择其中一项，验证按需下载没有被关闭。
		if requested.wait(3):
			raise AssertionError(f"unseen favorites requested at startup: {paths}")
		client.command("player_skin qm_unseen_favorite_39")
		if not requested.wait(8):
			raise AssertionError("explicitly selected favorite was not requested")
		env.quit()
		if any("Invalid m_MenuPage" in line or " E assert:" in line for line in client._lines):
			raise AssertionError("statistics page configuration failed to open the actual page")
		if paths != ["/qm_unseen_favorite_39.png"]:
			raise AssertionError(f"selection requested unrelated favorites: {paths}")
		print("favorites_are_metadata_until_visible: passed")
	finally:
		server.shutdown()
		server.server_close()
		thread.join(5)
		env.close()
		print(f"artifacts: {env.temp_dir}")

def smoke_invalid_catalog_preserves_cache(build_dir: Path) -> None:
	env = StartupEnvironment(build_dir)
	cache_dir = env.temp_dir / "downloadedskins"
	cache_dir.mkdir()
	valid_index = b'<a href="qm_cached_skin.png">qm_cached_skin.png</a>'
	http_error = threading.Event()
	invalid_body = threading.Event()

	class Handler(BaseHTTPRequestHandler):
		def log_message(self, *_args: object) -> None:
			pass

		def do_GET(self) -> None:
			if self.path == "/invalid/":
				self.send_response(200)
				self.end_headers()
				self.wfile.write(b"<html>not a skin index</html>")
				invalid_body.set()
			else:
				self.send_error(503)
				http_error.set()

	server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
	thread = threading.Thread(target=server.serve_forever, daemon=True)
	thread.start()
	try:
		base = f"http://127.0.0.1:{server.server_port}"
		paths = [cache_dir / f"{kind}_{hashlib.sha256(f'{base}/{suffix}'.encode()).hexdigest()}.html" for kind, suffix in (("custom_official_skins", "unavailable"), ("custom_community_skins", "invalid"))]
		for path in paths:
			path.write_bytes(valid_index)
		client = env.start([
			"http_allow_insecure 1",
			f"cl_skin_download_url {base}/unavailable/",
			f"cl_skin_community_download_url {base}/invalid/",
			"stdout_output_level 1",
		])
		assert http_error.wait(20) and invalid_body.wait(20), "catalog requests did not complete"
		for suffix in ("unavailable", "invalid"):
			client.wait_for(lambda line: f"Failed to load skin catalog {base}/{suffix}/" in line, f"rejected {suffix} catalog", 15)
		env.quit()
		for path in paths:
			assert path.read_bytes() == valid_index, f"invalid response replaced {path.name}"
		print("invalid_catalog_preserves_cache: passed")
	finally:
		env.close()
		server.shutdown()
		server.server_close()
		thread.join(timeout=5)
		print(f"artifacts: {env.temp_dir}")


def smoke_presets_round_trip_with_escaped_name_and_full_queue(build_dir: Path) -> None:
	env = StartupEnvironment(build_dir)
	config_path = env.temp_dir / "qmclient/settings.cfg"
	preset_lines = [
		r'add_skin_queue_preset "Track \"A\" \\ warmup"',
		'add_skin_queue_preset_item 2 "default"',
		'add_skin_queue_preset_item_ex 2 "bluekitty" 1 12345 23456',
	]
	try:
		config_path.write_text("\n".join(["qm_skin_queue_length 1", "qm_dummy_skin_queue_length 3", *preset_lines]) + "\n", encoding="utf-8")
		env.start(["cl_save_settings 1", "cl_download_skins 0", "cl_download_community_skins 0"])
		env.finish_startup()
		env.quit()
		saved_lines = config_path.read_text(encoding="utf-8").splitlines()
		for line in preset_lines:
			if saved_lines.count(line) != 1:
				raise AssertionError(f"missing or duplicated preset command after reload and save: {line}")
		print("presets_round_trip_with_escaped_name_and_full_queue: passed")
	finally:
		env.close()
		print(f"artifacts: {env.temp_dir}")


SMOKE_TESTS = {
	"cache_before_network_and_community_fallback": smoke_cache_before_network_and_community_fallback,
	"refresh_cancels_loaded_cache_request": smoke_refresh_cancels_loaded_cache_request,
	"favorites_are_metadata_until_visible": smoke_favorites_are_metadata_until_visible,
	"invalid_catalog_preserves_cache": smoke_invalid_catalog_preserves_cache,
	"presets_round_trip_with_escaped_name_and_full_queue": smoke_presets_round_trip_with_escaped_name_and_full_queue,
}


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("build_dir", type=Path)
	parser.add_argument("test", choices=SMOKE_TESTS, nargs="?")
	args = parser.parse_args()
	names = [args.test] if args.test else SMOKE_TESTS
	build_dir = args.build_dir.resolve()
	for name in names:
		try:
			SMOKE_TESTS[name](build_dir)
		except Exception as exc:
			print(f"{name}: FAILED: {exc}", file=sys.stderr)
			return 1
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
