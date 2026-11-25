#!/usr/bin/env python3
"""Measure a fixed cold glyph batch and simultaneous settings switch in an isolated client."""

from __future__ import annotations

import argparse
from collections.abc import Callable
import hashlib
import json
import math
import os
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

try:
	from qmclient_scripts.integration.process_build import REPO_ROOT, read_cmake_cache
	from qmclient_scripts.integration.process_harness import EXE_SUFFIX, Process, log_message
except ModuleNotFoundError:
	from process_build import REPO_ROOT, read_cmake_cache
	from process_harness import EXE_SUFFIX, Process, log_message


MODES = (0, 1, 2)
SETTINGS_PAGE = 10
WINDOW_SIZE = (1280, 720)
FPS_CAP = 300
PERF_PREFIX = "perf/"
BATCH = {"first": 0x4E00, "count": 256, "px": 22}


def read_event(message: str) -> dict:
	if not message.startswith(PERF_PREFIX):
		return {}
	_, separator, payload = message.partition(": ")
	if not separator or not payload.startswith('{"system":'):
		return {}
	return json.loads(payload)


def is_text_summary(event: dict) -> bool:
	return event.get("system") == "perf/text" and event.get("event") == "text_runtime_summary"


def wait_for_event(client: Process, directory: Path, predicate: Callable[[dict], bool], description: str, timeout: float) -> tuple[Path, dict]:
	deadline = time.monotonic() + timeout
	log = None
	offset = 0
	while time.monotonic() < deadline and client.is_alive():
		if log is None:
			log = next(directory.glob("dumps/QmClient_Perf/*.log"), None)
		if log is not None:
			with log.open("rb") as source:
				source.seek(offset)
				for line in source:
					if not line.endswith(b"\n"):
						break
					offset += len(line)
					event = read_event(log_message(line.decode("utf-8")))
					if predicate(event):
						return log, event
		time.sleep(0.1)
	raise RuntimeError(f"client did not log {description} before timeout or exit")


def run_sample(binary: Path, directory: Path, mode: int, test_storage: bool, data_dir: Path) -> dict:
	directory.mkdir()
	if test_storage:
		# 隔离存储只改存档根目录；字体仍必须从构建目录的 data 读取。
		# 缺少这条路径时中文字体目录为空，所有文字消失，预热永久停住。
		(directory / "storage.cfg").write_text(f"add_path .\nadd_path {data_dir.as_posix()}\n", encoding="utf-8")
	config = [
		"gfx_fullscreen 0",
		"gfx_backend Vulkan",
		"qm_graphics_mode -1",
		"gfx_fsaa_samples 0",
		"qm_graphics_trace 1",
		f"gfx_screen_width {WINDOW_SIZE[0]}",
		f"gfx_screen_height {WINDOW_SIZE[1]}",
		"gfx_vsync 0",
		"gfx_backgroundrender 1",
		f"cl_refresh_rate {FPS_CAP}",
		f"cl_refresh_rate_inactive {FPS_CAP}",
		"qm_ui_scale 100",
		"cl_save_settings 0",
		"cl_show_welcome 0",
		"cl_skip_start_menu 1",
		"cl_download_skins 0",
		"qm_download_ddstats_skins 0",
		"qm_auto_update 0",
		"qm_steam_auto_launch 0",
		"qm_websocket_url disabled",
		"snd_enable 0",
		"qm_perf_debug 1",
		"qm_perf_logfile 1",
		"qm_perf_stutter_diagnostics 0",
		"qm_settings_prewarm 0",
		"qm_settings_prewarm_migrated 1",
		f"qm_text_raster_mode {mode}",
		"cl_languagefile languages/simplified_chinese.txt",
		"ui_page 16",
		"ui_settings_page 1",
	]
	client = Process(
		"client",
		[str(binary), *config],
		directory,
		fifo_command="cl_input_fifo",
		pipe_prefix="qmglyphperf_",
		env={"QMCLIENT_TEST_HIDE_DIALOG": "1", **({"QMCLIENT_TEST_STORAGE_ROOT": str(directory)} if test_storage else {})},
	)
	try:
		storage = client.wait_for(lambda line: line.startswith("storage: added path "), "isolated storage path", 30)
		if str(directory).replace("\\", "/").casefold() not in storage.replace("\\", "/").casefold():
			raise RuntimeError(f"unexpected client storage path: {storage}")
		renderer = client.wait_for(lambda line: line.startswith("gfx: GPU renderer: "), "real GPU renderer", 30)
		version = client.wait_for(lambda line: line.startswith("gfx: GPU version: "), "Vulkan GPU version", 30)
		if "vulkan" not in version.casefold():
			raise RuntimeError(f"Vulkan fallback detected: {version}")
		client.wait_for(lambda line: "event=first_cjk_glyph" in line, "bundled CJK font glyph", 30)
		wait_for_event(client, directory, is_text_summary, "settled settings page", 30)
		client.command(f"qm_glyph_benchmark; ui_settings_page {SETTINGS_PAGE}")
		log, marker = wait_for_event(client, directory, lambda event: event.get("event") in {"glyph_benchmark_done", "glyph_benchmark_error"},
			"fixed cold glyph batch completion", 15)
		if marker["event"] != "glyph_benchmark_done":
			raise ValueError(f"cold glyph batch failed: {marker.get('reason')}")
		wait_for_event(client, directory, is_settings_switch, "30-frame TClient settings switch", 15)
		client.command("quit")
		if (exit_code := client.wait_for_exit(20)) != 0:
			raise RuntimeError(f"client exited with {exit_code}")
	finally:
		client.stop()
		(directory / "client.log").write_text("\n".join(client._lines) + "\n", encoding="utf-8")
	result = summarize_batch(log, mode)
	return {**result, "mode": mode, "renderer": renderer, "device_version": version, "log": str(log)}


