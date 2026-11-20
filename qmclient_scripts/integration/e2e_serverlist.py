#!/usr/bin/env python3
"""使用真实隔离客户端和本机 HTTP 验证服务器列表恢复与缓存。"""

from __future__ import annotations

import argparse
from collections.abc import Callable
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import socket
import tempfile
import threading
import time

try:
	from qmclient_scripts.integration.process_build import prepare_process_build, read_cmake_cache
	from qmclient_scripts.integration.process_harness import EXE_SUFFIX, Process
except ModuleNotFoundError:
	from process_build import prepare_process_build, read_cmake_cache
	from process_harness import EXE_SUFFIX, Process


def serverlist(name: str, port: int) -> bytes:
	return json.dumps({"servers": [{"addresses": [f"tw-0.6+udp://127.0.0.1:{port}"], "location": "eu", "info": {"max_clients": 16, "max_players": 16, "passworded": False, "game_type": "DDRace", "name": name, "map": {"name": "Map"}, "version": "0.6", "clients": []}}]}).encode()


class Loopback:
	def __init__(self):
		self.condition = threading.Condition()
		self.responses: dict[str, tuple[int, bytes, dict[str, str]]] = {}
		self.requests: list[tuple[str, float]] = []
		self.gates: dict[str, tuple[threading.Event, threading.Event, threading.Event]] = {}
		owner = self

		class Handler(BaseHTTPRequestHandler):
			def setup(self):
				super().setup()
				self.connection.settimeout(5)

			def do_GET(self):
				with owner.condition:
					owner.requests.append((self.path, time.monotonic()))
					status, body, headers = owner.responses.get(self.path, (503, b"unavailable", {}))
					gate = owner.gates.get(self.path)
					owner.condition.notify_all()
				if gate:
					gate[0].set()
					ready = gate[1].wait(45)
					gate[2].set()
					if not ready:
						return
				try:
					self.send_response(status)
					self.send_header("Content-Length", str(len(body)))
					for key, value in headers.items():
						self.send_header(key, value)
					self.end_headers()
					self.wfile.write(body)
				except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
					pass

			def log_message(self, *_args):
				pass

		self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
		self.server.daemon_threads = True
		self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
		self.thread.start()
		self.url = f"http://127.0.0.1:{self.server.server_port}"

	def set(self, path: str, status: int, body: bytes, **headers: str):
		with self.condition:
			self.responses[path] = (status, body, headers)

	def wait_requests(self, path: str, count: int, timeout: float = 15):
		with self.condition:
			if not self.condition.wait_for(lambda: sum(p == path for p, _ in self.requests) >= count, timeout):
				raise TimeoutError(f"HTTP requests {path} < {count}: {self.requests}")

	def close(self):
		for _, release, _ in self.gates.values():
			release.set()
		self.server.shutdown()
		self.server.server_close()
		self.thread.join(timeout=3)
		if self.thread.is_alive():
			raise TimeoutError("loopback HTTP server did not stop")


STATUS = re.compile(r"test/serverlist: token=(\d+) count=(\d+) revision=(\d+) stale=(\d+) error=(\d+) name=(.*)")


