"""回放浏览器读取 Windows 早期文件日期后继续绘制列表。"""

from __future__ import annotations

import json
import os
from unittest import SkipTest

try:
	from qmclient_scripts.integration.process_harness import ProcessEnvironment
except ModuleNotFoundError:
	from process_harness import ProcessEnvironment  # type: ignore[no-redef]


def _perf_fields(line: str, system: str) -> dict[str, str]:
	prefix = f"{system}: "
	if not line.startswith(prefix):
		return {}
	return json.loads(line.removeprefix(prefix))


def scenario_demo_browser_pre_epoch_date(env: ProcessEnvironment) -> None:
	"""Windows 用文件元数据返回 1970 年前的日期，回放列表仍能完成下一帧。"""
	if os.name != "nt":
		raise SkipTest("the Windows runtime rejects dates before the Unix epoch")

	directory = env.path("demo-browser-dates")
	directory.mkdir()
	# 合影条目走回放列表的日期列，浏览时无需解析内容。
	pose = directory / "media-date.poses"
	pose.touch()
	mtime = -2_678_400 * 1_000_000_000  # 1969-12-01 UTC
	os.utime(pose, ns=(mtime, mtime))
	if pose.stat().st_mtime_ns != mtime:
		raise AssertionError("the isolated fixture did not retain its pre-epoch modification time")

	client = env.start_client(
		[
			"stdout_output_level 2",
			"qm_perf_debug 1",
			"qm_auto_update 0",
			"qm_steam_auto_launch 0",
			"qm_websocket_url disabled",
			"cl_show_welcome 0",
			"cl_skip_start_menu 1",
			"ui_page 15",  # PAGE_DEMOS
			"br_demo_sort 3",  # SORT_DATE
			f'qm_demo_directory "{directory.as_posix()}"',
		],
		connect=False,
		env={"QMCLIENT_TEST_HIDE_DIALOG": "1"},
	)

	def visible_date_loaded(line: str) -> bool:
		fields = _perf_fields(line, "perf/interaction")
		return (
			fields.get("event") == "demo_browser_date_fetch"
			and fields.get("source") == "demos"
			and fields.get("trigger") == "list_frame"
			and int(fields.get("visible_done", "0")) >= 1
		)

	date_loaded = client.wait_for(visible_date_loaded, "visible replay-browser file date loaded", 30)
	metadata_frame = int(_perf_fields(date_loaded, "perf/interaction")["frame"])

	def list_rendered_date(line: str) -> bool:
		# 列表遍历可见行之后才记预算。等下一帧可避免元数据刚读取就退出。
		fields = _perf_fields(line, "perf/settings-resource")
		return (
			fields.get("event") == "settings_adaptive_budget"
			and fields.get("source") == "demo_browser"
			and fields.get("tab") == "list"
			and int(fields["frame"]) > metadata_frame
		)

	client.wait_for(list_rendered_date, "replay browser rendered after fetching the file date", 10)
	client.command("quit")
	code = client.wait_for_exit(15)
	if code != 0:
		raise AssertionError(f"client exited with {code} after rendering the file date")
