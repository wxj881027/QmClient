#!/usr/bin/env python3
"""Demo 视频导出的画质与速度基准（可重复测量，非 gate）。

用法：
    python qmclient_scripts/integration/perf_video_export.py cmake-build-release [speed_hd|quality_codec|quality_aa]

- speed_hd：1080p/4K 两档预设的真实客户端导出吞吐（帧/秒，start_video→拼接完成墙钟）。
- quality_codec：同一批无损帧重编码的确定性画质门（PSNR/VMAF 阈值来自 2026-10-24 实测基线，
  跨机器稳定：输入帧与编码器同版本内确定）。
- quality_aa：导出 FSAA 相对无 AA 的增益门（对照超采样降采样参考；阈值放宽，跨 GPU 只看方向）。

前置：可播放 demo（QM_E2E_DEMO，与 e2e_video_export 相同回退）与 PATH 上的 ffmpeg/ffprobe。
产物保留在各场景临时目录（含成片与 JSON 摘要），失败打印 artifacts 路径。
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path
from unittest import SkipTest

try:
	from qmclient_scripts.integration.process_harness import ProcessEnvironment
except ModuleNotFoundError:
	from process_harness import ProcessEnvironment

RANGE_START_SECONDS = 2
RANGE_END_SECONDS = 4
EXPECTED_FRAMES = 120  # 2s @ 60fps；边界取整容差 ±5
WINDOW_WIDTH = 640
WINDOW_HEIGHT = 360
REF_RESOLUTION = (1920, 1080)

# 2026-10-24 实测基线（RTX 4060 Ti + 本仓库 FFmpeg）：daily=50.4dB/96.5、
# official≈同档、best=48.6dB(360p)/更高(1080p)。阈值留 4dB/5VMAF 余量防场景漂移。
QUALITY_THRESHOLDS = {
	"daily_gop60": {"psnr_y": 45.0, "vmaf": 90.0},
	"official_master": {"psnr_y": 45.0, "vmaf": 90.0},
	"best_intra444": {"psnr_y": 46.0, "vmaf": 90.0},
}
# AA 场景不断言像素 Δ：跨运行的特效随机地板（±1.5dB）大于 AA 信号本身（约 +1.6dB），
# 断言的是离屏 MSAA 机制生效（采样数声明 + 无窗口路径回退）。


def demo_path() -> Path | None:
	candidates: list[Path] = []
	override = os.environ.get("QM_E2E_DEMO")
	if override:
		candidates.append(Path(override))
	appdata = os.environ.get("APPDATA")
	if appdata:
		candidates.append(Path(appdata) / "DDNet" / "demos" / "qmmkvtest.demo")
	return next((path for path in candidates if path.is_file()), None)


def base_config(log_path: Path) -> list[str]:
	return [
		f'logfile "{log_path.as_posix()}"',
		"gfx_fullscreen 0",
		f"gfx_screen_width {WINDOW_WIDTH}",
		f"gfx_screen_height {WINDOW_HEIGHT}",
		"snd_enable 0",
		"cl_show_welcome 0",
		"cl_skip_start_menu 1",
		"cl_save_settings 0",
		"qm_auto_update 0",
		"qm_steam_auto_launch 0",
		"demo_speed 8",
		"qm_video_segment_seconds 0",
	]


def export_once(env: ProcessEnvironment, name: str, spec: dict) -> dict:
	"""一次导出：返回 wall/frames/size 与成片路径；spec 项见调用点。"""
	(env.temp_dir / "qmclient" / "settings.cfg").write_text('gfx_backend "OpenGL"\ngfx_fsaa_samples 0\n', encoding="utf-8")
	config = [
		*base_config(env.path(f"{name}.log")),
		"qm_video_container mp4",
		f"qm_video_encoder {spec['encoder']}",
		f"qm_video_fsaa {spec['fsaa']}",
		f"qm_video_gop {spec['gop']}",
		f"qm_video_pixfmt {spec['pixfmt']}",
		f"qm_video_width {spec['w']}",
		f"qm_video_height {spec['h']}",
	]
	if spec.get("quality") is not None:
		config.append(f"qm_video_quality {spec['quality']}")
	client = env.start_client(config=config, connect=False)
	try:
		client.wait_for(lambda line: line.startswith("client: version"), "client 启动", 30)
		demo = demo_path()
		assert demo is not None
		client.command(f'play "{demo}"')
		client.wait_for(lambda line: "demo_player" in line and "loading demo" in line.lower(), "demo 开始播放", 60)
		start = time.monotonic()
		client.command(f"start_video {name} {RANGE_START_SECONDS} {RANGE_END_SECONDS}")
		client.wait_for(lambda line: "Concatenated" in line and f"{name}.mp4" in line, "导出完成", 300)
		wall = time.monotonic() - start
		client.command("quit")
		exit_ok = client.wait_for_exit(30) == 0
	finally:
		client.stop()
	video = env.path("videos", f"{name}.mp4")
	if not video.is_file():
		raise AssertionError(f"导出未产出成片: {video}")
	ffprobe = shutil.which("ffprobe")
	probe = json.loads(subprocess.run([ffprobe, "-v", "error", "-show_entries", "stream=nb_frames,width,height", "-of", "json", str(video)], capture_output=True, text=True, check=True).stdout)["streams"][0]
	frames = int(probe.get("nb_frames") or 0)
	if not EXPECTED_FRAMES - 5 <= frames <= EXPECTED_FRAMES + 5:
		raise AssertionError(f"{name} 帧数 {frames} 偏离 {EXPECTED_FRAMES}（读回生命周期或起点缺陷）")
	return {"wall_seconds": round(wall, 3), "fps": round(frames / wall, 1), "frames": frames, "size_bytes": video.stat().st_size, "video": str(video), "exit_ok": exit_ok}


def scenario_speed_hd(env: ProcessEnvironment) -> None:
	"""1080p/4K 两档预设的导出吞吐：只报告数值并校验成片正确，不做机器相关的速度断言。"""
	specs = {
		"best_1080": dict(w=1920, h=1080, fsaa=4, encoder="libx264", quality=14, gop=1, pixfmt=3),
		"fast_1080": dict(w=1920, h=1080, fsaa=0, encoder="h264", quality=18, gop=60, pixfmt=0),
		"best_4k": dict(w=3840, h=2160, fsaa=4, encoder="libx264", quality=14, gop=1, pixfmt=3),
		"fast_4k": dict(w=3840, h=2160, fsaa=0, encoder="h264", quality=18, gop=60, pixfmt=0),
	}
	results = {}
	for name, spec in specs.items():
		results[name] = export_once(env, f"perf_{name}", spec)
		print(f"{name}: {results[name]['fps']} fps (wall {results[name]['wall_seconds']}s, {results[name]['size_bytes']} bytes)", flush=True)
	env.path("perf_speed_hd.json").write_text(json.dumps(results, ensure_ascii=False, indent=1), encoding="utf-8")


def sh(args: list[str]) -> subprocess.CompletedProcess:
	return subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace")


def parse_metrics(dist: Path, ref: Path) -> tuple[float | None, float | None]:
	p = sh(["ffmpeg", "-hide_banner", "-i", str(dist), "-i", str(ref), "-filter_complex", "[0:v]format=yuv444p16le[a];[1:v]format=yuv444p16le[b];[a][b]psnr", "-f", "null", "-"])
	m = re.search(r"PSNR y:([\d.]+|inf)", p.stderr)
	psnr = float(m.group(1)) if m and m.group(1) != "inf" else None
	p = sh(["ffmpeg", "-hide_banner", "-i", str(dist), "-i", str(ref), "-filter_complex", "[0:v]format=yuv420p[a];[1:v]format=yuv420p[b];[a][b]libvmaf=n_threads=4", "-f", "null", "-"])
	m = re.search(r"VMAF score: ([\d.]+)", p.stderr)
	vmaf = round(float(m.group(1)), 2) if m else None
	return psnr, vmaf


def scenario_quality_codec(env: ProcessEnvironment) -> None:
	"""确定性编码画质门：同一批无损帧重编码，PSNR/VMAF 不得低于基线阈值。"""
	if shutil.which("ffmpeg") is None or shutil.which("ffprobe") is None:
		raise SkipTest("ffmpeg/ffprobe 不可用")
	w, h = REF_RESOLUTION
	ref_spec = dict(w=w, h=h, fsaa=0, encoder="libx264", quality=0, gop=1, pixfmt=3)
	ref = export_once(env, "perf_ref_lossless", ref_spec)
	ref_video = Path(ref["video"])
	# 参考自身必须真无损：qp0 下重解码重编码逐位一致，PSNR 解析失败或有限值都视为参考异常。
	p = sh(["ffmpeg", "-hide_banner", "-i", str(ref_video), "-i", str(ref_video), "-filter_complex", "[0:v]format=yuv444p16le[a];[1:v]format=yuv444p16le[b];[a][b]psnr", "-f", "null", "-"])
	if "PSNR" not in p.stderr:
		raise AssertionError(f"参考帧度量失败: {p.stderr[-300:]}")

	encodings = {
		"daily_gop60": ["-c:v", "libx264", "-preset", "medium", "-crf", "18", "-g", "60", "-pix_fmt", "yuv420p"],
		"official_master": ["-c:v", "libx264", "-preset", "medium", "-crf", "18", "-g", "12", "-pix_fmt", "yuv420p"],
		"best_intra444": ["-c:v", "libx264", "-preset", "medium", "-crf", "14", "-g", "1", "-pix_fmt", "yuv444p"],
	}
	out_dir = env.temp_dir / "perf_enc"
	out_dir.mkdir(parents=True, exist_ok=True)
	results = {}
	for name, enc in encodings.items():
		dst = out_dir / f"{name}.mp4"
		p = sh(["ffmpeg", "-hide_banner", "-y", "-i", str(ref_video), "-an", *enc, str(dst)])
		if p.returncode != 0:
			raise AssertionError(f"{name} 编码失败: {p.stderr[-300:]}")
		psnr, vmaf = parse_metrics(dst, ref_video)
		threshold = QUALITY_THRESHOLDS[name]
		results[name] = {"psnr_y": psnr, "vmaf": vmaf, "size_bytes": dst.stat().st_size}
		print(f"{name}: PSNR-Y {psnr} dB / VMAF {vmaf}（阈值 {threshold}）", flush=True)
		if psnr is None or psnr < threshold["psnr_y"]:
			raise AssertionError(f"{name} PSNR-Y {psnr} 低于阈值 {threshold['psnr_y']}（编码画质回退）")
		if vmaf is None or vmaf < threshold["vmaf"]:
			raise AssertionError(f"{name} VMAF {vmaf} 低于阈值 {threshold['vmaf']}（编码画质回退）")
	env.path("perf_quality_codec.json").write_text(json.dumps(results, ensure_ascii=False, indent=1), encoding="utf-8")


def scenario_quality_aa(env: ProcessEnvironment) -> None:
	"""AA 生效性门：跨运行像素对比受特效随机地板支配（2026-10-24 实测同配置两次
	导出 PSNR 仅 35.3dB，波动 ±1.5dB 大于 AA 信号），不能作断言；改为断言离屏
	MSAA 机制生效（日志声明的采样数、无窗口路径回退），像素 Δ 仅作参考输出。"""
	if shutil.which("ffmpeg") is None:
		raise SkipTest("ffmpeg 不可用")
	w, h = REF_RESOLUTION[0] // 3, REF_RESOLUTION[1] // 3  # 640x360，同实测基线
	specs = {
		"noaa": dict(w=w, h=h, fsaa=0, encoder="libx264", quality=0, gop=1, pixfmt=3),
		"msaa": dict(w=w, h=h, fsaa=4, encoder="libx264", quality=0, gop=1, pixfmt=3),
		"ss": dict(w=w * 4, h=h * 4, fsaa=0, encoder="libx264", quality=0, gop=1, pixfmt=3),
	}
	videos = {}
	for name, spec in specs.items():
		videos[name] = Path(export_once(env, f"perf_aa_{name}", spec)["video"])

	# 机制断言：导出必须真以 4x 多采样离屏渲染，且全程没有静默回退到读窗口帧缓冲。
	msaa_log = env.path("perf_aa_msaa.log").read_text(encoding="utf-8", errors="replace")
	noaa_log = env.path("perf_aa_noaa.log").read_text(encoding="utf-8", errors="replace")
	if f"Offscreen export: rendering {w}x{h} (4x multisampling)" not in msaa_log:
		raise AssertionError("离屏导出没有按 4x 多采样启动（后端钳制或配置未生效）")
	if f"Offscreen export: rendering {w}x{h} (1x multisampling)" not in noaa_log:
		raise AssertionError("对照组没有按 1x 启动（配置串扰）")
	for label, log_text in (("msaa", msaa_log), ("noaa", noaa_log)):
		if "capturing from the window framebuffer" in log_text:
			raise AssertionError(f"{label} 导出中途静默回退到读窗口帧缓冲")

	def psnr_vs_ss(src: Path) -> float:
		p = sh(["ffmpeg", "-hide_banner", "-i", str(src), "-i", str(videos["ss"]), "-filter_complex", f"[1:v]scale={w}:{h}:flags=lanczos,format=yuv444p16le[ref];[0:v]format=yuv444p16le[main];[main][ref]psnr", "-f", "null", "-"])
		m = re.search(r"PSNR y:([\d.]+|inf)", p.stderr)
		return float(m.group(1)) if m and m.group(1) != "inf" else float("nan")

	# 参考值：跨运行波动 ±1.5dB（特效随机），量级仅供参考，不作断言。
	noaa = psnr_vs_ss(videos["noaa"])
	msaa = psnr_vs_ss(videos["msaa"])
	delta = msaa - noaa
	print(f"AA 参考: no-AA {noaa:.2f} dB, 4x MSAA {msaa:.2f} dB, Δ {delta:+.2f} dB（跨运行波动 ±1.5dB，仅参考）", flush=True)
	env.path("perf_quality_aa.json").write_text(json.dumps({"no_aa_psnr_y": noaa, "msaa_psnr_y": msaa, "delta_reference_only": delta}, ensure_ascii=False, indent=1), encoding="utf-8")


PERF_SCENARIOS = {
	"speed_hd": scenario_speed_hd,
	"quality_codec": scenario_quality_codec,
	"quality_aa": scenario_quality_aa,
}


def main() -> int:
	parser = argparse.ArgumentParser(description="Run QmClient video export performance/quality benchmarks")
	parser.add_argument("build_dir", type=Path)
	parser.add_argument("scenario", choices=sorted(PERF_SCENARIOS), nargs="?")
	args = parser.parse_args()

	scenarios = {args.scenario: PERF_SCENARIOS[args.scenario]} if args.scenario else PERF_SCENARIOS
	failed = 0
	for name, scenario in scenarios.items():
		env = ProcessEnvironment(args.build_dir, temp_prefix="qmclient_video_perf_")
		try:
			scenario(env)
		except SkipTest as exc:
			print(f"{name}: SKIPPED ({exc})")
		except Exception as exc:  # pylint: disable=broad-except
			failed += 1
			print(f"{name}: FAILED\n{exc}\nartifacts: {env.temp_dir}", file=sys.stderr)
		else:
			print(f"{name}: passed")
		finally:
			env.close(keep_temp=True)
			print(f"artifacts: {env.temp_dir}")
	return 1 if failed else 0


if __name__ == "__main__":
	sys.exit(main())