class Scenario:
	def __init__(self, build: Path, name: str):
		root = Path(__file__).resolve().parents[2] / "tmp"
		root.mkdir(exist_ok=True)
		self.artifacts = Path(tempfile.mkdtemp(prefix=f"serverlist-e2e-{name}-", dir=root))
		self.build = build
		self.http = Loopback()
		self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
		self.udp.bind(("127.0.0.1", 0))
		self.client: Process | None = None
		self.clients: list[Process] = []
		self.token = 0
		self.default_sources = False
		(self.artifacts / "storage.cfg").write_text(f"add_path .\nadd_path {build / 'data'}\n", encoding="utf-8")

	def sources(self, *paths: str):
		(self.artifacts / "ddnet-serverlist-urls.cfg").write_text("".join(self.http.url + path + "\n" for path in paths), encoding="utf-8")

	def payload(self, name: str) -> bytes:
		return serverlist(name, self.udp.getsockname()[1])

	def defaults(self):
		self.default_sources = True
		assert not (self.artifacts / "ddnet-serverlist-urls.cfg").exists(), "default tier tests must not override the source list"
		for index in range(4):
			self.http.set(f"/default/{index}", 500, b"master unavailable")

	def assert_requests(self, required: set[int], forbidden: set[int], tiers: tuple[set[int], ...] = ()):
		with self.http.condition:
			paths = [path for path, _ in self.http.requests]
		assert {f"/default/{index}" for index in required} <= set(paths), paths
		assert not ({f"/default/{index}" for index in forbidden} & set(paths)), paths
		for before, after in zip(tiers, tiers[1:]):
			assert max(paths.index(f"/default/{index}") for index in before) < min(paths.index(f"/default/{index}") for index in after), paths

	def start(self):
		# 外部 HTTP(S) 也指向本机拒绝响应的代理，测试不依赖公网。
		proxy = self.http.url
		env = {key: proxy for key in ("http_proxy", "https_proxy", "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "all_proxy")}
		env.update({"NO_PROXY": "127.0.0.1,localhost", "no_proxy": "127.0.0.1,localhost", "QMCLIENT_TEST_STORAGE_ROOT": str(self.artifacts), "QMCLIENT_TEST_HIDE_DIALOG": "1"})
		if self.default_sources:
			env["QMCLIENT_TEST_SERVERLIST_ORIGIN"] = self.http.url
		config = ["gfx_fullscreen 0", "gfx_screen_width 640", "gfx_screen_height 480", "snd_enable 0", "cl_show_welcome 0", "cl_skip_start_menu 1", "cl_save_settings 0", "cl_menu_map \"\"", "qm_auto_update 0", "qm_steam_auto_launch 0", "http_allow_insecure 1", f"qm_websocket_url ws://127.0.0.1:{self.http.server.server_port}/ws", "minimize"]
		self.client = Process(f"serverlist-{len(self.clients)}", [str(self.build / f"DDNet{EXE_SUFFIX}"), *config], self.artifacts, fifo_command="cl_input_fifo", pipe_prefix="serverlist_e2e_", env=env)
		self.clients.append(self.client)
		self.client.wait_for(lambda line: line.startswith("client: version"), "client startup", 45)
		self.client.command("qm_serverlist_refresh")

	def status(self) -> dict:
		assert self.client is not None
		self.token += 1
		self.client.command(f"qm_serverlist_status {self.token}")
		line = self.client.wait_for(lambda line: bool((match := STATUS.fullmatch(line)) and int(match[1]) == self.token), f"consumer status token {self.token}", 5)
		match = STATUS.fullmatch(line)
		assert match
		return dict(zip(("token", "count", "revision", "stale", "error", "name"), [*(int(match[i]) for i in range(1, 6)), match[6]]))

	def until(self, predicate: Callable[[dict], bool], timeout: float = 20) -> dict:
		deadline = time.monotonic() + timeout
		while time.monotonic() < deadline:
			status = self.status()
			if predicate(status):
				return status
			# 有界状态轮询，不用长等待掩盖失败或退出。
			threading.Event().wait(0.1)
		raise TimeoutError(f"consumer did not reach expected state: {status}")

	def list(self, name: str) -> dict:
		return self.until(lambda state: state["count"] == 1 and state["name"] == name)

	def quit(self):
		assert self.client is not None
		self.client.command("quit")
		code = self.client.wait_for_exit(15)
		if code != 0:
			raise AssertionError(f"client exit code {code}")
		self.client = None

	def close(self):
		for index, client in enumerate(self.clients):
			client.stop()
			(self.artifacts / f"client-{index}.log").write_text("\n".join(client._lines), encoding="utf-8")
		self.http.close()
		self.udp.close()
		(self.artifacts / "requests.json").write_text(json.dumps(self.http.requests, indent=2), encoding="utf-8")


def scenario_default_master_success_skips_fallbacks(s: Scenario):
	s.defaults()
	s.http.set("/default/0", 200, s.payload("Master"), Age="0")
	s.start()
	state = s.list("Master")
	assert state["stale"] == 0, state
	s.quit()
	s.assert_requests({0}, {4, 5})


def scenario_default_failed_masters_publish_mirror(s: Scenario):
	s.defaults()
	s.http.set("/default/4", 200, s.payload("Mirror"), Age="0")
	s.start()
	state = s.list("Mirror")
	assert state["stale"] == 0, state
	s.quit()
	s.assert_requests({0, 1, 2, 3, 4}, {5}, ({0, 1, 2, 3}, {4}))


def scenario_default_failed_mirror_publishes_github(s: Scenario):
	s.defaults()
	s.http.set("/default/4", 503, b"mirror unavailable", **{"Retry-After": "60"})
	payload = json.loads(s.payload("GitHub"))
	payload["qm_snapshot_updated_at"] = int(time.time())
	s.http.set("/default/5", 200, json.dumps(payload).encode(), Age="0")
	s.start()
	state = s.list("GitHub")
	assert state["stale"] == 0, state
	s.quit()
	s.assert_requests({0, 1, 2, 3, 4, 5}, set(), ({0, 1, 2, 3}, {4}, {5}))


def scenario_default_github_without_timestamp_is_rejected(s: Scenario):
	s.defaults()
	s.http.set("/default/4", 503, b"mirror unavailable", **{"Retry-After": "60"})
	# 相同合法列表和 Age: 0 也不能绕过 GitHub 快照时间校验。
	s.http.set("/default/5", 200, s.payload("MissingTimestamp"), Age="0")
	s.start()
	state = s.until(lambda state: state["error"] == 1)
	assert state["count"] == 0 and state["name"] == "" and state["revision"] == 0, state
	s.quit()
	s.assert_requests({0, 1, 2, 3, 4, 5}, set(), ({0, 1, 2, 3}, {4}, {5}))


def scenario_failed_sources_publish_age_only(s: Scenario):
	s.sources("/bad", "/failed", "/valid")
	s.http.set("/bad", 200, b"{broken", Age="0")
	s.http.set("/failed", 500, b"error")
	s.http.set("/valid", 200, s.payload("Fallback"), Age="0")
	s.start()
	state = s.list("Fallback")
	assert state["stale"] == 0 and state["revision"] > 0, state
	for path in ("/bad", "/failed", "/valid"):
		s.http.wait_requests(path, 1)
	s.quit()


def scenario_automatic_retry_recovers_without_refresh(s: Scenario):
	s.sources("/recover")
	s.start()
	s.until(lambda state: state["error"] == 1 and state["count"] == 0)
	s.http.set("/recover", 200, s.payload("Recovered"), Age="0")
	s.list("Recovered")
	s.http.wait_requests("/recover", 2)
	s.quit()


def scenario_failed_refresh_retains_consumer(s: Scenario):
	s.sources("/list")
	s.http.set("/list", 200, s.payload("Retained"), Age="0")
	s.start()
	before = s.list("Retained")
	s.http.set("/list", 200, b"not-json", Age="0")
	assert s.client
	s.client.command("qm_serverlist_refresh")
	after = s.until(lambda state: state["error"] == 1)
	assert (after["count"], after["name"], after["revision"]) == (1, "Retained", before["revision"]), after
	s.quit()


def scenario_older_refresh_retains_fresher_consumer(s: Scenario):
	s.sources("/list")
	s.http.set("/list", 200, s.payload("Fresh"), Age="0")
	s.start()
	before = s.list("Fresh")
	s.http.set("/list", 200, s.payload("Old"), Age="600")
	assert s.client
	s.client.command("qm_serverlist_refresh")
	s.client.wait_for(lambda line: "retained fresher cached serverlist, response age=" in line, "older response retained", 15)
	after = s.status()
	assert (after["count"], after["name"], after["revision"]) == (1, "Fresh", before["revision"]), after
	s.quit()


def scenario_restart_loads_cache_when_network_fails(s: Scenario):
	s.sources("/list")
	s.http.set("/list", 200, s.payload("Persisted"), Age="0")
	s.start()
	s.list("Persisted")
	s.quit()
	cache = s.artifacts / "qmclient" / "cache" / "network" / "serverlist.json"
	assert cache.is_file() and b"Persisted" in cache.read_bytes(), "cache was not persisted on shutdown"
	s.http.set("/list", 503, b"unavailable")
	s.start()
	after = s.until(lambda state: state["error"] == 1 and state["count"] == 1 and state["name"] == "Persisted")
	assert after["stale"] == 1, after
	s.quit()


def scenario_valid_source_publishes_while_other_source_pending(s: Scenario):
	s.sources("/slow", "/valid")
	entered = threading.Event()
	release = threading.Event()
	finished_wait = threading.Event()
	valid_entered = threading.Event()
	s.http.gates["/slow"] = (entered, release, finished_wait)
	s.http.gates["/valid"] = (valid_entered, entered, threading.Event())
	s.http.set("/slow", 200, s.payload("Slow"), Age="0")
	s.http.set("/valid", 200, s.payload("Immediate"), Age="0")
	s.start()
	assert entered.wait(5), "slow source was not requested"
	s.list("Immediate")
	assert not finished_wait.is_set(), "publication incorrectly waited for slow response or timeout"
	s.quit()


def scenario_invalid_icon_filename_does_not_block_valid_download(s: Scenario):
	s.sources("/servers.json")
	s.http.set("/servers.json", 200, s.payload("Local"), Age="0")
	image = (s.build / "data/skins/default.png").read_bytes()
	digest = hashlib.sha256(image).hexdigest()
	valid_path = "/valid-community-icon.png"
	invalid_path = "/nested-community-icon.png"
	s.http.set(valid_path, 200, image)
	communities = [
		{
			"id": community_id,
			"name": community_id,
			"icon": {"sha256": digest, "url": s.http.url + path},
			"has_finishes": False,
			"servers": [],
		}
		for community_id, path in (("division/team", invalid_path), ("qm_icon_cache", valid_path))
	]
	(s.artifacts / "ddnet-info.json").write_text(json.dumps({"communities": communities}), encoding="utf-8")
	s.start()
	assert s.client is not None
	s.client.wait_for(lambda line: "Skipping community icon download: invalid cache filename" in line, "invalid icon filename rejected", 20)
	s.http.wait_requests(valid_path, 1)
	s.quit()
	with s.http.condition:
		assert all(path != invalid_path for path, _ in s.http.requests), s.http.requests


SCENARIOS = {
	"default_master_success_skips_fallbacks": scenario_default_master_success_skips_fallbacks,
	"default_failed_masters_publish_mirror": scenario_default_failed_masters_publish_mirror,
	"default_failed_mirror_publishes_github": scenario_default_failed_mirror_publishes_github,
	"default_github_without_timestamp_is_rejected": scenario_default_github_without_timestamp_is_rejected,
	"failed_sources_publish_age_only": scenario_failed_sources_publish_age_only,
	"automatic_retry_recovers_without_refresh": scenario_automatic_retry_recovers_without_refresh,
	"failed_refresh_retains_consumer": scenario_failed_refresh_retains_consumer,
	"older_refresh_retains_fresher_consumer": scenario_older_refresh_retains_fresher_consumer,
	"restart_loads_cache_when_network_fails": scenario_restart_loads_cache_when_network_fails,
	"valid_source_publishes_while_other_source_pending": scenario_valid_source_publishes_while_other_source_pending,
	"invalid_icon_filename_does_not_block_valid_download": scenario_invalid_icon_filename_does_not_block_valid_download,
}


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("build_dir", type=Path)
	parser.add_argument("scenario", choices=sorted(SCENARIOS), nargs="*")
	args = parser.parse_args()
	build = prepare_process_build(args.build_dir)
	cache = read_cmake_cache(build)
	if cache.get("QMCLIENT_TEST_STORAGE") != "ON" or cache.get("DEV") != "ON":
		raise RuntimeError("serverlist E2E requires DEV=ON and QMCLIENT_TEST_STORAGE=ON")
	failures = 0
	for name in (args.scenario or SCENARIOS):
		s = Scenario(build, name)
		try:
			SCENARIOS[name](s)
		except Exception as error:
			failures += 1
			print(f"{name}: FAILED: {error}; artifacts={s.artifacts}", flush=True)
		else:
			print(f"{name}: PASS; artifacts={s.artifacts}", flush=True)
		finally:
			s.close()
	return int(failures != 0)


if __name__ == "__main__":
	raise SystemExit(main())
