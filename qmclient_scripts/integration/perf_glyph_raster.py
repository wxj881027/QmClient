#!/usr/bin/env python3
"""Measure the real settings glyph prewarm pipeline on a prebuilt isolated client.

Each fresh process opens the same settings page and keeps it open. Cold means
the first five-second runtime window, warm the last window of the same process.
Frame times are end-to-end intervals from the production frame batches.
"""

from __future__ import annotations

import argparse
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
GLYPH_HISTORY = Path("qmclient/cache/settings/glyph_prewarm.txt")
SEED_CODEPOINTS = range(0x4E00, 0x5000)
SEED_FONT_SIZE = 22


def read_event(message: str) -> dict:
	if not message.startswith(PERF_PREFIX):
		return {}
	_, separator, payload = message.partition(": ")
	if not separator or not payload.startswith('{"system":'):
		return {}
	return json.loads(payload)


def is_text_summary(event: dict) -> bool:
	return event.get("system") == "perf/text" and event.get("event") == "text_runtime_summary"


def wait_for_summaries(client: Process, directory: Path, count: int) -> Path:
	deadline = time.monotonic() + 20 + 6 * count
	log = None
	offset = 0
	seen = set()
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
					if b'"event":"text_runtime_summary"' in line:
						event = read_event(log_message(line.decode("utf-8")))
						if is_text_summary(event):
							seen.add(event["frame"])
							if len(seen) >= count:
								return log
		time.sleep(0.1)
	raise RuntimeError(f"client produced {len(seen)}/{count} runtime windows before timeout or exit")


def summarize(log: Path, summary_count: int, mode: int) -> dict:
	frames: dict[int, float] = {}
	summaries = []
	settings_pages = set()
	startup_prewarm = None
	started = None
	ended = False
	observed_mode = None
	with log.open(encoding="utf-8") as source:
		for line in source:
			event = read_event(log_message(line))
			if not event:
				continue
			event_name = event.get("event")
			if event_name == "session_start":
				if started is not None or event.get("frame_samples") != "all":
					raise ValueError("expected one session recording every rendered frame")
				started = event
			if started is None or event.get("session") != started["session"]:
				raise ValueError("performance event outside its session")
			if event_name == "session_end":
				ended = event.get("reason") == "shutdown"
			elif event_name == "config_value" and event.get("name") == "qm_text_raster_mode":
				observed_mode = int(event["value"])
				if observed_mode != mode:
					raise ValueError(f"client used raster mode {observed_mode}, requested {mode}")
			elif event_name == "frame_batch" and event.get("system") == "perf/frame":
				for frame, duration in zip(event["frames"], event["durations_ms"], strict=True):
					if type(frame) is not int or frame <= 0 or frame in frames or not math.isfinite(duration) or duration < 0:
						raise ValueError(f"invalid or duplicate frame sample {frame}: {duration}")
					frames[frame] = duration
			elif is_text_summary(event):
				summaries.append(event)
			elif event_name == "startup_glyph_prewarm":
				startup_prewarm = event
			elif event_name in {"text_prewarm_window", "settings_text_usage"}:
				page = event.get("page", "")
				if isinstance(page, str) and page.startswith("settings:"):
					settings_pages.add(page)
	if not ended or observed_mode is None or len(summaries) < summary_count:
		raise ValueError("incomplete session, raster config, or text runtime samples")
	if "settings:tclient" not in settings_pages:
		raise ValueError(f"TClient settings page did not report prewarm or text activity: {sorted(settings_pages)}")
	assert started is not None
	windows = []
	start_frame = started["frame"]
	for position, summary in enumerate(summaries[:summary_count]):
		end_frame = summary["frame"]
		if end_frame <= start_frame:
			raise ValueError("text runtime summaries must carry increasing client frame IDs")
		samples = sorted(duration for frame, duration in frames.items() if start_frame < frame < end_frame)
		if not samples:
			raise ValueError(f"no rendered frames for the text runtime window ending at frame {end_frame}")
		windows.append({
			"phase": "cold" if position == 0 else "warm" if position == summary_count - 1 else "transition",
			"first_sample_after_frame": start_frame,
			"last_sample_before_frame": end_frame,
			"sample_frames": len(samples),
			"frame_ms_p95": samples[math.ceil(len(samples) * 0.95) - 1],
			"frame_ms_max": samples[-1],
			"text_runtime": summary,
		})
		start_frame = end_frame - 1
	return {"revision": started.get("revision"), "pages": sorted(settings_pages), "windows": windows, "startup_prewarm": startup_prewarm}


