#!/usr/bin/env python3
"""使用已构建的testrunner验证Windows原生异常与崩溃文件，不启动完整客户端。"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

REPO_ROOT = Path(__file__).resolve().parents[2]
TIMEOUT_SECONDS = 20


def run_probe(binary: Path, directory: Path, mode: str, log: Path | None = None) -> subprocess.CompletedProcess[bytes]:
	arguments = [str(binary), "--qm-test-crashdump", mode]
	if log is not None:
		arguments.append(str(log))
	process = subprocess.Popen(
		arguments,
		cwd=directory,
		stdout=subprocess.PIPE,
		stderr=subprocess.PIPE,
		creationflags=subprocess.CREATE_NO_WINDOW,
	)
	timed_out = False
	try:
		stdout, stderr = process.communicate(timeout=TIMEOUT_SECONDS)
	except subprocess.TimeoutExpired:
		timed_out = True
		process.kill()
		stdout, stderr = process.communicate(timeout=5)
	finally:
		if process.poll() is None:
			process.kill()
			process.wait(timeout=5)
	(directory / "probe.stdout.log").write_bytes(stdout)
	(directory / "probe.stderr.log").write_bytes(stderr)
	(directory / "probe.exit.json").write_text(
		json.dumps({"mode": mode, "returncode": process.returncode, "timed_out": timed_out}), encoding="utf-8"
	)
	if timed_out:
		raise AssertionError(f"crashdump probe timed out; artifacts: {directory}")
	return subprocess.CompletedProcess(arguments, process.returncode, stdout, stderr)


def paths(directory: Path) -> tuple[Path, Path, Path, Path]:
	return (
		directory / "probe.RPT",
		directory / "probe_fatal_report.txt",
		directory / "probe_fatal_dump.dmp",
		directory / "probe_fatal_report.txt.confirmed",
	)


def require_exit(result: subprocess.CompletedProcess[bytes], expected: int) -> None:
	if result.returncode != expected:
		raise AssertionError(f"expected exit {expected}, got {result.returncode}: {result.stderr!r}")


def check_fatal_report(directory: Path, reason: str, *, exception: str | None = None) -> None:
	_, report, dump, confirmed = paths(directory)
	text = report.read_text(encoding="utf-8")
	if "Report type: fatal-crash" not in text or f"Reason: {reason}" not in text:
		raise AssertionError(f"wrong fatal report: {report}")
	if "Fallback minidump written: yes" not in text:
		raise AssertionError(f"native minidump failed: {report}")
	if exception is not None and exception not in text:
		raise AssertionError(f"wrong native exception attribution: {report}")
	if "Vectored exception fallback" in text or "EXCEPTION_ACCESS_VIOLATION" in text:
		raise AssertionError(f"previous recovered exception replaced the fatal event: {report}")
	with dump.open("rb") as file:
		if file.read(4) != b"MDMP":
			raise AssertionError(f"invalid native minidump signature: {dump}")
	if confirmed.read_bytes() != b"confirmed fatal crash\r\n":
		raise AssertionError(f"missing or partial confirmation: {confirmed}")


def scenario_recovered_exception_does_not_create_fatal_files(binary: Path, directory: Path) -> None:
	log, report, dump, confirmed = paths(directory)
	require_exit(run_probe(binary, directory, "recovered", log), 0)
	if any(path.exists() for path in (report, dump, confirmed)):
		raise AssertionError("recovered first-chance exception created fatal artifacts")


def scenario_recovered_exception_preserves_existing_report(binary: Path, directory: Path) -> None:
	log, report, dump, confirmed = paths(directory)
	for path in (report, dump, confirmed):
		path.write_bytes(b"previous-run:" + path.name.encode("ascii"))
	require_exit(run_probe(binary, directory, "recovered", log), 0)
	for path in (report, dump, confirmed):
		if path.read_bytes() != b"previous-run:" + path.name.encode("ascii"):
			raise AssertionError(f"recovered exception overwrote existing artifact: {path}")


def scenario_fatal_after_recovery_uses_current_reason(binary: Path, directory: Path) -> None:
	log, _, _, _ = paths(directory)
	require_exit(run_probe(binary, directory, "recovered-then-terminate", log), 3)
	check_fatal_report(directory, "Unhandled C++ exception (std::terminate)")


def scenario_unhandled_seh_still_writes_current_exception(binary: Path, directory: Path) -> None:
	log, _, _, _ = paths(directory)
	result = run_probe(binary, directory, "unhandled", log)
	# Windows/CRT或前置filter可决定终止码；必须是异常终止并有真实故障报告。
	if result.returncode in (0, 2, 4, 5, 6, 7, 77):
		raise AssertionError(f"native unhandled exception did not terminate as expected: {result.returncode}")
	check_fatal_report(directory, "Unhandled structured exception", exception="EXCEPTION_ILLEGAL_INSTRUCTION")


def scenario_unicode_directory_keeps_all_fatal_artifacts(binary: Path, directory: Path) -> None:
	unicode_directory = directory / "崩溃-🚀"
	unicode_directory.mkdir()
	log, _, _, _ = paths(unicode_directory)
	require_exit(run_probe(binary, directory, "terminate", log), 3)
	check_fatal_report(unicode_directory, "Unhandled C++ exception (std::terminate)")


SCENARIOS = {
	"recovered_no_artifacts": scenario_recovered_exception_does_not_create_fatal_files,
	"recovered_preserves_existing": scenario_recovered_exception_preserves_existing_report,
	"fatal_after_recovery": scenario_fatal_after_recovery_uses_current_reason,
	"unhandled_seh": scenario_unhandled_seh_still_writes_current_exception,
	"unicode_directory": scenario_unicode_directory_keeps_all_fatal_artifacts,
}


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("testrunner", type=Path, help="当前源码已构建且EXCEPTION_HANDLING=ON的Windows测试程序")
	parser.add_argument("scenario", nargs="?", choices=["all", *SCENARIOS], default="all")
	args = parser.parse_args()
	if os.name != "nt":
		parser.error("requires Windows")
	binary = args.testrunner.resolve()
	if not binary.is_relative_to(REPO_ROOT) or not binary.is_file():
		parser.error("testrunner must already be built inside this workspace for the current source")
	root = REPO_ROOT / "tmp" / "tests"
	root.mkdir(parents=True, exist_ok=True)
	output = Path(tempfile.mkdtemp(prefix="crashdump-", dir=root))
	capabilities = output / "capabilities"
	capabilities.mkdir()
	result = run_probe(binary, capabilities, "capabilities")
	if result.returncode != 0 or b"windows-crashdump=enabled" not in result.stdout:
		parser.error(f"requires current probe and EXCEPTION_HANDLING=ON; artifacts: {output}")
	names = list(SCENARIOS) if args.scenario == "all" else [args.scenario]
	for name in names:
		directory = output / name
		directory.mkdir()
		SCENARIOS[name](binary, directory)
	print(json.dumps({"scenarios": names, "logs": str(output)}, ensure_ascii=False))
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
