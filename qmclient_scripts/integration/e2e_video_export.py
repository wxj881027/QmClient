#!/usr/bin/env python3
"""从真实客户端验证 Demo 视频导出的 Range 与分段路径。

前置：需要一份可播放的 demo。优先取环境变量 `QM_E2E_DEMO`，否则使用
`%APPDATA%/DDNet/demos/qmmkvtest.demo`；两者都缺失时场景报 SKIPPED，
不会伪装成通过。

用法：
    python qmclient_scripts/integration/e2e_video_export.py cmake-build-release
    python qmclient_scripts/integration/e2e_video_export.py cmake-build-release video_export_range
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from unittest import SkipTest

try:
	from qmclient_scripts.integration.process_harness import ProcessEnvironment
except ModuleNotFoundError:
	from process_harness import ProcessEnvironment

# 固定短片段：2 秒输出足以验证编码、容器与时长，又不会把 e2e 拖成分钟级。
RANGE_START_SECONDS = 2
RANGE_END_SECONDS = 4
OUTPUT_WIDTH = 320
OUTPUT_HEIGHT = 180


def demo_path() -> Path | None:
	candidates: list[Path] = []
	override = os.environ.get("QM_E2E_DEMO")
	if override:
		candidates.append(Path(override))
	appdata = os.environ.get("APPDATA")
	if appdata:
		candidates.append(Path(appdata) / "DDNet" / "demos" / "qmmkvtest.demo")
	return next((path for path in candidates if path.is_file()), None)


def start_demo_client(env: ProcessEnvironment, extra_config: list[str]) -> object:
	"""启动窗口化客户端并立即播放 demo；不连服务器，避免网络噪声。"""
	config = [
		"gfx_fullscreen 0",
		f"gfx_screen_width {OUTPUT_WIDTH * 2}",
		f"gfx_screen_height {OUTPUT_HEIGHT * 2}",
		"snd_enable 0",
		"cl_show_welcome 0",
		"cl_skip_start_menu 1",
		"cl_save_settings 0",
		"qm_auto_update 0",
		"qm_steam_auto_launch 0",
		# 不能 minimize：Vulkan 交换链在窗口最小化时暂停（paused=1），帧捕获读不到呈现帧，
		# 录制必然 READ_FAILED；导出需要可见窗口持续呈现帧（wt-1 实测同为前台窗口）。
		*extra_config,
	]
	return env.start_client(config=config, connect=False)


def probe_duration(video: Path) -> float:
	ffprobe = shutil.which("ffprobe")
	if ffprobe is None:
		raise SkipTest("ffprobe 不可用，无法校验输出时长")
	result = subprocess.run(
		[ffprobe, "-v", "error", "-show_entries", "format=duration", "-of", "json", str(video)],
		capture_output=True,
		text=True,
		check=False,
	)
	if result.returncode != 0:
		raise AssertionError(f"ffprobe 无法读取 {video}: {result.stderr.strip()}")
	return float(json.loads(result.stdout)["format"]["duration"])


def assert_video(video: Path, expected_seconds: float, tolerance: float = 0.8) -> None:
	if not video.is_file():
		raise AssertionError(f"预期产物不存在: {video}")
	if video.stat().st_size <= 0:
		raise AssertionError(f"产物为空: {video}")
	duration = probe_duration(video)
	if abs(duration - expected_seconds) > tolerance:
		raise AssertionError(f"输出时长 {duration:.2f}s 与预期 {expected_seconds:.1f}s 偏差超过 {tolerance}s")


def scenario_video_export_range(env: ProcessEnvironment) -> None:
	"""单段 Range 导出：start_video <name> <start> <end> 应在 end 秒自动停止。"""
	demo = demo_path()
	if demo is None:
		raise SkipTest("缺少 demo：设置 QM_E2E_DEMO 或放置 AppData/DDNet/demos/qmmkvtest.demo")

	client = start_demo_client(
		env,
		[
			"demo_speed 8",
			"qm_video_segment_seconds 0",
			"qm_video_encoder libx264",
			"qm_video_container mp4",
			f"qm_video_width {OUTPUT_WIDTH}",
			f"qm_video_height {OUTPUT_HEIGHT}",
		],
	)
	try:
		client.command(f'play "{demo}"')
		# 播放开始的唯一 STANDARD 级日志是 "Loading demo"；"playing" 行不存在。
		client.wait_for(lambda line: "demo_player" in line and "loading demo" in line.lower(), "demo 开始播放", 60)
		client.command(f"start_video qme2e_range {RANGE_START_SECONDS} {RANGE_END_SECONDS}")
		# 指定 Range 统一走分段任务状态机（段长 0 接管单段），完成日志是 Concatenated；
		# 旧直录路径的 "Stopped recording." 在该路径不再出现。
		client.wait_for(lambda line: "Concatenated" in line and "segment(s) into" in line, "Range 导出完成", 180)
		client.command("quit")
		if client.wait_for_exit(30) != 0:
			raise AssertionError("客户端未正常退出")
	finally:
		client.stop()

	video = env.temp_dir / "videos" / "qme2e_range.mp4"
	assert_video(video, RANGE_END_SECONDS - RANGE_START_SECONDS)


def scenario_video_export_segmented_range(env: ProcessEnvironment) -> None:
	"""分段 Range 导出：段长 2s、区间 [2,6] 应产生 2 段并自动拼接。"""
	demo = demo_path()
	if demo is None:
		raise SkipTest("缺少 demo：设置 QM_E2E_DEMO 或放置 AppData/DDNet/demos/qmmkvtest.demo")

	segment_seconds = 2
	end_seconds = 6
	client = start_demo_client(
		env,
		[
			"demo_speed 8",
			f"qm_video_segment_seconds {segment_seconds}",
			"qm_video_encoder libx264",
			"qm_video_container mkv",
			f"qm_video_width {OUTPUT_WIDTH}",
			f"qm_video_height {OUTPUT_HEIGHT}",
		],
	)
	try:
		client.command(f'play "{demo}"')
		# 播放开始的唯一 STANDARD 级日志是 "Loading demo"；"playing" 行不存在。
		client.wait_for(lambda line: "demo_player" in line and "loading demo" in line.lower(), "demo 开始播放", 60)
		client.command(f"start_video qme2e_segmented {RANGE_START_SECONDS} {end_seconds}")
		client.wait_for(
			lambda line: "Segmented export" in line and f"{segment_seconds} segment(s)" in line,
			"分段计划日志",
			60,
		)
		client.wait_for(lambda line: "Concatenated" in line and "segment(s) into" in line, "分段拼接完成", 300)
		client.command("quit")
		if client.wait_for_exit(30) != 0:
			raise AssertionError("客户端未正常退出")
	finally:
		client.stop()

	video = env.temp_dir / "videos" / "qme2e_segmented.mkv"
	assert_video(video, end_seconds - RANGE_START_SECONDS)


E2E_TESTS = {
	"video_export_range": scenario_video_export_range,
	"video_export_segmented_range": scenario_video_export_segmented_range,
}


def main() -> int:
	parser = argparse.ArgumentParser(description="Run QmClient video export end-to-end tests")
	parser.add_argument("build_dir", type=Path)
	parser.add_argument("test", choices=sorted(E2E_TESTS), nargs="?")
	args = parser.parse_args()

	tests = {args.test: E2E_TESTS[args.test]} if args.test else E2E_TESTS
	failed = 0
	for name, test in tests.items():
		env = ProcessEnvironment(args.build_dir, temp_prefix="qmclient_video_e2e_")
		keep_temp = False
		try:
			test(env)
		except SkipTest as exc:
			print(f"{name}: SKIPPED ({exc})")
		except Exception as exc:  # pylint: disable=broad-exception-caught
			failed += 1
			keep_temp = True
			print(f"{name}: FAILED\n{exc}\nartifacts: {env.temp_dir}", file=sys.stderr)
			for label, tail in env.process_tails():
				print(f"--- {label} tail ---", file=sys.stderr)
				for line in tail:
					print(line.rstrip(), file=sys.stderr)
		else:
			print(f"{name}: passed")
		finally:
			env.close(keep_temp=keep_temp)
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
