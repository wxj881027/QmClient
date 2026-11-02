"""设置页签译文生命周期的真实进程回归。"""

from __future__ import annotations

try:
	from qmclient_scripts.integration.process_harness import ProcessEnvironment
except ModuleNotFoundError:
	from process_harness import ProcessEnvironment  # type: ignore[no-redef]


def _render_settings_page(env: ProcessEnvironment, page: int) -> None:
	# 截图成功发生在真实绘制和交换缓冲之后，避免只用 echo 证明命令被处理。
	env.client.command(f"ui_settings_page {page}; screenshot")
	env.client.wait_for(lambda line: line.startswith("client: Saved screenshot to '"), "settings page rendered", 15)


def scenario_settings_language_round_trip(env: ProcessEnvironment) -> None:
	"""未访问页签期间切走语言再切回同名文件，三组页签仍能绘制并正常退出。"""
	language = "languages/qm_e2e_tabs.txt"
	path = env.path(language)
	path.parent.mkdir(parents=True, exist_ok=True)
	keys = (
		"Settings", "Bind Wheel", "War List", "Chat Binds", "Status Bar",
		"HUD", "Chat", "Name Plate", "Hook Collisions", "Info Messages", "Laser",
		"Entities", "Game", "Emoticons", "Particles", "Mouse", "Direction Keys",
		"Strong Weak Hook", "Entity Background Image", "Other",
	)
	path.write_text("\n\n".join(f"{key}\n== First {key}" for key in keys) + "\n", encoding="utf-8")
	env.start_client(
		["qm_steam_auto_launch 0", "cl_show_welcome 0", "cl_skip_start_menu 1", "qm_settings_prewarm 0",
		 "ui_page 16", "ui_settings_page 10", f'cl_languagefile "{language}"'],
		connect=False,
		env={"QMCLIENT_TEST_HIDE_DIALOG": "1"},
	)
	# 固定页号来自玩家可设置的 ui_settings_page：TClient、外观、素材。
	for page in (10, 4, 9):
		_render_settings_page(env, page)
	_render_settings_page(env, 1)
	for revision in range(2):
		env.client.command('cl_languagefile ""')
		_render_settings_page(env, 1)
		# 同一路径的新内容，释放旧语言堆后改变译文分配布局。
		path.write_text("\n\n".join(f"{key}\n== Reload {revision} {'expanded ' * (revision + 3)}{key}" for key in keys) + "\n", encoding="utf-8")
		env.client.command(f'cl_languagefile "{language}"')
		env.client.wait_for(lambda line: line == f"localization: loaded '{language}'", "language reloaded", 15)
		_render_settings_page(env, 1)
		for page in (10, 4, 9):
			_render_settings_page(env, page)
		_render_settings_page(env, 1)
	env.client.command("quit")
	code = env.client.wait_for_exit(15)
	if code != 0:
		raise AssertionError(f"client exited with {code} after language round trips")
	reports = list(env.path("dumps", "QmClient_Crash").glob("*.txt"))
	if reports:
		raise AssertionError(f"language round trip produced crash reports: {reports}")
