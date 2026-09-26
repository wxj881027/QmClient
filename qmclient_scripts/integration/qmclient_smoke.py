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
	from qmclient_scripts.integration.process_harness import (
		ProcessEnvironment as SmokeEnvironment,
		_format_exit_code,
		log_message,
	)
except ModuleNotFoundError:
	from process_harness import (  # type: ignore[no-redef]
		ProcessEnvironment as SmokeEnvironment,
		_format_exit_code,
		log_message,
	)

__all__ = ["SMOKE_TESTS", "SmokeEnvironment", "_format_exit_code", "log_message"]


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