def require_raster_activity(result: dict, mode: int) -> None:
	summaries = [window["text_runtime"] for window in result["windows"]]
	totals = {
		name: sum(window[name] for window in summaries)
		for name in (
			"glyph_new",
			"glyph_worker_queued",
			"glyph_worker_completed",
			"glyph_worker_jobs",
			"gpu_batches",
			"gpu_glyphs",
			"gpu_readbacks",
			"gpu_fallbacks",
		)
	}
	result["totals"] = totals
	if mode == 0:
		if not totals["glyph_new"] or any(value for key, value in totals.items() if key != "glyph_new"):
			raise ValueError(f"expected sync glyph work without worker or GPU activity: {totals}")
	elif mode == 1:
		if not all(totals[key] for key in ("glyph_worker_queued", "glyph_worker_completed", "glyph_worker_jobs")):
			raise ValueError(f"expected CPU worker job completion: {totals}")
		if any(value for key, value in totals.items() if key.startswith("gpu_")):
			raise ValueError(f"unexpected GPU activity in CPU worker mode: {totals}")
	elif not all(totals[key] for key in ("gpu_batches", "gpu_glyphs", "gpu_readbacks")):
		raise ValueError(f"expected real GPU batches and readbacks: {totals}")


def run_sample(binary: Path, directory: Path, mode: int, summary_count: int, test_storage: bool) -> dict:
	directory.mkdir()
	history = directory / GLYPH_HISTORY
	history.parent.mkdir(parents=True)
	history.write_text("".join(f"{codepoint} {SEED_FONT_SIZE}\n" for codepoint in SEED_CODEPOINTS), encoding="ascii")
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
		"qm_auto_update 0",
		"qm_steam_auto_launch 0",
		"qm_websocket_url disabled",
		"snd_enable 0",
		"qm_perf_debug 1",
		"qm_perf_logfile 1",
		"qm_perf_stutter_diagnostics 0",
		"qm_settings_prewarm 1",
		f"qm_text_raster_mode {mode}",
		"cl_languagefile languages/simplified_chinese.txt",
		"ui_page 16",
		f"ui_settings_page {SETTINGS_PAGE}",
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
		log = wait_for_summaries(client, directory, summary_count)
		client.command("quit")
		if (exit_code := client.wait_for_exit(20)) != 0:
			raise RuntimeError(f"client exited with {exit_code}")
	finally:
		client.stop()
		(directory / "client.log").write_text("\n".join(client._lines) + "\n", encoding="utf-8")
	result = summarize(log, summary_count, mode)
	return {**result, "mode": mode, "renderer": renderer, "device_version": version, "log": str(log)}


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
	parser.add_argument("--windows", type=int, default=4, help="number of five-second runtime windows per process")
	parser.add_argument("--modes", type=int, choices=MODES, nargs="+", default=MODES)
	args = parser.parse_args()
	if args.repetitions < 1 or args.windows < 2 or len(set(args.modes)) != len(args.modes):
		parser.error("need positive repetitions, at least two windows, and unique modes")
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
		"seed_history": {"first_codepoint": SEED_CODEPOINTS.start, "count": len(SEED_CODEPOINTS), "font_size": SEED_FONT_SIZE},
		"trials": [],
	}
	failed = False
	modes = list(args.modes)
	for repetition in range(args.repetitions):
		order_offset = repetition % len(modes)
		for mode in modes[order_offset:] + modes[:order_offset]:
			directory = output / f"rep{repetition + 1}-mode{mode}"
			trial = {"repetition": repetition + 1, "mode": mode, "directory": str(directory)}
			try:
				trial.update(run_sample(staged_binary, directory, mode, args.windows, test_storage))
				require_raster_activity(trial, mode)
			except (RuntimeError, ValueError, OSError, KeyError) as error:
				trial["error"] = str(error).splitlines()[0]
				failed = True
			result["trials"].append(trial)
			(output / "summary.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
			if "error" in trial:
				print(f"{directory.name}: {trial['error']}", flush=True)
			else:
				for window in (trial["windows"][0], trial["windows"][-1]):
					print(f"{directory.name} {window['phase']}: n={window['sample_frames']} p95={window['frame_ms_p95']:.3f} ms max={window['frame_ms_max']:.3f} ms", flush=True)
	return int(failed)


if __name__ == "__main__":
	raise SystemExit(main())
