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
import hashlib
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
# 非整数帧率下每帧样本不是整数；用它验证音频时间轴仍与画面时长一致。
NONINTEGER_FPS = 144
# AAC 一包 1024 样本在 48 kHz 下约 21.3 ms，是音画对齐的天然最小粒度。
AAC_PACKET_SECONDS = 1024 / 48000


def demo_path() -> Path | None:
	candidates: list[Path] = []
	override = os.environ.get("QM_E2E_DEMO")
	if override:
		candidates.append(Path(override))
	appdata = os.environ.get("APPDATA")
	if appdata:
		candidates.append(Path(appdata) / "DDNet" / "demos" / "qmmkvtest.demo")
	return next((path for path in candidates if path.is_file()), None)


def start_demo_client(env: ProcessEnvironment, extra_config: list[str], extra_env: dict[str, str] | None = None) -> object:
	"""启动窗口化客户端并立即播放 demo；不连服务器，避免网络噪声。"""
	config = [
		f'logfile "{env.path("client.log")}"',
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
	return env.start_client(config=config, connect=False, env=extra_env)


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


def probe_streams(video: Path) -> list[dict[str, object]]:
	"""用 ffprobe 读取每条流的类型、时长与帧率，用于音画时长比对。"""
	ffprobe = shutil.which("ffprobe")
	if ffprobe is None:
		raise SkipTest("ffprobe 不可用，无法校验音视频流")
	result = subprocess.run(
		[ffprobe, "-v", "error", "-show_entries", "stream=codec_type,duration,nb_frames,r_frame_rate,avg_frame_rate,channels", "-of", "json", str(video)],
		capture_output=True,
		text=True,
		check=False,
	)
	if result.returncode != 0:
		raise AssertionError(f"ffprobe 无法读取 {video}: {result.stderr.strip()}")
	return json.loads(result.stdout)["streams"]


def probe_stream_end(video: Path, selector: str) -> float:
	"""按包时间戳求某条流的覆盖终点（pts + duration）。

	matroska 不存每条流的时长，stream=duration 为空；音画同步必须比较包时间轴本身。
	"""
	ffprobe = shutil.which("ffprobe")
	if ffprobe is None:
		raise SkipTest("ffprobe 不可用，无法校验包时间轴")
	result = subprocess.run(
		[ffprobe, "-v", "error", "-select_streams", selector, "-show_entries", "packet=pts_time,duration_time", "-of", "csv=p=0", str(video)],
		capture_output=True,
		text=True,
		check=False,
	)
	if result.returncode != 0:
		raise AssertionError(f"ffprobe 无法读取 {video} 的 {selector} 包: {result.stderr.strip()}")
	end = None
	for line in result.stdout.splitlines():
		parts = line.split(",")
		if len(parts) < 2 or not parts[0]:
			continue
		pts = float(parts[0])
		duration = float(parts[1]) if parts[1] not in {"", "N/A"} else 0.0
		candidate = pts + duration
		end = candidate if end is None else max(end, candidate)
	if end is None:
		raise AssertionError(f"{video} 没有 {selector} 包")
	return end


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


def scenario_video_export_noninteger_fps_audio_sync(env: ProcessEnvironment) -> None:
	"""144 FPS 导出仍写出音频且音画时长一致，验证非整数帧率下时间轴推进可用。

	逐帧样本余数导致的长期漂移由 VideoAudioTimeline 单元测试覆盖；这里验证真实
	录制链路在非整数帧率下同时产出音视频流，且不会因时间轴改动整体中断。
	"""
	demo = demo_path()
	if demo is None:
		raise SkipTest("缺少 demo：设置 QM_E2E_DEMO 或提供测试 demo")
	name = "qme2e_noninteger_fps"
	expected_seconds = 12
	client = start_demo_client(
		env,
		[
			"snd_enable 1",
			"demo_speed 8",
			f"cl_video_recorder_fps {NONINTEGER_FPS}",
			"qm_video_segment_seconds 0",
			"qm_video_encoder libx264",
			"qm_video_container mkv",
			f"qm_video_width {OUTPUT_WIDTH}",
			f"qm_video_height {OUTPUT_HEIGHT}",
		],
	)
	try:
		client.command(f'play "{demo}"')
		client.wait_for(lambda line: "demo_player" in line and "loading demo" in line.lower(), "demo 开始播放", 60)
		client.command(f"start_video {name} {RANGE_START_SECONDS} {RANGE_START_SECONDS + expected_seconds}")
		client.wait_for(lambda line: "Audio recording enabled" in line, "音频录制已启用", 60)
		client.wait_for(lambda line: "Concatenated" in line and f"{name}.mkv" in line, "导出完成", 300)
		client.command("quit")
		if client.wait_for_exit(30) != 0:
			raise AssertionError("客户端未正常退出")
	finally:
		client.stop()

	video = env.path("videos", f"{name}.mkv")
	assert_video(video, expected_seconds)
	streams = probe_streams(video)
	video_streams = [stream for stream in streams if stream.get("codec_type") == "video"]
	audio_streams = [stream for stream in streams if stream.get("codec_type") == "audio"]
	if len(video_streams) != 1 or len(audio_streams) != 1:
		raise AssertionError(f"预期一条视频流和一条音频流，实际 {streams}")
	# avg_frame_rate 是 matroska DefaultDuration/封装器写入的权威帧率；r_frame_rate 由 FFmpeg
	# 按包时长反推，非整除帧间隔下可能给出非记录值，因此只校验平均帧率。
	if video_streams[0].get("avg_frame_rate") != f"{NONINTEGER_FPS}/1":
		raise AssertionError(f"视频平均帧率不是 {NONINTEGER_FPS} FPS: {video_streams[0].get('avg_frame_rate')} (r={video_streams[0].get('r_frame_rate')})")
	video_seconds = probe_stream_end(video, "v:0")
	audio_seconds = probe_stream_end(video, "a:0")
	# 余数保留后每帧平均样本与画面时长一致，允许编码器首尾填充的一帧级偏差。
	if abs(audio_seconds - video_seconds) > 0.1:
		raise AssertionError(f"音画时长不一致: audio={audio_seconds:.3f}s video={video_seconds:.3f}s")
	if abs(video_seconds - expected_seconds) > 0.8:
		raise AssertionError(f"视频时长 {video_seconds:.3f}s 偏离预期 {expected_seconds}s")


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


def scenario_video_export_segmented_audio_sync(env: ProcessEnvironment) -> None:
	"""分段导出同时录制声音：每段音频按视频时长裁剪后拼接，长区间不得逐段漂移。

	拼接对每段音频做预算裁剪（丢弃 AAC priming 与包对齐余数），否则每段约 24 ms
	的余量会累加。这里用 4 段共 8 秒，检查音画包时间轴终点偏差不超过一个 AAC 包，
	且明显小于"不裁剪"时约 4×24 ms 的累计量。
	"""
	demo = demo_path()
	if demo is None:
		raise SkipTest("缺少 demo：设置 QM_E2E_DEMO 或提供测试 demo")
	name = "qme2e_segmented_audio"
	segment_seconds = 2
	end_seconds = 10
	expected_seconds = end_seconds - RANGE_START_SECONDS
	client = start_demo_client(
		env,
		[
			"snd_enable 1",
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
		client.wait_for(lambda line: "demo_player" in line and "loading demo" in line.lower(), "demo 开始播放", 60)
		client.command(f"start_video {name} {RANGE_START_SECONDS} {end_seconds}")
		client.wait_for(lambda line: "Audio recording enabled" in line, "音频录制已启用", 60)
		client.wait_for(
			lambda line: "Segmented export" in line and f"{(expected_seconds + segment_seconds - 1) // segment_seconds} segment(s)" in line,
			"分段计划日志",
			60,
		)
		client.wait_for(lambda line: "Concatenated" in line and f"{name}.mkv" in line, "分段拼接完成", 600)
		client.command("quit")
		if client.wait_for_exit(30) != 0:
			raise AssertionError("客户端未正常退出")
	finally:
		client.stop()

	video = env.path("videos", f"{name}.mkv")
	assert_video(video, expected_seconds)
	streams = probe_streams(video)
	video_streams = [stream for stream in streams if stream.get("codec_type") == "video"]
	audio_streams = [stream for stream in streams if stream.get("codec_type") == "audio"]
	if len(video_streams) != 1 or len(audio_streams) != 1:
		raise AssertionError(f"分段带声音导出应有一条视频流和一条音频流，实际 {streams}")

	video_seconds = probe_stream_end(video, "v:0")
	audio_seconds = probe_stream_end(video, "a:0")
	if abs(video_seconds - expected_seconds) > 0.8:
		raise AssertionError(f"视频时长 {video_seconds:.3f}s 偏离预期 {expected_seconds}s")
	# AAC 一包 1024 样本约 21.3 ms；不裁剪时每段多出的余数会累加约 4×24 ms。
	drift = audio_seconds - video_seconds
	if abs(drift) > AAC_PACKET_SECONDS:
		raise AssertionError(f"分段带声音拼接音画漂移 {drift:+.3f}s 超过一个 AAC 包 {AAC_PACKET_SECONDS:.3f}s: audio={audio_seconds:.3f}s video={video_seconds:.3f}s")


def scenario_video_export_resume_truncated_segment(env: ProcessEnvironment) -> None:
	"""完整导出后截断一段：重启续渲只重做该段，保留另一段并产出完整成片。"""
	demo = demo_path()
	if demo is None:
		raise SkipTest("缺少 demo：设置 QM_E2E_DEMO 或提供测试 demo")
	config = [
		"demo_speed 8",
		"qm_video_segment_seconds 2",
		"qm_video_encoder libx264",
		"qm_video_container mkv",
		f"qm_video_width {OUTPUT_WIDTH}",
		f"qm_video_height {OUTPUT_HEIGHT}",
	]

	def render() -> None:
		client = start_demo_client(env, config)
		try:
			client.command(f'play "{demo}"')
			client.wait_for(lambda line: "demo_player" in line and "loading demo" in line.lower(), "demo 开始播放", 60)
			client.command("start_video qme2e_resume 2 6")
			client.wait_for(lambda line: "starting at segment 1/2" in line, "从第一段开始或恢复渲染", 60)
			client.wait_for(lambda line: "Concatenated" in line and "segment(s) into" in line, "恢复后的拼接完成", 300)
			client.command("quit")
			if client.wait_for_exit(30) != 0:
				raise AssertionError("客户端未正常退出")
		finally:
			client.stop()

	render()
	first = env.path("videos", "qme2e_resume_p001.mkv")
	second = env.path("videos", "qme2e_resume_p002.mkv")
	second_stat = second.stat()
	second_digest = hashlib.sha256(second.read_bytes()).digest()
	truncated_size = first.stat().st_size - 1
	if truncated_size <= 0:
		raise AssertionError("首段产物为空，无法执行截断恢复场景")
	with first.open("r+b") as file:
		file.truncate(truncated_size)
	truncated_digest = hashlib.sha256(first.read_bytes()).digest()

	render()
	if hashlib.sha256(first.read_bytes()).digest() == truncated_digest:
		raise AssertionError("截断的第一段没有重新生成")
	assert_video(first, 2)
	if second.stat().st_mtime_ns != second_stat.st_mtime_ns or hashlib.sha256(second.read_bytes()).digest() != second_digest:
		raise AssertionError("完好的第二段被重复渲染或被修改")
	assert_video(env.path("videos", "qme2e_resume.mkv"), 4)


def scenario_video_export_range_rejection_and_restart(env: ProcessEnvironment) -> None:
	"""非法区间不产物；只给结束时间可导出，完成后同一播放器可再次导出。"""
	demo = demo_path()
	if demo is None:
		raise SkipTest("缺少 demo：设置 QM_E2E_DEMO 或提供测试 demo")
	client = start_demo_client(
		env,
		[
			"demo_speed 1",
			"qm_video_segment_seconds 0",
			"qm_video_encoder libx264",
			"qm_video_container mkv",
			f"qm_video_width {OUTPUT_WIDTH}",
			f"qm_video_height {OUTPUT_HEIGHT}",
		],
	)
	try:
		client.command(f'play "{demo}"')
		client.wait_for(lambda line: "demo_player" in line and "loading demo" in line.lower(), "demo 开始播放", 60)
		for name, start, end in [("inverted", 4, 2), ("empty", 2, 2), ("end_zero", -1, 0)]:
			client.command(f"start_video qme2e_{name} {start} {end}")
			client.wait_for(
				lambda line, start=start, end=end: f"Invalid export range [{start:.3f}s, {end:.3f}s]" in line,
				f"拒绝 {name} 选区",
				30,
			)
			if list(env.path("videos").glob(f"qme2e_{name}*")):
				raise AssertionError(f"非法 {name} 选区产生了录像或进度")

		for name, start, end in [("end_only", -1, 2), ("restart", 2, 4)]:
			client.command(f"start_video qme2e_{name} {start} {end}")
			client.wait_for(
				lambda line, name=name: "Concatenated" in line and f"qme2e_{name}.mkv" in line,
				f"{name} 选区导出完成",
				180,
			)
			assert_video(env.path("videos", f"qme2e_{name}.mkv"), 2)
		client.command("quit")
		if client.wait_for_exit(30) != 0:
			raise AssertionError("客户端未正常退出")
	finally:
		client.stop()


def scenario_video_export_failed_start_preserves_output(env: ProcessEnvironment) -> None:
	"""同名直录初始化失败保留旧成片，随后同进程仍可正常导出。"""
	demo = demo_path()
	if demo is None:
		raise SkipTest("缺少 demo：设置 QM_E2E_DEMO 或提供测试 demo")
	client = start_demo_client(
		env,
		[
			"demo_speed 1",
			"qm_video_segment_seconds 0",
			"qm_video_encoder libx264",
			"qm_video_container mkv",
			f"qm_video_width {OUTPUT_WIDTH}",
			f"qm_video_height {OUTPUT_HEIGHT}",
		],
	)
	try:
		client.command(f'play "{demo}"')
		client.wait_for(lambda line: "demo_player" in line and "loading demo" in line.lower(), "demo 开始播放", 60)
		client.command("start_video qme2e_preserve 2 4")
		client.wait_for(lambda line: "Concatenated" in line and "qme2e_preserve.mkv" in line, "生成旧成片", 180)
		video = env.path("videos", "qme2e_preserve.mkv")
		assert_video(video, 2)
		previous = video.read_bytes()
		client.command("qm_video_encoder nonexistent_encoder")
		client.command("start_video qme2e_preserve")
		client.wait_for(lambda line: "Failed to start recording" in line and "qme2e_preserve.mkv" in line, "拒绝无效编码器", 60)
		if video.read_bytes() != previous:
			raise AssertionError("初始化失败改写了同名旧成片")
		if env.path("videos", "qme2e_preserve.mkv.recording").exists():
			raise AssertionError("初始化失败遗留临时输出")
		client.command("qm_video_encoder libx264")
		client.command(f'play "{demo}"')
		# wait_for 会扫描历史行；同一场景第二次播放必须按出现次数等待新加载行，
		# 否则会命中第一次播放的旧日志，在 demo 尚未加载时发出 start_video。
		seen_loading = 0

		def second_loading(line: str) -> bool:
			nonlocal seen_loading
			if "demo_player" in line and "loading demo" in line.lower():
				seen_loading += 1
				return seen_loading == 2
			return False

		client.wait_for(second_loading, "失败后 demo 重新加载", 60)
		client.command("start_video qme2e_after_failure 2 4")
		client.wait_for(lambda line: "Concatenated" in line and "qme2e_after_failure.mkv" in line, "失败后重启导出", 180)
		assert_video(env.path("videos", "qme2e_after_failure.mkv"), 2)
		client.command("quit")
		if client.wait_for_exit(30) != 0:
			raise AssertionError("客户端未正常退出")
	finally:
		client.stop()


def scenario_video_export_display_snapshot_and_resume(env: ProcessEnvironment) -> None:
	"""导出中改显示配置不改变当前任务快照；重启后指纹失效并从第一段重渲。"""
	demo = demo_path()
	if demo is None:
		raise SkipTest("缺少 demo：设置 QM_E2E_DEMO 或提供测试 demo")
	name = "qme2e_displaysnapshot"
	config = [
		"demo_speed 1",
		"qm_video_segment_seconds 2",
		"qm_video_encoder libx264",
		"qm_video_container mkv",
		f"qm_video_width {OUTPUT_WIDTH}",
		f"qm_video_height {OUTPUT_HEIGHT}",
	]

	client = start_demo_client(env, [*config, "qm_demo_show_chat 0"])
	try:
		client.command(f'play "{demo}"')
		client.wait_for(lambda line: "demo_player" in line and "loading demo" in line.lower(), "demo 开始播放", 60)
		client.command(f"start_video {name} 2 6")
		client.wait_for(
			lambda line: "display snapshot:" in line and name in line and "chat=0" in line,
			"第一段使用 chat=0 快照",
			60,
		)
		# 第一段已开录后改配置。第二段必须继续打印 chat=0，证明用的是任务启动时快照。
		client.command("qm_demo_show_chat 1")
		seen = 0

		def second_segment_uses_old_snapshot(line: str) -> bool:
			nonlocal seen
			if "display snapshot:" in line and name in line and "chat=0" in line:
				seen += 1
				return seen == 2
			return False

		client.wait_for(second_segment_uses_old_snapshot, "第二段仍使用 chat=0 快照", 90)
		client.wait_for(lambda line: "Concatenated" in line and f"{name}.mkv" in line, "首次导出完成", 180)
		client.command("quit")
		if client.wait_for_exit(30) != 0:
			raise AssertionError("客户端未正常退出")
	finally:
		client.stop()

	assert_video(env.path("videos", f"{name}.mkv"), 4)

	# 同一任务名、同一区间，显示配置改为 chat=1：指纹必须失效，不能从第二段续渲。
	client = start_demo_client(env, [*config, "qm_demo_show_chat 1"])
	try:
		client.command(f'play "{demo}"')
		client.wait_for(lambda line: "demo_player" in line and "loading demo" in line.lower(), "demo 开始播放", 60)
		client.command(f"start_video {name} 2 6")
		client.wait_for(lambda line: "starting at segment 1/2" in line and name in line, "显示配置变化后从第一段重渲", 60)
		client.wait_for(lambda line: "Concatenated" in line and f"{name}.mkv" in line, "重渲拼接完成", 300)
		client.command("quit")
		if client.wait_for_exit(30) != 0:
			raise AssertionError("客户端未正常退出")
	finally:
		client.stop()

	assert_video(env.path("videos", f"{name}.mkv"), 4)


def scenario_video_export_audio_failure_keeps_segment_pending(env: ProcessEnvironment) -> None:
	"""音频转换失败必须终止本次导出、不提交旧音频/半成品；去掉故障后可重试成功。"""
	demo = demo_path()
	if demo is None:
		raise SkipTest("缺少 demo：设置 QM_E2E_DEMO 或提供测试 demo")
	name = "qme2e_audio_fail"
	config = [
		"snd_enable 1",
		"demo_speed 1",
		"qm_video_segment_seconds 0",
		"qm_video_encoder libx264",
		"qm_video_container mkv",
		f"qm_video_width {OUTPUT_WIDTH}",
		f"qm_video_height {OUTPUT_HEIGHT}",
	]

	client = start_demo_client(env, config, extra_env={"QMCLIENT_TEST_VIDEO_AUDIO_FAIL": "1"})
	try:
		client.command(f'play "{demo}"')
		client.wait_for(lambda line: "demo_player" in line and "loading demo" in line.lower(), "demo 开始播放", 60)
		client.command(f"start_video {name} 2 4")
		client.wait_for(lambda line: "Audio recording enabled" in line, "音频录制已启用", 60)
		client.wait_for(lambda line: "Could not convert audio frame: injected test failure" in line, "注入音频转换失败", 60)
		client.wait_for(lambda line: "Segment 1 recording failed" in line, "失败段保持待渲", 60)
		client.command("quit")
		if client.wait_for_exit(30) != 0:
			raise AssertionError("客户端未正常退出")
	finally:
		client.stop()

	if env.path("videos", f"{name}.mkv").exists():
		raise AssertionError("音频转换失败仍提交了最终成片")
	if env.path("videos", f"{name}.mkv.recording").exists():
		raise AssertionError("音频转换失败遗留临时输出")

	client = start_demo_client(env, config)
	try:
		client.command(f'play "{demo}"')
		client.wait_for(lambda line: "demo_player" in line and "loading demo" in line.lower(), "demo 开始播放", 60)
		client.command(f"start_video {name} 2 4")
		client.wait_for(lambda line: "starting at segment 1/1" in line, "失败段从头重试", 60)
		client.wait_for(lambda line: "Concatenated" in line and f"{name}.mkv" in line, "重试导出完成", 180)
		client.command("quit")
		if client.wait_for_exit(30) != 0:
			raise AssertionError("客户端未正常退出")
	finally:
		client.stop()

	assert_video(env.path("videos", f"{name}.mkv"), 2)


def probe_video_dimensions(video: Path) -> tuple[int, int]:
	"""读取成片的像素尺寸，用于验证输出分辨率（离屏超采样导出）。"""
	ffprobe = shutil.which("ffprobe")
	if ffprobe is None:
		raise SkipTest("ffprobe 不可用，无法校验输出尺寸")
	result = subprocess.run(
		[ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height", "-of", "csv=p=0", str(video)],
		capture_output=True,
		text=True,
		check=False,
	)
	if result.returncode != 0:
		raise AssertionError(f"ffprobe 无法读取 {video} 的尺寸: {result.stderr.strip()}")
	width_text, height_text = result.stdout.strip().split(",", 1)
	return int(width_text), int(height_text)


def scenario_video_export_offscreen_fsaa(env: ProcessEnvironment) -> None:
	"""导出专用 FSAA：qm_video_fsaa 4 时整段录制画进离屏 MSAA 目标，全程不回退地完整出片。

	预写隔离存储的 settings.cfg 强制 OpenGL 后端：gfx_backend 必须在图形初始化前生效，
	命令行参数做不到；GL 下离屏多采样上限来自 GL_MAX_SAMPLES（与窗口 FSAA 解耦），
	全局 gfx_fsaa_samples 保持 0 也能按 4x 离屏采样。同时验证 PBO 异步读回
	（一帧延迟 + 收尾补交）在 GL 后端的完整生命周期：帧数断言与窗口路径同刻度。
	"""
	demo = demo_path()
	if demo is None:
		raise SkipTest("缺少 demo：设置 QM_E2E_DEMO 或提供测试 demo")
	name = "qme2e_offscreen_fsaa"
	# 命令行配置在图形初始化后才执行，后端选择必须写进启动前读取的设置文件。
	(env.temp_dir / "qmclient").mkdir(parents=True, exist_ok=True)
	(env.temp_dir / "qmclient" / "settings.cfg").write_text('gfx_backend "OpenGL"\n', encoding="utf-8")
	client = start_demo_client(
		env,
		[
			"gfx_fsaa_samples 0",
			"qm_video_fsaa 4",
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
		client.wait_for(lambda line: "demo_player" in line and "loading demo" in line.lower(), "demo 开始播放", 60)
		client.command(f"start_video {name} {RANGE_START_SECONDS} {RANGE_END_SECONDS}")
		# 离屏导出启动日志（目标尺寸与采样数）必须出现，证明没有静默回退到读窗口帧缓冲。
		client.wait_for(lambda line: "Offscreen export: rendering" in line, "离屏导出已启动", 60)
		client.wait_for(lambda line: "Concatenated" in line and f"{name}.mp4" in line, "离屏导出完成", 180)
		client.command("quit")
		if client.wait_for_exit(30) != 0:
			raise AssertionError("客户端未正常退出")
	finally:
		client.stop()

	video = env.path("videos", f"{name}.mp4")
	assert_video(video, RANGE_END_SECONDS - RANGE_START_SECONDS)
	log_text = env.path("client.log").read_text(encoding="utf-8", errors="replace")
	if "capturing from the window framebuffer" in log_text:
		raise AssertionError("离屏导出中途静默回退到读窗口帧缓冲")
	# GL 后端的离屏多采样与窗口 FSAA 解耦：窗口关 AA 也必须按 4x 采样启动。
	if "Offscreen export: rendering 640x360 (4x multisampling)" not in log_text:
		raise AssertionError("离屏导出没有按 4x 多采样启动（GL_MAX_SAMPLES 能力或后端选择失败）")
	if "gfx/opengl" not in log_text:
		raise AssertionError("客户端没有运行在 OpenGL 后端，settings.cfg 预写未生效")
	# PBO 异步读回按「上一帧结算 + 收尾补交」推进：帧数必须与窗口路径同刻度
	# （2s @ 60fps ≈ 118-122 帧），一帧延迟的生命周期错一会直接反映在帧数上。
	streams = probe_streams(video)
	video_streams = [stream for stream in streams if stream.get("codec_type") == "video"]
	if not video_streams:
		raise AssertionError(f"成片没有视频流: {streams}")
	nb_frames = video_streams[0].get("nb_frames")
	if nb_frames not in (None, "N/A"):
		if not 118 <= int(nb_frames) <= 122:
			raise AssertionError(f"离屏导出帧数 {nb_frames} 偏离 2s@60fps 预期（异步读回生命周期缺陷）")


def scenario_video_export_upscale_output(env: ProcessEnvironment) -> None:
	"""输出大于窗口：按输出尺寸离屏渲染（真分辨率导出），成片尺寸就是输出尺寸。

	旧行为是把大于窗口的输出回退到窗口分辨率；离屏化后 960x540 输出必须原样产出。
	"""
	demo = demo_path()
	if demo is None:
		raise SkipTest("缺少 demo：设置 QM_E2E_DEMO 或提供测试 demo")
	name = "qme2e_upscale"
	upscale_width = OUTPUT_WIDTH * 3  # 960 > 窗口宽 640
	upscale_height = OUTPUT_HEIGHT * 3  # 540 > 窗口高 360
	client = start_demo_client(
		env,
		[
			"qm_video_fsaa -1",
			"demo_speed 8",
			"qm_video_segment_seconds 0",
			"qm_video_encoder libx264",
			"qm_video_container mp4",
			f"qm_video_width {upscale_width}",
			f"qm_video_height {upscale_height}",
		],
	)
	try:
		client.command(f'play "{demo}"')
		client.wait_for(lambda line: "demo_player" in line and "loading demo" in line.lower(), "demo 开始播放", 60)
		client.command(f"start_video {name} {RANGE_START_SECONDS} {RANGE_END_SECONDS}")
		client.wait_for(lambda line: "Offscreen export: rendering" in line, "离屏导出已启动", 60)
		client.wait_for(lambda line: "Concatenated" in line and f"{name}.mp4" in line, "超采样导出完成", 300)
		client.command("quit")
		if client.wait_for_exit(30) != 0:
			raise AssertionError("客户端未正常退出")
	finally:
		client.stop()

	video = env.path("videos", f"{name}.mp4")
	assert_video(video, RANGE_END_SECONDS - RANGE_START_SECONDS)
	width, height = probe_video_dimensions(video)
	if (width, height) != (upscale_width, upscale_height):
		raise AssertionError(f"成片尺寸 {width}x{height} 不是输出尺寸 {upscale_width}x{upscale_height}（超采样导出被回退）")


E2E_TESTS = {
	"video_export_failed_start_preserves_output": scenario_video_export_failed_start_preserves_output,
	"video_export_noninteger_fps_audio_sync": scenario_video_export_noninteger_fps_audio_sync,
	"video_export_display_snapshot_and_resume": scenario_video_export_display_snapshot_and_resume,
	"video_export_audio_failure_keeps_segment_pending": scenario_video_export_audio_failure_keeps_segment_pending,
	"video_export_offscreen_fsaa": scenario_video_export_offscreen_fsaa,
	"video_export_range": scenario_video_export_range,
	"video_export_segmented_range": scenario_video_export_segmented_range,
	"video_export_segmented_audio_sync": scenario_video_export_segmented_audio_sync,
	"video_export_resume_truncated_segment": scenario_video_export_resume_truncated_segment,
	"video_export_range_rejection_and_restart": scenario_video_export_range_rejection_and_restart,
	"video_export_upscale_output": scenario_video_export_upscale_output,
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
		try:
			test(env)
			reports = list(env.path("dumps").rglob("*hang*"))
			if reports:
				raise AssertionError(f"客户端产生无响应诊断，产物通过不代表场景通过: {reports}")
		except SkipTest as exc:
			print(f"{name}: SKIPPED ({exc})")
		except Exception as exc:  # pylint: disable=broad-exception-caught
			failed += 1
			print(f"{name}: FAILED\n{exc}\nartifacts: {env.temp_dir}", file=sys.stderr)
			for label, tail in env.process_tails():
				print(f"--- {label} tail ---", file=sys.stderr)
				for line in tail:
					print(line.rstrip(), file=sys.stderr)
		else:
			print(f"{name}: passed")
		finally:
			# 成功场景同样保留日志、成片与诊断，避免清理掉间歇性无响应的证据。
			env.close(keep_temp=True)
			print(f"artifacts: {env.temp_dir}")
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
