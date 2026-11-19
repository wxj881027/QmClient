#!/usr/bin/env python3
"""验证控制台日志导出的启动、连续写入与失败恢复。"""

from __future__ import annotations

import argparse
from pathlib import Path
import socket

try:
	from qmclient_scripts.integration.process_harness import Process, ProcessEnvironment
except ModuleNotFoundError:
	from process_harness import Process, ProcessEnvironment


def _start_client(env: ProcessEnvironment, client_env: dict[str, str], *startup_commands: str) -> Process:
	return env.start_client(
		[
			"stdout_output_level 2",
			"cl_show_welcome 0",
			"qm_steam_auto_launch 0",
			"qm_auto_update 0",
			"qm_websocket_url disabled",
			*startup_commands,
		],
		connect=False,
		env=client_env,
	)


def _wait_for_marker(client: Process, marker: str) -> None:
	client.wait_for(lambda line: line == f"chatresp: No such command: {marker}.", marker, 15)


def _dumps(env: ProcessEnvironment, console: str = "local") -> list[Path]:
	return list(env.path("dumps").glob(f"{console}_console_dump_*.txt"))


def _quit(client: Process) -> None:
	client.command("quit")
	code = client.wait_for_exit(15)
	assert code == 0, f"client exited with {code} after dumping the console"


def scenario_dump_before_graphics_init(env: ProcessEnvironment, client_env: dict[str, str]) -> None:
	client = _start_client(env, client_env, "dump_local_console", "dump_remote_console")

	assert len(_dumps(env)) == 1
	assert len(_dumps(env, "remote")) == 1
	assert not list(env.path("dumps").glob("*.tmp"))
	_quit(client)


def scenario_consecutive_console_dumps(env: ProcessEnvironment, client_env: dict[str, str]) -> None:
	client = _start_client(env, client_env)
	markers = [f"qm_e2e_dump_snapshot_{index}" for index in range(4)]
	commands = [f"clear_local_console; {marker}; dump_local_console" for marker in markers]
	done = "qm_e2e_dumps_done"
	client.command("; ".join([*commands, done]))
	_wait_for_marker(client, done)

	contents = [path.read_bytes() for path in _dumps(env)]
	assert len(contents) == len(markers), f"expected {len(markers)} separate dumps, got {len(contents)}"
	for marker in markers:
		assert sum(marker.encode() in text for text in contents) == 1, f"missing or duplicate snapshot for {marker}"
	assert not list(env.path("dumps").glob("*.tmp"))
	_quit(client)


def scenario_dump_retries_after_open_failure(env: ProcessEnvironment, client_env: dict[str, str]) -> None:
	client = _start_client(env, client_env)
	dumps_path = env.path("dumps")
	dumps_backup = env.path("dumps-before-console-failure")
	dumps_path.rename(dumps_backup)
	try:
		dumps_path.touch(exist_ok=False)
		client.command("dump_local_console")
		client.wait_for(lambda line: line.startswith("console: Failed to open 'dumps/local_console_dump_"), "dump open failure", 15)
		assert dumps_path.is_file()
	finally:
		if dumps_path.is_file():
			dumps_path.unlink()
		dumps_backup.rename(dumps_path)
	marker = "qm_e2e_dump_retry"
	done = "qm_e2e_dump_retry_done"
	client.command(f"clear_local_console; {marker}; dump_local_console; {done}")
	_wait_for_marker(client, done)

	paths = _dumps(env)
	assert len(paths) == 1
	assert marker.encode() in paths[0].read_bytes()
	assert not list(dumps_path.glob("*.tmp"))
	_quit(client)


def main() -> int:
	scenarios = {
		"startup": scenario_dump_before_graphics_init,
		"consecutive": scenario_consecutive_console_dumps,
		"open_failure": scenario_dump_retries_after_open_failure,
	}
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("build_dir", type=Path)
	parser.add_argument("scenario", nargs="?", choices=(*scenarios, "all"), default="all")
	args = parser.parse_args()

	with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as reservation:
		reservation.bind(("127.0.0.1", 0))
		proxy = f"http://127.0.0.1:{reservation.getsockname()[1]}"
		client_env = {name: proxy for name in ("http_proxy", "https_proxy", "all_proxy", "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY")}
		client_env.update({"no_proxy": "", "NO_PROXY": "", "QMCLIENT_TEST_HIDE_DIALOG": "1"})
		selected = scenarios.values() if args.scenario == "all" else (scenarios[args.scenario],)
		for scenario in selected:
			env = ProcessEnvironment(args.build_dir, temp_prefix="qm_console_dump_")
			try:
				scenario(env, client_env)
			finally:
				env.close()
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
