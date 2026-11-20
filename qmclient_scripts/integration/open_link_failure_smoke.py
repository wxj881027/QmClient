#!/usr/bin/env python3
"""隔离 Unix 打开器，验证 exec 失败、异步返回和子进程回收。"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shlex
import signal
import subprocess
import sys
import tempfile

REPO_ROOT = Path(__file__).resolve().parents[2]
LINK = "https://example.invalid/path?value=two words;$(unused)"


def run_probe(binary: Path, directory: Path, mode: str, target: str, wait_for_return: bool = False) -> None:
	environment = os.environ.copy()
	environment["PATH"] = str(directory)
	command = [str(binary), "--qm-test-open-link", mode, target]
	if wait_for_return:
		command.append("wait-for-return")
	process = subprocess.Popen(
		command,
		cwd=directory,
		env=environment,
		stdin=subprocess.DEVNULL,
		stdout=subprocess.PIPE,
		stderr=subprocess.PIPE,
		text=True,
		encoding="utf-8",
		errors="replace",
		start_new_session=True,
	)
	timed_out = False
	try:
		stdout, stderr = process.communicate(timeout=5)
	except subprocess.TimeoutExpired:
		timed_out = True
		try:
			os.killpg(process.pid, signal.SIGKILL)
		except ProcessLookupError:
			pass
		stdout, stderr = process.communicate(timeout=2)
	finally:
		# 只回收本 runner 创建的独立进程组，失败的启动器也不能残留。
		try:
			os.killpg(process.pid, signal.SIGKILL)
		except ProcessLookupError:
			pass
	(directory / "probe.stdout.log").write_text(stdout, encoding="utf-8")
	(directory / "probe.stderr.log").write_text(stderr, encoding="utf-8")
	if timed_out:
		raise AssertionError(f"launcher probe timed out: {stdout}\n{stderr}")
	if process.returncode == 77:
		raise RuntimeError("launcher probe requires waitid with WNOWAIT")
	if "launcher-returned-to-probe" in stderr:
		raise AssertionError(f"launcher returned to the probe after exec failed: {stderr}")
	if process.returncode != 0 or "launcher-reaped" not in stdout.splitlines():
		raise AssertionError(f"launcher probe failed ({process.returncode}): {stdout}\n{stderr}")


def launcher_path(directory: Path) -> Path:
	return directory / ("xdg-open" if sys.platform.startswith("linux") else "open")


def install_launcher(directory: Path, executable: bool, wait_for_return: bool = False) -> Path:
	marker = directory / "launcher.argument.txt"
	launcher = launcher_path(directory)
	script = "#!/bin/sh\n"
	if wait_for_return:
		script += "read -r line || :\n"
	script += f"printf '%s' \"$1\" > {shlex.quote(str(marker))}\nexit 0\n"
	launcher.write_text(script, encoding="utf-8")
	launcher.chmod(0o700 if executable else 0o600)
	return marker


def smoke_missing_launcher(binary: Path, directory: Path) -> None:
	run_probe(binary, directory, "link", LINK)


def smoke_nonexecutable_launcher(binary: Path, directory: Path) -> None:
	marker = install_launcher(directory, False)
	run_probe(binary, directory, "link", LINK)
	if marker.exists():
		raise AssertionError("non-executable launcher was unexpectedly run")


def smoke_launcher_recovery(binary: Path, directory: Path) -> None:
	run_probe(binary, directory, "link", LINK)
	marker = install_launcher(directory, True, wait_for_return=True)
	run_probe(binary, directory, "link", LINK, wait_for_return=True)
	if marker.read_text(encoding="utf-8") != LINK:
		raise AssertionError("launcher did not receive the original URL argument")


def smoke_open_file_delegation(binary: Path, directory: Path) -> None:
	target = directory / "文件 目录"
	target.mkdir()
	marker = install_launcher(directory, True)
	run_probe(binary, directory, "file", str(target))
	expected = str(target) if sys.platform == "darwin" else f"file://{target}"
	if marker.read_text(encoding="utf-8") != expected:
		raise AssertionError("open_file did not pass the expected path to the launcher")


SCENARIOS = {
	"missing_launcher": smoke_missing_launcher,
	"nonexecutable_launcher": smoke_nonexecutable_launcher,
	"launcher_recovery": smoke_launcher_recovery,
	"open_file_delegation": smoke_open_file_delegation,
}


def main() -> int:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("testrunner", type=Path)
	parser.add_argument("scenario", nargs="?", choices=["all", *SCENARIOS], default="all")
	args = parser.parse_args()
	if os.name != "posix":
		parser.error("this runner requires Unix")
	binary = args.testrunner.resolve()
	if not binary.is_file():
		parser.error("testrunner must already be built for the current source")
	root = REPO_ROOT / "tmp" / "tests"
	root.mkdir(parents=True, exist_ok=True)
	output = Path(tempfile.mkdtemp(prefix="open-link-", dir=root))
	names = list(SCENARIOS) if args.scenario == "all" else [args.scenario]
	for name in names:
		directory = output / name
		directory.mkdir()
		SCENARIOS[name](binary, directory)
	print(json.dumps({"scenarios": names, "logs": str(output)}, ensure_ascii=False))
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
