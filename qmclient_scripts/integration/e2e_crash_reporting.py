"""Windows 结构化异常报告与退出的真实进程回归。"""

from __future__ import annotations

import sys
from unittest import SkipTest

try:
	from qmclient_scripts.integration.process_harness import ProcessEnvironment
except ModuleNotFoundError:
	from process_harness import ProcessEnvironment  # type: ignore[no-redef]


def scenario_fatal_exception_exits_without_hang(env: ProcessEnvironment) -> None:
	"""真实 Windows 异常写完本地报告与转储后退出，不再卡在在线符号处理中。"""
	if sys.platform != "win32":
		raise SkipTest("structured exception reporting requires Windows")
	if "EXCEPTION_HANDLING:BOOL=OFF" in (env.build_dir / "CMakeCache.txt").read_text(encoding="utf-8"):
		raise SkipTest("structured exception reporting is disabled in this build")
	env.start_client(
		["qm_steam_auto_launch 0", "--qm-test-main-thread-exception"],
		connect=False,
		env={"QMCLIENT_TEST_HIDE_DIALOG": "1"},
		startup_timeout=None,
	)
	env.client.wait_for(lambda line: line == "test/crashdump: raising_noncontinuable_access_violation", "injected structured exception", 30)
	code = env.client.wait_for_exit(30)
	if code == 0:
		raise AssertionError("fatal exception unexpectedly exited successfully")
	dump_dir = env.path("dumps", "QmClient_Crash")
	reports = list(dump_dir.glob("*_fatal_report.txt"))
	if len(reports) != 1:
		raise AssertionError(f"expected one local fatal report: {reports}")
	content = reports[0].read_text(encoding="utf-8", errors="replace")
	if "0xC0000005" not in content or "Fallback minidump written: yes" not in content:
		raise AssertionError(f"original exception or minidump result missing: {content}")
	if not reports[0].with_name(reports[0].name + ".confirmed").is_file():
		raise AssertionError("unhandled exception filter did not confirm the fatal report")
	dumps = list(dump_dir.glob("*_fatal_dump.dmp"))
	if len(dumps) != 1:
		raise AssertionError(f"expected one fatal minidump: {dumps}")
	with dumps[0].open("rb") as dump:
		if dump.read(4) != b"MDMP":
			raise AssertionError("fatal dump is not a minidump")
	hangs = list(dump_dir.glob("*hang_report_*.txt")) + list(dump_dir.glob("*hang_dump_*.dmp"))
	if hangs:
		raise AssertionError(f"fatal exception also produced a hang report: {hangs}")
