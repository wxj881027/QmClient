"""Run QmClient-owned Python tests."""

from __future__ import annotations

import sys
from pathlib import Path

from lib import runner
from lib.report import ResultCollector


def run(results: ResultCollector, included: list[str], dry_run: bool = False) -> None:
	roots = (
		Path("qmclient_scripts/tests"),
		Path("qmclient_scripts/gate/tests"),
		Path("qmclient_scripts/integration"),
	)
	outputs = []
	for root in roots:
		command = [
			sys.executable,
			"-m",
			"unittest",
			"discover",
			"-s",
			str(root),
			"-p",
			"test*.py",
		]
		if dry_run:
			outputs.append("DryRun: " + " ".join(command))
			continue
		code, output = runner.run(command, title=f"QmClient Python tests: {root}", check=False)
		outputs.append(output)
		if code != 0:
			results.add("FAIL", "QmClient Python 测试", "\n".join(outputs))
			return
	if dry_run:
		results.add("INFO", "QmClient Python 测试", "\n".join(outputs))
	else:
		results.add("PASS", "QmClient Python 测试", "\n".join(outputs))
