#!/usr/bin/env python3
"""在隔离客户端里测量真实皮肤目录、首屏和筛选定位。"""

from __future__ import annotations

import argparse
from collections import Counter
from hashlib import file_digest, sha256
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
import os
from pathlib import Path
import re
import shutil
import tempfile
import threading
import time
from urllib.parse import quote, unquote, urlsplit

try:
	from qmclient_scripts.integration.process_build import REPO_ROOT, read_cmake_cache
	from qmclient_scripts.integration.process_harness import Process
	from qmclient_scripts.integration.perf_glyph_raster import stage_client
except ModuleNotFoundError:
	from process_build import REPO_ROOT, read_cmake_cache
	from process_harness import Process
	from perf_glyph_raster import stage_client


def perf_event(line: str) -> dict:
	start = line.find("{")
	if start != -1:
		try:
			value = json.loads(line[start:])
			if isinstance(value, dict):
				return value
		except json.JSONDecodeError:
			pass
	return {}


class PerfCursor:
	def __init__(self, client: Process, log: Path):
		self.client = client
		self.log = log
		self.offset = 0
		self.events = []

	def wait(self, name: str, matches, timeout: float = 45) -> dict:
		deadline = time.monotonic() + timeout
		while self.client.is_alive() and time.monotonic() < deadline:
			if self.log.is_file():
				with self.log.open("rb") as source:
					source.seek(self.offset)
					while (line := source.readline()).endswith(b"\n"):
						self.offset = source.tell()
						event = perf_event(line.decode("utf-8", errors="replace"))
						if event:
							self.events.append(event)
							if matches(event):
								return event
			time.sleep(0.01)
		raise RuntimeError(f"timed out or client terminated waiting for {name}")


def percentile(values: list[float], fraction: float) -> float:
	sorted_values = sorted(values)
	return sorted_values[math.ceil(len(sorted_values) * fraction) - 1]


def frame_stats(events: list[dict], first: int, last: int) -> dict:
	values = [duration for event in events if event.get("event") == "frame_batch" for frame, duration in zip(event["frames"], event["durations_ms"], strict=True) if first <= frame <= last and duration > 0]
	return {"frames": len(values), "p95_ms": percentile(values, 0.95), "p99_ms": percentile(values, 0.99), "max_ms": max(values), "over_16_7_ms": sum(value > 16.7 for value in values)} if values else {"frames": 0}


