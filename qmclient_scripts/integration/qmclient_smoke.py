#!/usr/bin/env python3
"""QmClient 真实进程冒烟测试入口。

进程编排能力在 `process_harness.py`，本文件只保留冒烟场景自身。
"""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
from collections.abc import Callable

try:
	from qmclient_scripts.integration.e2e_serverlist import Loopback
	from qmclient_scripts.integration.process_harness import (
		ProcessEnvironment as SmokeEnvironment,
		_format_exit_code,
		log_message,
	)
except ModuleNotFoundError:
	from e2e_serverlist import Loopback  # type: ignore[no-redef]
	from process_harness import (  # type: ignore[no-redef]
		ProcessEnvironment as SmokeEnvironment,
		_format_exit_code,
		log_message,
	)

__all__ = ["SMOKE_TESTS", "SmokeEnvironment", "_format_exit_code", "log_message"]


def _smoke_media_directory_startup(env: SmokeEnvironment, directory: str) -> None:
	settings_path = env.path("qmclient", "settings.cfg")
	settings_path.parent.mkdir(parents=True, exist_ok=True)
	settings_path.write_text(
		'qm_steam_auto_launch 0\n'
		f'qm_demo_directory "{directory}"\n'
		f'qm_screenshot_directory "{directory}"\n',
		encoding="utf-8",
	)
	env.start_client([], connect=False)
	# 主循环响应命令，证明菜单等组件已经完成初始化。
	marker = "media_directory_startup_ready"
	env.client.command(f"echo {marker}")
	env.client.wait_for(lambda line: marker in line, "startup command response", 15)
	env.client.command("quit")
	code = env.client.wait_for_exit(15)
	if code != 0:
		raise RuntimeError(f"client exited with {_format_exit_code(code)}")


def smoke_default_media_directory_startup(env: SmokeEnvironment) -> None:
	"""默认目录下启动、响应命令并正常退出，不需要连接服务端。"""
	_smoke_media_directory_startup(env, "")


def smoke_custom_media_directory_startup(env: SmokeEnvironment) -> None:
	"""从隔离配置加载相对自定义目录后完成启动，避免初始化前访问存储。"""
	_smoke_media_directory_startup(env, "自定义 媒体")


def smoke_startup_console_commands(env: SmokeEnvironment) -> None:
	"""启动脚本和命令行可执行依赖游戏接口的命令。"""
	env.path("autoexec_client.cfg").write_text(
		"dummy_disconnect\ndummy_reset\ncl_replays 0\nsave_replay\n",
		encoding="utf-8",
	)
	client = env.start_client([
		"cl_show_welcome 1",
		"qm_steam_auto_launch 0",
		"poses_open qmclient-startup-missing.poses",
	], connect=False)
	client.wait_for(
		lambda line: line == "replay: Feature is disabled. Please enable it via configuration.",
		"disabled replay reported during startup",
		15,
	)
	client.wait_for(
		lambda line: line == "poses: could not open the .poses file",
		"missing pose file reported during startup",
		15,
	)
	client.command("quit")
	code = client.wait_for_exit(15)
	if code != 0:
		raise RuntimeError(f"client exited with {_format_exit_code(code)}")