def is_settings_switch(event: dict) -> bool:
	return event.get("event") == "fps_summary" and event.get("operation") == "settings_tab_switch" and event.get("page") == "settings:tclient"


def summarize_batch(log: Path, mode: int) -> dict:
	frames = {}
	events = {}
	with log.open(encoding="utf-8") as source:
		for line in source:
			event = read_event(log_message(line))
			name = event.get("event")
			if name == "frame_batch":
				for frame, duration in zip(event["frames"], event["durations_ms"], strict=True):
					if frame in frames or not math.isfinite(duration) or duration < 0:
						raise ValueError(f"invalid frame sample {frame}: {duration}")
					frames[frame] = duration
			elif name in {"glyph_benchmark_begin", "glyph_benchmark_done", "session_start", "session_end"}:
				if name in events:
					raise ValueError(f"duplicate {name}")
				events[name] = event
			elif is_settings_switch(event):
				events["switch"] = event
	started, ended = (events[name] for name in ("session_start", "session_end"))
	begin, done, switch = (events[name] for name in ("glyph_benchmark_begin", "glyph_benchmark_done", "switch"))
	if started["frame_samples"] != "all" or ended["reason"] != "shutdown" or any(event["session"] != started["session"] for event in events.values()):
		raise ValueError("expected one complete session with all rendered frames")
	if any(begin[key] != expected for key, expected in BATCH.items()) or begin["mode"] != mode or done["mode"] != mode or done["ready"] != BATCH["count"]:
		raise ValueError(f"unexpected cold batch: {begin}, {done}")
	if not math.isfinite(done["wall_ms"]) or done["wall_ms"] <= 0 or bool(done["worker_jobs"]) != (mode != 0):
		raise ValueError(f"unexpected timing or worker activity: {done}")
	if mode == 2:
		if done["gpu_glyphs"] <= 0 or done["gpu_glyphs"] + done["gpu_fallbacks"] != BATCH["count"]:
			raise ValueError(f"unaccounted GPU glyphs: {done}")
	elif done["gpu_glyphs"] or done["gpu_fallbacks"]:
		raise ValueError(f"unexpected GPU activity: {done}")
	between = sorted((frame, duration) for frame, duration in frames.items() if frame > begin["frame"])
	if len(between) < 30 or switch["window_start_frame"] != between[0][0]:
		raise ValueError("settings switch did not coincide with the first rendered frame after the batch request")
	samples = sorted(duration for _, duration in between[:30])
	return {
		"revision": started.get("revision"), "batch": begin, "completion": done, "switch": switch,
		"switch_frames": 30, "switch_first_ms": between[0][1],
		"switch_p95_ms": samples[math.ceil(len(samples) * 0.95) - 1], "switch_max_ms": samples[-1],
	}