def scene(build: Path, pngs: dict[str, Path], mode: str, args: argparse.Namespace) -> Path:
	artifact_root = REPO_ROOT / "tmp" / "skin-list-perf"
	artifact_root.mkdir(parents=True, exist_ok=True)
	artifact = Path(tempfile.mkdtemp(prefix=f"{mode}-", dir=artifact_root))
	print(f"artifacts: {artifact}", flush=True)
	binary = args.staged_client.resolve() if args.staged_client else stage_client(build, artifact / "client-bundle")
	with binary.open("rb") as source:
		binary_hash = file_digest(source, "sha256").hexdigest()
	(artifact / "qmclient" / "fonts").mkdir(parents=True)
	if mode == "offline":
		cache = artifact / "downloadedskins"
		cache.mkdir()
		for name, path in pngs.items():
			shutil.copyfile(path, cache / name)

	directory_body = "".join(f'<a href="{quote(name)}">skin</a>\n' for name in pngs).encode() if mode == "online" else b""
	requests = Counter()
	served = Counter()
	request_lock = threading.Lock()
	png_release = threading.Event()

	class Handler(BaseHTTPRequestHandler):
		def log_message(self, *_args: object) -> None:
			pass

		def do_GET(self) -> None:
			request = unquote(urlsplit(self.path).path).removeprefix("/")
			with request_lock:
				requests[request] += 1
			if not request:
				body, content_type = directory_body or b"<html></html>", "text/html"
			elif mode == "online" and request in pngs:
				if not png_release.wait(30):
					self.send_error(503)
					return
				body, content_type = pngs[request].read_bytes(), "image/png"
			else:
				self.send_error(404)
				return
			try:
				self.send_response(200)
				self.send_header("Content-Type", content_type)
				self.send_header("Content-Length", str(len(body)))
				self.end_headers()
				self.wfile.write(body)
			except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
				return
			with request_lock:
				served[request] += 1

	client = None
	with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
		worker = threading.Thread(target=server.serve_forever, daemon=True)
		worker.start()
		try:
			base_url = f"http://127.0.0.1:{server.server_port}/"
			client = Process(
				"client",
				[
					str(binary),
					"gfx_fullscreen 0",
					"gfx_screen_width 1280",
					"gfx_screen_height 720",
					f"gfx_backend {args.backend}",
					f"gfx_refresh_rate {args.fps}",
					"gfx_vsync 0",
					"gfx_fsaa_samples 0",
					"qm_graphics_mode -1",
					"gfx_backgroundrender 1",
					"qm_ui_scale 100",
					"cl_refresh_rate_inactive 0",
					"cl_save_settings 0",
					"cl_show_welcome 0",
					"qm_auto_update 0",
					"qm_steam_auto_launch 0",
					"cl_skip_start_menu 1",
					"ui_page 16",
					"ui_settings_page 1",
					"qm_settings_prewarm 0",
					"qm_perf_debug 1",
					"qm_perf_logfile 1",
					"qm_perf_debug_threshold_ms 1000",
					"qm_graphics_trace 3",
					"stdout_output_level 1",
					"http_allow_insecure 1",
					f"cl_download_skins {int(mode == 'online')}",
					"cl_download_community_skins 0",
					f"cl_skin_download_url {base_url}",
					f"cl_skin_community_download_url {base_url}",
					f"qm_ddstats_skin_download_url {base_url}",
					"qm_skin_list_filter 0",
				],
				artifact,
				fifo_command="cl_input_fifo",
				pipe_prefix="qm_skin_perf_",
				env={"NO_PROXY": f"{os.environ.get('NO_PROXY', '')},127.0.0.1"},
			)
			client.command("echo qm_skin_perf_ready")
			client.wait_for(lambda line: "qm_skin_perf_ready" in line, "startup", 25)
			client.wait_for(lambda line: "storage: added path '$CURRENTDIR'" in line, "isolated storage configuration", 2)
			renderer = client.wait_for(lambda line: line.startswith("gfx: GPU renderer: "), "real GPU renderer", 5)
			version = client.wait_for(lambda line: line.startswith("gfx: GPU version: "), "graphics version", 5)
			if args.backend == "Vulkan" and not version.startswith("gfx: GPU version: Vulkan "):
				raise RuntimeError(f"expected Vulkan, received {version}")
			if mode == "online":
				client.wait_for(lambda line: f"Loaded skin catalog {base_url}: {len(pngs)} names" in line, "corpus catalog", 30)
			(log,) = (artifact / "dumps/QmClient_Perf").glob("*.log")
			cursor = PerfCursor(client, log)
			entry_command_time = time.monotonic()
			client.command("ui_settings_page 3")
			png_release.set()
			entry = cursor.wait("Tee page entry", lambda event: event.get("event") == "tee_enter")
			first = cursor.wait("first visible skin", lambda event: event.get("event") == "first_visible_ready" or (event.get("event") == "request_window" and int(event.get("visible_ready", 0)) > 0))
			first_observed_ms = (time.monotonic() - entry_command_time) * 1000
			try:
				viewport = (
					first
					if (first.get("event") == "request_window" and first["visible_ready"] == first["visible"])
					else cursor.wait("settled first viewport", lambda event: event.get("event") == "all_visible_ready" or (event.get("event") == "request_window" and int(event.get("visible", 0)) > 0 and event["visible_ready"] == event["visible"]), 4)
				)
			except RuntimeError:
				if not client.is_alive():
					raise
				viewport = None
			viewport_observed_ms = (time.monotonic() - entry_command_time) * 1000 if viewport else None
			viewport_max_ready = max((int(event.get("visible_ready", 0)) for event in cursor.events if event.get("event") == "request_window"), default=0)
			settled = (
				viewport
				if viewport and int(viewport.get("total", 0)) >= len(pngs)
				else next(
					(event for event in reversed(cursor.events) if event.get("event") == "request_window" and int(event.get("total", 0)) >= len(pngs)),
					None,
				)
			)
			if settled is None:
				settled = cursor.wait("full corpus list count", lambda event: event.get("event") == "request_window" and int(event.get("total", 0)) >= len(pngs))
			client.command("qm_settings_prewarm 1")
			candidate_names = [Path(name).stem for name in pngs if re.fullmatch(r"(?!x_)[a-zA-Z0-9_-]{9,23}\.png", name, re.IGNORECASE)]
			if len(candidate_names) < 10:
				raise RuntimeError("corpus does not have enough searchable skin names for navigation")
			navigation = []
			for fraction in (0.92, 0.55, 0.2, 0.8, 0.35, 0.65):
				name = candidate_names[int(len(candidate_names) * fraction)]
				client.command(f"cl_skin_filter_string {name}")
				navigation.append(cursor.wait(f"viewport navigated to {name}", lambda event: event.get("event") == "request_window" and str(event.get("first_visible_skin", "")).casefold() == name.casefold()))
			client.command('cl_skin_filter_string ""')
			last = cursor.wait("full corpus viewport restored", lambda event: event.get("event") == "request_window" and int(event.get("total", 0)) >= len(pngs))
			client.command("quit")
			if client.wait_for_exit(25) != 0:
				raise RuntimeError("client did not exit normally")
		finally:
			png_release.set()
			if client is not None:
				client.kill()
				(artifact / "client.log").write_text("\n".join(client._lines) + "\n", encoding="utf-8")
			server.shutdown()
			worker.join(5)

	assert client is not None
	lines = log.read_text(encoding="utf-8").splitlines()
	events = [event for line in lines if (event := perf_event(line))]
	entry_frame = int(entry["frame"]) or next(int(event["frame"]) for event in events if event.get("event") == "request_window" and int(event["frame"]) > 0)
	last_frame = int(last["frame"])
	upload = [event for event in events if event.get("event") == "skin_upload_frame" and entry_frame <= int(event["frame"]) <= last_frame]
	frames = frame_stats(events, entry_frame, last_frame)
	transfers = {int(match[1]): float(match[2]) for line in [*lines, *client._lines] if (match := re.search(r"event=gpu_transfer_batch backend_frame=(\d+) gpu_transfer_ms=([\d.]+)", line))}
	transfer_times = list(transfers.values())
	if not frames["frames"] or not upload:
		raise RuntimeError("missing frame or skin-upload samples")
	if (mode == "online" and not (pngs.keys() & served.keys())) or (mode == "offline" and pngs.keys() & requests.keys()):
		raise RuntimeError(f"unexpected client PNG HTTP activity in {mode} mode")
	result = {
		"mode": mode,
		"backend": args.backend,
		"frame_limit": args.fps,
		"window": [1280, 720],
		"ui_scale": 100,
		"vsync": False,
		"renderer": renderer,
		"gpu_version": version,
		"loopback_png_released_on_tee_command": mode == "online",
		"client": str(binary),
		"client_sha256": binary_hash,
		"corpus_count": len(pngs),
		"first_ready_observed_ms_after_command": first_observed_ms,
		"viewport_settled_observed_ms_after_command": viewport_observed_ms,
		"corpus_filename_sha256": sha256("\n".join(pngs).encode()).hexdigest(),
		"http_png_unique_requested": len(pngs.keys() & requests.keys()),
		"http_png_unique_served": len(pngs.keys() & served.keys()),
		"first_visible": first,
		"first_viewport_settled": viewport,
		"first_viewport_max_ready_observed": viewport_max_ready,
		"full_corpus_window": settled,
		"viewport_navigation": navigation,
		"frame_stats": frames,
		"first_viewport_frame_stats": frame_stats(events, entry_frame, int(viewport["frame"])) if viewport else None,
		"viewport_navigation_frame_stats": frame_stats(events, int(settled["frame"]), last_frame),
		"upload_events": upload,
		"upload_summary": {
			"frames": len(upload),
			"attempts": sum(int(event["attempts"]) for event in upload),
			"uploads": sum(int(event["uploads"]) for event in upload),
			"prepared_bytes": sum(int(event["prepared_bytes"]) for event in upload),
			"max_submit_ms": max(float(event["submit_ms"]) for event in upload),
			"p95_submit_ms": percentile([float(event["submit_ms"]) for event in upload], 0.95),
		},
		"vulkan_transfer_batches_all_session": {"count": len(transfer_times), "max_ms": max(transfer_times, default=None), "p95_ms": percentile(transfer_times, 0.95) if transfer_times else None},
	}
	(artifact / "summary.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
	print(json.dumps({key: value for key, value in result.items() if key not in {"upload_events", "viewport_navigation", "full_corpus_window"}}, ensure_ascii=False), flush=True)
	return artifact


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--build-dir", required=True, type=Path)
	parser.add_argument("--staged-client", type=Path, help="Reuse an existing staged executable already configured for isolated storage")
	parser.add_argument("--corpus", required=True, type=Path, help="Directory of unique, verified real PNG skins")
	parser.add_argument("--require-count", type=int, default=10000)
	parser.add_argument("--backend", choices=("Vulkan", "OpenGL"), default="Vulkan")
	parser.add_argument("--fps", type=int, default=160)
	parser.add_argument("--mode", choices=("online", "offline", "both"), default="both")
	args = parser.parse_args()
	build = args.build_dir.resolve()
	cache = read_cmake_cache(build)
	if Path(cache.get("CMAKE_HOME_DIRECTORY", "")).resolve() != REPO_ROOT or not build.is_relative_to(REPO_ROOT):
		parser.error("build must belong to this workspace")
	if os.name == "nt" and (cache.get("QMCLIENT_TEST_STORAGE") == "ON" or os.environ.get("QMCLIENT_TEST_STORAGE_ROOT")):
		parser.error("select a normal Windows build with no environment override for the staged storage configuration")
	if args.staged_client and (not args.staged_client.is_file() or not args.staged_client.resolve().is_relative_to(REPO_ROOT / "tmp")):
		parser.error("staged executable must exist under workspace tmp")
	if args.fps < 1 or args.fps > 10000:
		parser.error("fps must be between 1 and 10000")
	pngs = {path.name: path.resolve() for path in sorted(args.corpus.glob("*.png"), key=lambda path: path.name.casefold())}
	if args.require_count < 1 or len(pngs) < args.require_count or len({name.casefold() for name in pngs}) != len(pngs):
		parser.error(f"found {len(pngs)} PNGs, expected at least {args.require_count} unique names")
	for mode in ("online", "offline") if args.mode == "both" else (args.mode,):
		scene(build, pngs, mode, args)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
