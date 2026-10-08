"""菜单地图切换、加载失败与退出清理的真实进程回归场景。"""

from __future__ import annotations

try:
	from qmclient_scripts.integration.process_harness import ProcessEnvironment
except ModuleNotFoundError:
	from process_harness import ProcessEnvironment  # type: ignore[no-redef]


def _assert_menu_map_loaded(env: ProcessEnvironment, theme: str) -> None:
	failure = f"menuthemes: failed to load menu theme '{theme}'"
	if any(failure in line for line in env.client._lines):
		raise AssertionError(f"menu map failed to load: {theme}")


def _wait_for_menu_map(env: ProcessEnvironment, theme: str) -> None:
	env.client.wait_for(
		lambda line: line == f"datafile: loading done. datafile='themes/{theme}'",
		f"menu map file loaded: {theme}",
		30,
	)
	# 文件读取之后还会校验地图并初始化图层；等待后续命令执行再检查加载结果。
	marker = f"menu_background_loaded_{theme}"
	env.client.command(f"echo {marker}")
	env.client.wait_for(lambda line: marker in line, "menu map load command completed", 10)
	_assert_menu_map_loaded(env, theme)


def _start_menu_client(env: ProcessEnvironment, *, connect: bool = False) -> None:
	if connect:
		env.start_server()
	env.start_client(
		["stdout_output_level 2", "qm_steam_auto_launch 0", "cl_menu_map heavens_day.map"],
		connect=connect,
		env={"QMCLIENT_TEST_HIDE_DIALOG": "1"},
	)
	if connect:
		env.server.wait_for(lambda line: line.startswith("server: player has entered the game"), "client connection", 30)
	_wait_for_menu_map(env, "heavens_day.map")


def _switch_menu_map(env: ProcessEnvironment, theme: str) -> None:
	env.client.command(f"cl_menu_map {theme}")
	_wait_for_menu_map(env, theme)


def _wait_for_clean_exit(env: ProcessEnvironment) -> None:
	code = env.client.wait_for_exit(15)
	if code != 0:
		raise AssertionError(f"client exited with {code} after switching menu maps")
	reports = list(env.path("dumps", "QmClient_Crash").glob("*_fatal_report.txt"))
	if reports:
		raise AssertionError(f"client produced a fatal crash report: {reports}")


def scenario_menu_background_switch_shutdown(env: ProcessEnvironment) -> None:
	"""正常断开连接后连续切换菜单地图，释放旧背景后退出不崩溃。"""
	_start_menu_client(env, connect=True)
	env.client.command("disconnect; echo menu_background_disconnected")
	env.client.wait_for(lambda line: "menu_background_disconnected" in line, "client disconnected", 10)
	# 第二次切换即使发生在过渡完成前，也会释放首次保留的地图。
	_switch_menu_map(env, "autumn_day.map")
	_switch_menu_map(env, "winter_day.map")
	env.client.command("quit")
	_wait_for_clean_exit(env)


def scenario_menu_background_failed_switch_shutdown(env: ProcessEnvironment) -> None:
	"""菜单地图加载失败后仍可加载有效地图，并正常退出。"""
	_start_menu_client(env)
	missing_theme = "qm_e2e_missing_menu.map"
	env.client.command(f"cl_menu_map {missing_theme}")
	env.client.wait_for(
		lambda line: line == f"menuthemes: failed to load menu theme '{missing_theme}'",
		"missing menu map rejected",
		10,
	)
	_switch_menu_map(env, "autumn_day.map")
	env.client.command("quit")
	_wait_for_clean_exit(env)


def scenario_menu_background_pending_switch_shutdown(env: ProcessEnvironment) -> None:
	"""切换菜单地图后立即退出，允许背景过渡尚未完成。"""
	_start_menu_client(env)
	# 同一条控制台指令中退出，避免切换与退出之间插入渲染帧。
	env.client.command("cl_menu_map autumn_day.map; quit")
	env.client.wait_for(
		lambda line: line == "datafile: loading done. datafile='themes/autumn_day.map'",
		"menu map loaded before immediate quit",
		30,
	)
	_wait_for_clean_exit(env)
	_assert_menu_map_loaded(env, "autumn_day.map")
