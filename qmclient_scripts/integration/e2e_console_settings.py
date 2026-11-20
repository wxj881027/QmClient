"""启动参数请求打开控制台外观时，客户端仍能进入主循环并正常退出。"""

from __future__ import annotations

import argparse
from pathlib import Path

try:
	from qmclient_scripts.integration.process_harness import ProcessEnvironment
except ModuleNotFoundError:
	from process_harness import ProcessEnvironment  # type: ignore[no-redef]


def scenario_console_settings_startup(env: ProcessEnvironment) -> None:
	client = env.start_client(
		["qm_steam_auto_launch 0", "qm_auto_update 0", "cl_show_welcome 1", "qm_console_settings"],
		connect=False,
	)
	client.command("echo console_settings_startup_ready")
	client.wait_for(lambda line: line == "console: console_settings_startup_ready", "console command response after startup", 15)
	client.command("quit")
	code = client.wait_for_exit(15)
	if code != 0:
		raise AssertionError(f"client exited with {code}")


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("build_dir", type=Path)
	args = parser.parse_args()

	env = ProcessEnvironment(args.build_dir, temp_prefix="qm_console_settings_e2e_")
	keep_temp = True
	try:
		scenario_console_settings_startup(env)
		keep_temp = False
	finally:
		env.close(keep_temp=keep_temp)
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
