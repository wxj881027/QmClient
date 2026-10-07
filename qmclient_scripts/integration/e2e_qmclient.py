#!/usr/bin/env python3
"""QmClient 真实进程端到端场景。

每个场景使用独立的 ProcessEnvironment，验证玩家路径产生的日志和落盘产物。
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import sys
import time
from collections.abc import Callable

try:
	from qmclient_scripts.integration.process_harness import ProcessEnvironment
except ModuleNotFoundError:
	from process_harness import ProcessEnvironment  # type: ignore[no-redef]


# 与 src/engine/client/client.cpp 的 gs_HangTimeoutSeconds 保持一致：
# 心跳停滞超过该阈值时 hang 看门狗会写出报告并拉起第二个报告进程。
HANG_WATCHDOG_TIMEOUT = 10.0


def _wait_for_file(path: Path, description: str, timeout: float = 10.0) -> Path:
	deadline = time.monotonic() + timeout
	while time.monotonic() < deadline:
		if path.is_file():
			return path
		time.sleep(0.1)
	raise TimeoutError(f"timed out waiting for {description}: {path}")


def _wait_for_perf_log(env: ProcessEnvironment, timeout: float = 15.0) -> Path:
	deadline = time.monotonic() + timeout
	pattern = "dumps/QmClient_Perf/qm_perf_*.log"
	while time.monotonic() < deadline:
		for path in env.temp_dir.glob(pattern):
			if path.is_file() and path.stat().st_size > 0 and '"stage":' in path.read_text(encoding="utf-8", errors="replace"):
				return path
		time.sleep(0.1)
	raise TimeoutError(f"timed out waiting for performance log with a stage field: {env.path(pattern)}")


def _quit_client(env: ProcessEnvironment) -> None:
	env.client.command("quit")
	code = env.client.wait_for_exit(15)
	if code != 0:
		raise RuntimeError(f"client exited with {code}")


def _wait_for_hang_report(env: ProcessEnvironment) -> Path:
	dump_dir = env.path("dumps", "QmClient_Crash")
	deadline = time.monotonic() + HANG_WATCHDOG_TIMEOUT + 20.0
	while time.monotonic() < deadline:
		reports = sorted(dump_dir.glob("*hang_report_*.txt"))
		if reports and "Report type: hang" in reports[0].read_text(encoding="utf-8", errors="replace"):
			return reports[0]
		if not env.client.is_alive():
			raise AssertionError("client exited before the hang watchdog reported the injected stall")
		time.sleep(0.25)
	raise AssertionError("hang watchdog did not report the injected main thread stall")


def scenario_online_replay_without_source(env: ProcessEnvironment) -> None:
	"""未加载回放时的查看请求可重复调用，连接和真实聊天仍可用。"""
	env.start_server()
	env.connect_client(["qm_echo_merge_window_ms 0"])
	# 默认语言在初始化时根据系统选择，连接后明确切回英文再断言反馈。
	env.client.command('cl_languagefile ""')
	for index in range(2):
		marker = f"rank1_missing_source_request_processed_{index}"
		env.client.command("qm_rank_ghost_view")
		env.client.command(f"echo {marker}")
		env.client.wait_for(lambda line: marker in line, "view command processed", 10)
	env.client.wait_for(lambda line: "load a ghost first" in line, "missing replay feedback", 10)
	env.client.command("qm_rank_ghost_off")
	env.client.command("say rank1_connection_still_alive")
	env.server.wait_for(lambda line: "rank1_connection_still_alive" in line, "live chat reached server", 15)
	_quit_client(env)


def scenario_demo_recording(env: ProcessEnvironment) -> None:
	"""连接、切图、录制、加 marker、停止并校验 demo 文件。"""
	env.start_server()
	env.connect_client(["cl_auto_demo_record 1"])
	# Tutorial 是当前构建的默认地图，先切到另一张内置地图，确保后续
	# `sv_map Tutorial` 产生可观测的真实切图事件，而不是复用初始状态。
	env.server.command("sv_map ctf1")
	# 地图加载提示使用 ADDINFO 级别，默认 stdout logger 不会输出；自动录像
	# 的文件名包含当前地图，是同一真实加载路径的稳定外部观测点。
	env.client.wait_for(lambda line: "Recording to 'demos/auto/ctf1_" in line, "ctf1 map load", 15)
	env.server.command("sv_map Tutorial")
	env.client.wait_for(lambda line: "Recording to 'demos/auto/Tutorial_" in line, "Tutorial map load", 15)

	env.client.command("record e2e_demo")
	env.client.wait_for(lambda line: "Recording to 'demos/e2e_demo.demo'" in line, "demo recording start", 10)
	# Marker 至少需要一个已写入的 demo tick；等待一小段真实游戏时间再发命令。
	time.sleep(1.2)
	env.client.command("add_demomarker")
	env.client.wait_for(lambda line: line.endswith(": Added timeline marker"), "demo timeline marker", 10)
	env.client.command("stoprecord")
	env.client.wait_for(lambda line: "Stopped recording to 'demos/e2e_demo.demo'" in line, "demo recording stop", 10)

	demo_path = _wait_for_file(env.path("demos", "e2e_demo.demo"), "demo output")
	if demo_path.stat().st_size <= 0:
		raise RuntimeError(f"demo output is empty: {demo_path}")
	_quit_client(env)


def scenario_qm_lifecycle_persistence(env: ProcessEnvironment) -> None:
	"""验证 Qm 生命周期 marker、client id 和 statistics JSON 的真实落盘。"""
	env.start_server()
	env.connect_client([])
	# OnInit 会先创建 client id/statistics，OnUpdate 会写入生命周期 marker。
	marker_path = _wait_for_file(env.path("qmclient", "lifecycle_pending.marker"), "lifecycle marker", 15)
	client_id_path = _wait_for_file(env.path("qmclient", "playtime_client_id.txt"), "playtime client id", 15)
	statistics_path = _wait_for_file(env.path("qmclient", "statistics.json"), "statistics JSON", 15)

	_quit_client(env)

	marker = marker_path.read_text(encoding="utf-8")
	for key in ("session=", "started_at=", "last_seen_at=", "client_id="):
		if key not in marker:
			raise AssertionError(f"lifecycle marker is missing {key}: {marker!r}")

	client_id = client_id_path.read_text(encoding="utf-8").strip()
	if len(client_id) != 34 or not client_id.startswith("qm") or any(char not in "0123456789abcdef" for char in client_id[2:]):
		raise AssertionError(f"invalid playtime client id: {client_id!r}")

	try:
		json.loads(statistics_path.read_text(encoding="utf-8"))
	except json.JSONDecodeError as exc:
		raise AssertionError(f"statistics JSON is invalid: {statistics_path}") from exc


def scenario_startup_saved_favorites(env: ProcessEnvironment) -> None:
	"""验证启动时好友和社区收藏配置被应用，而不是作为未知命令原样保留。"""
	settings_path = env.path("qmclient", "settings.cfg")
	settings_path.parent.mkdir(parents=True, exist_ok=True)
	settings_path.write_text(
		'stdout_output_level 1\nqm_steam_auto_launch 0\nadd_favorite "127.0.0.1:8303"\nadd_favorite_community "ddnet"\nadd_friend "Startup Friend" "Clan" "Friends"\n',
		encoding="utf-8",
	)

	env.start_client([], connect=False)
	env.client.wait_for(lambda line: "adding 127.0.0.1:8303 to favorites" in line, "saved favorite load", 15)
	env.client.command("friends")
	env.client.wait_for(lambda line: "Name: Startup Friend, Clan: Clan" in line, "saved friend load", 15)
	env.client.command('remove_friend "Startup Friend" "Clan"')
	env.client.command('remove_favorite_community "ddnet"')
	env.client.command("cl_save_settings 1")
	_quit_client(env)

	saved_config = settings_path.read_text(encoding="utf-8")
	if 'add_favorite_community "ddnet"' in saved_config:
		raise AssertionError("removed community favorite was not applied before config save")
	if 'add_friend "Startup Friend"' in saved_config:
		raise AssertionError("removed friend was not applied before config save")


def scenario_invalid_statistics_preserved(env: ProcessEnvironment) -> None:
	"""验证损坏的 statistics 文件会被拒绝覆盖并保留原始字节。"""
	invalid_bytes = b'{"local": [broken\n'
	statistics_path = env.path("qmclient", "statistics.json")
	statistics_path.parent.mkdir(parents=True, exist_ok=True)
	statistics_path.write_bytes(invalid_bytes)

	env.start_client([], connect=False)
	env.client.wait_for(
		lambda line: "statistics file is invalid and will be preserved" in line,
		"invalid statistics warning",
		15,
	)
	_quit_client(env)
	if statistics_path.read_bytes() != invalid_bytes:
		raise AssertionError("invalid statistics file was modified")


def scenario_perf_log_persistence(env: ProcessEnvironment) -> None:
	"""验证开启性能调试后真实进程写出带 stage 字段的日志。"""
	env.start_client(["qm_perf_debug 1", "qm_perf_logfile 1"], connect=False)
	env.client.wait_for(lambda line: "writing performance log to" in line, "performance log setup", 15)
	perf_path = _wait_for_perf_log(env)
	_quit_client(env)
	if perf_path.stat().st_size <= 0:
		raise RuntimeError(f"performance log is empty: {perf_path}")


def scenario_vector_font_and_icon_resources(env: ProcessEnvironment) -> None:
	"""验证自定义字体和 Phosphor 图标资源在真实进程中加载。"""
	env.start_server()
	env.connect_client(["qm_ui_icon_weight 1"])
	# 打开真实设置页，确保 UI 图标绘制路径实际运行，而不是只验证资源文件存在。
	env.client.command("ui_page 16")
	time.sleep(2.0)
	for style in ("Regular", "Bold", "Light", "Fill"):
		font = env.build_dir / "data" / "fonts" / "Phosphor" / f"Phosphor-{style}.ttf"
		if not font.is_file():
			raise AssertionError(f"Bundled Phosphor font is missing: {font}")
	for line in env.client._lines:
		if "Bundled 'Phosphor' icon face is unavailable" in line:
			raise AssertionError("the bundled Phosphor icon face was not available")
		if "Failed to open/read font file 'fonts/" in line:
			raise AssertionError(f"bundled font path was malformed: {line}")
	_quit_client(env)


def scenario_legacy_icon_font_residual_ignored(env: ProcessEnvironment) -> None:
	"""用户 fonts 下的 Phosphor 副本不应进入字体 face 池。"""
	legacy_dir = env.path("fonts", "Phosphor")
	legacy_dir.mkdir(parents=True, exist_ok=True)
	shutil.copy2(
		env.build_dir / "data" / "fonts" / "Phosphor" / "Phosphor-Regular.ttf",
		legacy_dir / "Phosphor-Regular.ttf",
	)

	env.start_client([], connect=False)
	env.client.wait_for(
		lambda line: "Ignoring user copy of bundled icon font 'fonts/Phosphor/Phosphor-Regular.ttf'" in line,
		"legacy icon font residual rejection",
		15,
	)
	if any("Loaded" in line and "fonts/Phosphor/" in line for line in env.client._lines):
		raise AssertionError("legacy Phosphor residual was loaded into the font face pool")
	_quit_client(env)


def scenario_connection_failure_recovery(env: ProcessEnvironment) -> None:
	"""验证服务端断开后客户端报告离线并可正常退出。"""
	port = env.start_server()
	env.start_client([], connect=True, connect_address=f"localhost:{port}")
	env.server.wait_for(lambda line: line.startswith("server: player has entered the game"), "client connection", 15)
	env.server.command("shutdown")
	env.client.wait_for(lambda line: "offline error='" in line, "connection failure fallback", 15)
	_quit_client(env)


def scenario_recording_without_connection(env: ProcessEnvironment) -> None:
	"""验证未连接时录制命令走错误回退而不触发崩溃。"""
	env.start_client([], connect=False)
	env.client.command("record e2e_unloaded")
	env.client.wait_for(lambda line: line.endswith(": Client is not online."), "recording error fallback", 10)
	_quit_client(env)


def scenario_assert_dialog_no_false_hang(env: ProcessEnvironment) -> None:
	"""验证进程内弹窗阻塞主线程时看门狗不会误报“客户端卡死”。

	`--qm-test-main-thread-assert` 让真实客户端在主循环之前触发一次主线程断言，
	等价于启动阶段的网络/图形初始化错误弹窗：主线程停在模态弹窗里，心跳停滞。
	`QMCLIENT_TEST_HIDE_DIALOG` 只隐藏窗口，避免桌面上的点击提前关闭弹窗干扰断言，
	弹窗仍会正常创建并运行消息循环。修复前看门狗会在 10 秒后写出 hang 报告与转储
	并拉起第二个报告进程；这里断言弹窗阻塞期间不产生 hang 产物，且进程保持存活。
	"""
	env.start_client(["--qm-test-main-thread-assert"], connect=False, env={"QMCLIENT_TEST_HIDE_DIALOG": "1"})
	env.client.wait_for(lambda line: "qm test main thread assertion" in line, "injected main thread assertion", 30)
	time.sleep(HANG_WATCHDOG_TIMEOUT + 3.0)
	dump_dir = env.path("dumps", "QmClient_Crash")
	hang_artifacts = sorted(dump_dir.glob("*hang_report_*.txt")) + sorted(dump_dir.glob("*hang_dump_*.dmp"))
	if hang_artifacts:
		raise AssertionError(f"hang watchdog reported while the modal assert dialog was open: {hang_artifacts}")
	if not env.client.is_alive():
		raise AssertionError("client exited while the modal assert dialog was open")
	# 弹窗阻塞主线程，控制台命令无法送达，直接结束测试进程。
	env.client.kill()


def scenario_hang_watchdog_reports_stall(env: ProcessEnvironment) -> None:
	"""验证主线程真实卡死时 hang 看门狗会写出报告（单调时钟的正向回归）。

	`--qm-test-main-thread-stall` 让客户端在主循环内阻塞 12 秒（超过 10 秒阈值）。
	看门狗必须使用单调时钟在主线程阻塞期间写出 hang 报告；时钟若退回主循环
	tick 缓存（修复前的行为），报告永远不会出现，本场景失败。
	"""
	env.start_client(["--qm-test-main-thread-stall"], connect=False, env={"QMCLIENT_TEST_HIDE_DIALOG": "1"})
	_wait_for_hang_report(env)

	# 主线程仍处于注入的阻塞中，直接结束进程、不校验退出码：卡死后退出清理里
	# NVIDIA ICD（nvoglv64.dll）在销毁阶段可能访问违例，属于项目按已知驱动故障
	# 忽略的范畴（crashdump_mark_shutdown_begin），与被测的看门狗行为无关。
	# 报告进程已被 QMCLIENT_TEST_HIDE_DIALOG 抑制，不会遗留后台进程。
	env.client.kill()


def scenario_slow_asset_loading_no_false_hang(env: ProcessEnvironment) -> None:
	"""逐项加载累计超过十秒仍可完成启动，且不产生卡死报告。"""
	env.start_client(
		["qm_steam_auto_launch 0"],
		connect=False,
		env={"QMCLIENT_TEST_ASSET_LOAD_DELAY_MS": "1000", "QMCLIENT_TEST_HIDE_DIALOG": "1"},
		startup_timeout=None,
	)
	env.client.wait_for(lambda line: line == "test/loading: assets_begin delay_ms=1000", "slow asset loading start", 30)
	prefix = "test/loading: assets_complete elapsed_ms="
	line = env.client.wait_for(lambda value: value.startswith(prefix), "slow asset loading completion", 60)
	if int(line.removeprefix(prefix)) <= HANG_WATCHDOG_TIMEOUT * 1000:
		raise AssertionError(f"asset loading did not exceed the watchdog threshold: {line}")
	env.client.wait_for(lambda line: line.startswith("client: version"), "client startup after slow assets", 30)
	env.client.command("echo slow_asset_loading_responsive")
	env.client.wait_for(lambda line: line.endswith(": slow_asset_loading_responsive"), "client command after slow assets", 10)
	_quit_client(env)
	dump_dir = env.path("dumps", "QmClient_Crash")
	hang_artifacts = sorted(dump_dir.glob("*hang_report_*.txt")) + sorted(dump_dir.glob("*hang_dump_*.dmp"))
	if hang_artifacts:
		raise AssertionError(f"hang watchdog reported while asset loading was progressing: {hang_artifacts}")


def scenario_asset_loading_stall_reports_hang(env: ProcessEnvironment) -> None:
	"""启动资源加载中单次停滞超过十秒，仍然生成卡死报告。"""
	env.start_client(
		["qm_steam_auto_launch 0"],
		connect=False,
		env={"QMCLIENT_TEST_ASSET_LOAD_DELAY_MS": "12000", "QMCLIENT_TEST_HIDE_DIALOG": "1"},
		startup_timeout=None,
	)
	env.client.wait_for(lambda line: line == "test/loading: assets_begin delay_ms=12000", "stalled asset loading start", 30)
	_wait_for_hang_report(env)
	if any("test/loading: assets_complete" in line for line in env.client._lines):
		raise AssertionError("hang report was not produced during asset loading")
	env.client.kill()


E2E_TESTS: dict[str, Callable[[ProcessEnvironment], None]] = {
	"assert_dialog_no_false_hang": scenario_assert_dialog_no_false_hang,
	"asset_loading_stall_reports_hang": scenario_asset_loading_stall_reports_hang,
	"hang_watchdog_reports_stall": scenario_hang_watchdog_reports_stall,
	"connection_failure_recovery": scenario_connection_failure_recovery,
	"demo_recording": scenario_demo_recording,
	"online_replay_without_source": scenario_online_replay_without_source,
	"invalid_statistics_preserved": scenario_invalid_statistics_preserved,
	"perf_log_persistence": scenario_perf_log_persistence,
	"qm_lifecycle_persistence": scenario_qm_lifecycle_persistence,
	"recording_without_connection": scenario_recording_without_connection,
	"slow_asset_loading_no_false_hang": scenario_slow_asset_loading_no_false_hang,
	"startup_saved_favorites": scenario_startup_saved_favorites,
	"vector_font_and_icon_resources": scenario_vector_font_and_icon_resources,
	"legacy_icon_font_residual_ignored": scenario_legacy_icon_font_residual_ignored,
}


def main() -> int:
	parser = argparse.ArgumentParser(description="Run QmClient process end-to-end tests")
	parser.add_argument("build_dir", type=Path)
	parser.add_argument("test", choices=sorted(E2E_TESTS), nargs="?")
	args = parser.parse_args()

	tests = {args.test: E2E_TESTS[args.test]} if args.test else E2E_TESTS
	failed = 0
	for name, test in tests.items():
		env = ProcessEnvironment(args.build_dir, temp_prefix="qmclient_e2e_")
		keep_temp = False
		try:
			test(env)
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
	raise SystemExit(main())