def stage_client(build: Path, bundle: Path) -> Path:
	bundle.mkdir()
	name = f"DDNet{EXE_SUFFIX}"
	shutil.copy2(build / name, bundle / name)
	for dependency in build.iterdir():
		if dependency.is_file() and dependency.suffix.lower() in {".dll", ".dylib", ".so"}:
			shutil.copy2(dependency, bundle / dependency.name)
	shutil.copytree(build / "data", bundle / "data")
	(bundle / "storage.cfg").write_text("add_path $CURRENTDIR\nadd_path $DATADIR\n", encoding="utf-8")
	return bundle / name


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("build_dir", type=Path, help="already built client build directory")
	parser.add_argument("--repetitions", type=int, default=3)
	parser.add_argument("--modes", type=int, choices=MODES, nargs="+", default=MODES)
	args = parser.parse_args()
	if args.repetitions < 1 or len(set(args.modes)) != len(args.modes):
		parser.error("need positive repetitions and unique modes")
	build = args.build_dir.resolve()
	cache = read_cmake_cache(build)
	if not build.is_relative_to(REPO_ROOT) or Path(cache["CMAKE_HOME_DIRECTORY"]).resolve() != REPO_ROOT:
		parser.error("build directory must belong to this workspace")
	test_storage = os.name == "nt" and cache.get("QMCLIENT_TEST_STORAGE") == "ON"
	if os.name == "nt" and not test_storage and os.environ.get("QMCLIENT_TEST_STORAGE_ROOT") is not None:
		parser.error("unset QMCLIENT_TEST_STORAGE_ROOT before running a normal Windows build")
	binary = build / f"DDNet{EXE_SUFFIX}"
	if not binary.is_file() or not (build / "data").is_dir():
		parser.error(f"missing client binary or data directory in {build}")
	root = REPO_ROOT / "tmp" / "perf-scenarios" / "glyph-raster"
	root.mkdir(parents=True, exist_ok=True)
	output = Path(tempfile.mkdtemp(prefix="run-", dir=root))
	print(f"Artifacts: {output}", flush=True)
	staged_binary = binary if test_storage else stage_client(build, output / "client")
	with staged_binary.open("rb") as source:
		binary_hash = hashlib.file_digest(source, "sha256").hexdigest()
	result = {
		"binary": str(binary),
		"binary_sha256": binary_hash,
		"build_type": cache.get("CMAKE_BUILD_TYPE"),
		"git_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO_ROOT, text=True).strip(),
		"window": WINDOW_SIZE,
		"fps_cap": FPS_CAP,
		"vsync": False,
		"ui_scale": 100,
		"settings_page": SETTINGS_PAGE,
		"input": BATCH,
		"trials": [],
	}
	failed = False
	font = None
	modes = list(args.modes)
	for repetition in range(args.repetitions):
		order_offset = repetition % len(modes)
		for mode in modes[order_offset:] + modes[:order_offset]:
			directory = output / f"rep{repetition + 1}-mode{mode}"
			trial = {"repetition": repetition + 1, "mode": mode, "directory": str(directory)}
			try:
				trial.update(run_sample(staged_binary, directory, mode, test_storage, build / "data"))
				current_font = (trial["batch"]["family"], trial["batch"]["style"])
				if font is not None and current_font != font:
					raise ValueError(f"resolved font changed across trials: {font} -> {current_font}")
				font = current_font
			except (RuntimeError, ValueError, OSError, KeyError) as error:
				trial["error"] = str(error).splitlines()[0]
				failed = True
			result["trials"].append(trial)
			(output / "summary.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
			if "error" in trial:
				print(f"{directory.name}: {trial['error']}", flush=True)
			else:
				completion = trial["completion"]
				print(f"{directory.name}: batch={completion['wall_ms']:.3f} ms GPU={completion['gpu_glyphs']} fallback={completion['gpu_fallbacks']} "
					f"switch p95={trial['switch_p95_ms']:.3f} ms max={trial['switch_max_ms']:.3f} ms", flush=True)
	return int(failed)


if __name__ == "__main__":
	raise SystemExit(main())