def smoke_saved_custom_communities_startup(env: SmokeEnvironment) -> None:
	"""加载保存的社区 URL 后启动，使用最终值发出首次请求。"""
	http = Loopback()
	try:
		saved_path = "/saved-communities.json"
		startup_path = "/startup-communities.json"
		runtime_path = "/runtime-communities.json"
		http.set(startup_path, 200, b'{"communities":[]}')
		http.set(runtime_path, 200, b'{"communities":[]}')

		settings_path = env.path("qmclient", "settings.cfg")
		settings_path.write_text(
			f'qm_custom_communities_url "{http.url}{saved_path}"\n',
			encoding="utf-8",
		)
		client_env = {
			key: http.url
			for key in ("http_proxy", "https_proxy", "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "all_proxy")
		}
		client_env.update({"NO_PROXY": "127.0.0.1,localhost", "no_proxy": "127.0.0.1,localhost"})
		client = env.start_client([
			"qm_steam_auto_launch 0",
			"http_allow_insecure 1",
			f"qm_websocket_url ws://127.0.0.1:{http.server.server_port}/ws",
			f"qm_custom_communities_url {http.url}{startup_path}",
		], connect=False, env=client_env)
		http.wait_requests(startup_path, 1)

		client.command(f"qm_custom_communities_url {http.url}{runtime_path}")
		http.wait_requests(runtime_path, 1)
		client.command("quit")
		code = client.wait_for_exit(15)
		if code != 0:
			raise RuntimeError(f"client exited with {_format_exit_code(code)}")

		with http.condition:
			paths = [path for path, _ in http.requests if path in {saved_path, startup_path, runtime_path}]
		if paths != [startup_path, runtime_path]:
			raise AssertionError(f"unexpected custom community requests: {paths}")
	finally:
		http.close()


def smoke_plain_connection(env: SmokeEnvironment) -> None:
	env.start_server()
	env.connect_client([])


def smoke_focus_configuration(env: SmokeEnvironment) -> None:
	env.start_server()
	env.connect_client([
		"qm_focus_mode 1",
		"qm_focus_mode_hide_hud 1",
		"qm_focus_mode_hide_map_progress 0",
		"qm_focus_mode_hide_info_messages 1",
		"qm_focus_mode_hide_names 1",
		"qm_focus_mode_hide_nameplates 0",
		"qm_focus_mode_hide_direction_indicators 0",
		"qm_focus_mode_hide_guide_lines 1",
		"qm_focus_mode_hide_jump_effects 0",
		"qm_focus_mode_hide_muzzle_effects 1",
		"qm_focus_mode_mute_jump_sounds 0",
		"qm_focus_mode_hide_chat 1",
		"qm_focus_mode_hide_system_messages 0",
		"qm_focus_mode_hide_echo 1",
	])


def smoke_gores_configuration(env: SmokeEnvironment) -> None:
	env.start_server()
	env.connect_client([
		"qm_gores_auto_enable 1",
		"qm_gores 0",
		"qm_gores_fast_input 1",
		"qm_gores_fast_input_others 1",
		"qm_gores_hide_guides 1",
		"tc_fast_input 0",
		"tc_fast_input_others 0",
	])


def smoke_connection_shutdown(env: SmokeEnvironment) -> None:
	"""验证服务端主动关闭连接时客户端能观察到离线状态并退出。"""
	env.start_server()
	env.connect_client([])
	env.server.command("shutdown")
	env.client.wait_for(lambda line: line == "client: offline error='Server shutdown'", "server shutdown notification", 15)
	if env.server.wait_for_exit(15) != 0:
		raise RuntimeError(f"server exited with {_format_exit_code(env.server._process.returncode)}")
	env.client.command("quit")
	if env.client.wait_for_exit(15) != 0:
		raise RuntimeError(f"client exited with {_format_exit_code(env.client._process.returncode)}")


SMOKE_TESTS: dict[str, Callable[[SmokeEnvironment], None]] = {
	"default_media_directory_startup": smoke_default_media_directory_startup,
	"custom_media_directory_startup": smoke_custom_media_directory_startup,
	"startup_console_commands": smoke_startup_console_commands,
	"saved_custom_communities_startup": smoke_saved_custom_communities_startup,
	"plain_connection": smoke_plain_connection,
	"focus_configuration": smoke_focus_configuration,
	"gores_configuration": smoke_gores_configuration,
	"connection_shutdown": smoke_connection_shutdown,
}


def main() -> int:
	parser = argparse.ArgumentParser(description="Run QmClient process smoke tests")
	parser.add_argument("build_dir", type=Path)
	parser.add_argument("test", choices=sorted(SMOKE_TESTS), nargs="?")
	args = parser.parse_args()

	tests = {args.test: SMOKE_TESTS[args.test]} if args.test else SMOKE_TESTS
	failed = 0
	for name, test in tests.items():
		env = SmokeEnvironment(args.build_dir)
		keep_temp = False
		try:
			test(env)
		except Exception as exc:  # pylint: disable=broad-exception-caught
			failed += 1
			keep_temp = True
			print(f"{name}: FAILED\n{exc}\nartifacts: {env.temp_dir}", file=sys.stderr)
		else:
			print(f"{name}: passed")
		finally:
			env.close(keep_temp=keep_temp)
	return 1 if failed else 0


if __name__ == "__main__":
	raise SystemExit(main())
